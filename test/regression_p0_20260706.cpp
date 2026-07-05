#include <cassert>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <memory>

#include "../src/eval.hpp"
#include "../src/ft.hpp"
#include "../src/io.hpp"
#include "../src/placement_engine.hpp"
#include "../src/routing.hpp"
#include "../src/search.hpp"
#include "../src/validate.hpp"

namespace {
using namespace efgr;

bool near(double a, double b, double tol = 1e-6) { return std::abs(a - b) <= tol; }

Block softBlockWithRates() {
    Block b;
    b.type = BlockType::Soft;
    b.ft_rates = {0.2, 0.4, 0.8, 1.0};
    return b;
}

void testFtMathOfficialExamples() {
    Block b = softBlockWithRates();
    assert(near(ftTierRate(b, 1000.0), 0.2));
    assert(near(ftTierRate(b, 3500.0), 0.4));
    assert(near(ftTierRate(b, 6711.0), 0.8));
    assert(near(ftTierRate(b, 12013.0), 1.0));

    assert(near(ftExtend(1000.0, 0.2), 4.0));
    assert(near(ftExtend(3500.0, 0.4), 28.0));
    assert(near(ftExtend(6711.0, 0.8), 108.0));   // 107.376 -> ceil
    assert(near(ftExtend(12013.0, 1.0), 241.0));  // 240.26 -> ceil

    assert(near(ftRequiredArea(87500.0, 4.0), 89882.43));
    assert(near(ftRequiredArea(87500.0, 28.0), 104849.02));
    assert(near(ftRequiredArea(87500.0, 108.0), 163057.66));
    assert(near(ftRequiredArea(87500.0, 241.0), 288158.52));
    std::cout << "testFtMathOfficialExamples ok\n";
}

Problem forcedFtProblem() {
    Problem p;
    auto addBlock = [&](const std::string& name, double area, double w, double h, BlockType type) {
        Block b;
        b.name = name;
        b.area = area;
        b.width = w;
        b.height = h;
        b.type = type;
        b.ar_min = 0.25;
        b.ar_max = 4.0;
        b.ft_rates = {0.2, 0.4, 0.8, 1.0};
        p.block_index[name] = static_cast<int>(p.blocks.size());
        p.blocks.push_back(b);
    };
    addBlock("BLK01", 30000.0, 100.0, 300.0, BlockType::Macro);
    addBlock("BLK02", 90000.0, 300.0, 300.0, BlockType::Soft);
    addBlock("BLK03", 30000.0, 100.0, 300.0, BlockType::Macro);
    p.outline_w = 500.0;  // exactly the row bbox: no channels anywhere
    p.outline_h = 300.0;
    p.alpha = 1.0;
    Bundle bd;
    bd.source = 0;
    bd.target = 2;
    bd.nets = 900;  // single chunk, tier 1
    p.bundles.push_back(bd);
    return p;
}

Placement rowPlacement() {
    Placement pl;
    pl.rects = {Rect{0.0, 0.0, 100.0, 300.0}, Rect{100.0, 0.0, 300.0, 300.0}, Rect{400.0, 0.0, 100.0, 300.0}};
    pl.aspect = {1.0, 1.0, 1.0};
    return pl;
}

void testRoutingMismatchUsesCeiledMath() {
    Problem p = forcedFtProblem();
    Placement pl = rowPlacement();
    RoutingResult r = routeProblem(p, pl);
    assert(r.open_routes == 0);
    assert(r.soft_feedthrough_nets[1] > 0.0);  // FT through BLK02 was forced
    double required = ftRequiredArea(90000.0, ftExtend(900.0, 0.2));  // extend = ceil(3.6) = 4 -> (300+4)^2 = 92416.00
    assert(near(required, 92416.00));
    assert(near(r.feedthrough_mismatch, required - 90000.0, 1e-3));
    std::cout << "testRoutingMismatchUsesCeiledMath ok\n";
}

void testDirectAbutmentZeroWirelength() {
    Problem p;
    auto addSoft = [&](const std::string& name, double x_hint) {
        (void)x_hint;
        Block b;
        b.name = name;
        b.area = 10000.0;
        b.type = BlockType::Soft;
        b.ar_min = 0.25;
        b.ar_max = 4.0;
        b.ft_rates = {0.2, 0.4, 0.8, 1.0};
        p.block_index[name] = static_cast<int>(p.blocks.size());
        p.blocks.push_back(b);
    };
    addSoft("BLK01", 0.0);
    addSoft("BLK02", 100.0);
    p.outline_w = 200.0;
    p.outline_h = 100.0;
    p.alpha = 1.0;
    Bundle bd;
    bd.source = 0;
    bd.target = 1;
    bd.nets = 100;
    p.bundles.push_back(bd);

    Placement pl;
    pl.rects = {Rect{0.0, 0.0, 100.0, 100.0}, Rect{100.0, 0.0, 100.0, 100.0}};
    pl.aspect = {1.0, 1.0};

    RoutingResult r = routeProblem(p, pl);
    assert(r.open_routes == 0);
    assert(r.routes.size() == 1);
    assert(r.routes[0].node_path.size() == 2);  // direct block->block, no channel
    assert(near(r.routes[0].wirelength, 0.0));  // 1 guiding point -> empty HPWL sum
    assert(near(r.total_wirelength, 0.0));
    std::cout << "testDirectAbutmentZeroWirelength ok\n";
}
void testOutlineConsistencyEvalVsCfg() {
    Problem p = forcedFtProblem();
    Placement pl = rowPlacement();
    RoutingResult routing;
    Score exact = exactScore(p, pl, &routing);
    auto [w, h] = usedOutline(p, pl, routing);
    assert(near(exact.outline_area, w * h, 1e-6));

    const std::string tmp = "test/p0_outline_tmp.cfg";
    writeCfg(tmp, p, pl, routing);
    std::ifstream in(tmp);
    assert(in.good());
    std::string tok;
    double cfg_w = -1.0, cfg_h = -1.0;
    while (in >> tok) {
        if (tok == "Outline" || tok == "OUTLINE") {
            in >> cfg_w >> cfg_h;
            break;
        }
    }
    std::remove(tmp.c_str());
    assert(near(cfg_w * cfg_h, exact.outline_area, 1e-6));
    std::cout << "testOutlineConsistencyEvalVsCfg ok\n";
}

void testFeedthroughFinalizeWritesBackArea() {
    Problem p = forcedFtProblem();
    p.outline_w = 620.0;  // headroom so the inflated decode still fits
    p.outline_h = 340.0;

    std::unique_ptr<PlacementEngine> engine = makePlacementEngine("bstar");
    SearchOptions options;
    options.iters = 200;
    options.seed = 1;
    options.exact_interval = 20;
    SearchResult result = runPlacementSearch(p, *engine, options);

    // After the finalize hook, the emitted solution must not owe FT area.
    assert(result.best_routing.feedthrough_mismatch <= 1e-6);
    // And if FT was used, the soft block's dims must cover the ceil'd required area.
    double nets = result.best_routing.soft_feedthrough_nets.size() > 1 ? result.best_routing.soft_feedthrough_nets[1] : 0.0;
    if (nets > 0.0) {
        double required = ftRequiredArea(90000.0, ftExtend(nets, ftTierRate(p.blocks[1], nets)));
        const Rect& r = result.best_placement.rects[1];
        assert(r.w * r.h + 1e-6 >= required);
    }
    auto validation = validateSolution(p, result.best_placement, result.best_routing);
    assert(validation.ok);
    std::cout << "testFeedthroughFinalizeWritesBackArea ok\n";
}

void testFinalizeFeedthroughDirect() {
    Problem p = forcedFtProblem();  // 500x300, FT forced
    p.outline_w = 620.0;
    // Extra headroom above the wall: BStarEngine::seedFromPlacement/decode does not
    // preserve row layout on re-seed -- it re-derives geometry from a fresh
    // contour-packed tree (sorted by original x,y), which can restack the inflated
    // soft block above the wall rather than widening it in place. With outline_h
    // == wall-top (340) there is zero headroom for that restack and the inflated
    // candidate is rejected as out-of-outline. 700 gives enough headroom for the
    // restacked decode to still fit, while the wall (y=300..340) still blocks
    // channel routing over the top, forcing FT as required by the fixture.
    p.outline_h = 700.0;
    Placement pl = rowPlacement();
    // Wall off the top so channel routing cannot connect BLK01->BLK03 (forces FT):
    Block wall;
    wall.name = "BLK04";
    wall.area = 620.0 * 40.0;
    wall.width = 620.0;
    wall.height = 40.0;
    wall.type = BlockType::Macro;
    wall.ft_rates = {0.2, 0.4, 0.8, 1.0};
    p.block_index[wall.name] = static_cast<int>(p.blocks.size());
    p.blocks.push_back(wall);
    pl.rects.push_back(Rect{0.0, 300.0, 620.0, 40.0});
    pl.aspect.push_back(1.0);

    SearchResult result;
    result.best_placement = pl;
    result.best_score = exactScore(p, pl, &result.best_routing);
    assert(result.best_routing.feedthrough_mismatch > 0.0);  // precondition: FT happened, under-provisioned

    std::unique_ptr<PlacementEngine> engine = makePlacementEngine("bstar");
    finalizeFeedthrough(p, *engine, result);
    assert(result.best_routing.feedthrough_mismatch <= 1e-6);
    std::cout << "testFinalizeFeedthroughDirect ok\n";
}

}  // namespace

int main() {
    testFtMathOfficialExamples();
    testRoutingMismatchUsesCeiledMath();
    testDirectAbutmentZeroWirelength();
    testOutlineConsistencyEvalVsCfg();
    testFeedthroughFinalizeWritesBackArea();
    testFinalizeFeedthroughDirect();
    std::cout << "p0 regressions passed\n";
    return 0;
}
