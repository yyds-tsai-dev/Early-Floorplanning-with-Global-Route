#include <cmath>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>

#include "../src/eval.hpp"
#include "../src/io.hpp"
#include "../src/placement_engine.hpp"
#include "../src/search.hpp"
#include "../src/validate.hpp"

using namespace efgr;

namespace {

int fail(const std::string& msg) {
    std::cerr << "FAIL: " << msg << "\n";
    return 1;
}

bool near(double a, double b) {
    return std::abs(a - b) < 1e-6;
}

bool anyOverlap(const Placement& placement) {
    for (std::size_t i = 0; i < placement.rects.size(); ++i) {
        for (std::size_t j = i + 1; j < placement.rects.size(); ++j) {
            if (overlaps(placement.rects[i], placement.rects[j])) return true;
        }
    }
    return false;
}

std::string writeTempCsv(const std::string& name, const std::string& content) {
    std::string path = "/tmp/" + name;
    std::ofstream out(path, std::ios::binary);
    out << content;
    return path;
}

Problem makeTwoMacroProblem(int nets) {
    Problem p;
    p.outline_w = 1000.0;
    p.outline_h = 1000.0;
    p.alpha = 0.2;
    p.blocks = {
        Block{"A", 100.0, 10.0, 10.0, 1.0, 1.0, BlockType::Macro, {}, {0.2, 0.4, 0.8, 1.0}, 0, {}},
        Block{"B", 100.0, 10.0, 10.0, 1.0, 1.0, BlockType::Macro, {}, {0.2, 0.4, 0.8, 1.0}, 0, {}},
    };
    p.block_index["A"] = 0;
    p.block_index["B"] = 1;
    p.bundles.push_back(Bundle{0, 1, nets});
    return p;
}

int testBomCsvAndMultiPortEdgeRoute() {
    const std::string csv =
        "\xEF\xBB\xBF"
        "BLOCK,AREA,WIDTH,HEIGHT,ASPECT RATIO RANGE,EDGE HARD MACRO SOFT,LOCATION,PORT EDGE,FT CONVERSION,,,\n"
        ",,,,,,,,<=3000,>3000,>6000,>9000\n"
        "BLK01,100,10,10,1,EDGE,TL,\"1,2\",20%,40%,80%,100%\n"
        "BLK02,100,10,10,1,MACRO,,0,20%,40%,80%,100%\n"
        "OUTLINE,WIDTH,HEIGHT\n"
        "MAX,100,100\n"
        " α,0.2\n"
        "CONN MATRIX\n"
        "ON CHIP INTERFACE CONNECTION (1-1) MATRIX\n"
        ",BLK01,BLK02\n"
        "BLK01,0,10\n"
        "BLK02,0,0\n";
    Problem p;
    try {
        p = parseProblem(writeTempCsv("efgr_20260618_bom_port.csv", csv));
    } catch (const std::exception& e) {
        return fail(std::string("BOM CSV should parse, got: ") + e.what());
    }
    if (p.blocks.size() != 2) return fail("BOM CSV should parse BLOCK table");

    Placement placement;
    placement.rects = {Rect{0.0, 0.0, 10.0, 10.0}, Rect{0.0, 10.0, 10.0, 10.0}};
    placement.aspect = {1.0, 1.0};
    RoutingResult routing = routeProblem(p, placement);
    if (routing.open_routes != 0) return fail("PORT EDGE 1,2 should allow route through edge 2");
    return 0;
}

int testParallelLocationRequiresSegmentOverlap() {
    Problem p;
    p.outline_w = 300.0;
    p.outline_h = 300.0;
    p.blocks = {
        Block{"EDGE", 500.0, 50.0, 10.0, 1.0, 1.0, BlockType::Edge, {"TM", "TR"}, {0.2, 0.4, 0.8, 1.0}, 0, {}},
    };
    p.block_index["EDGE"] = 0;

    Placement bad;
    bad.rects = {Rect{0.0, 290.0, 50.0, 10.0}};
    bad.aspect = {1.0};
    if (validateSolution(p, bad, RoutingResult{}).ok) {
        return fail("TM,TR edge block at TL-only segment should be illegal");
    }

    Placement good;
    good.rects = {Rect{100.0, 290.0, 50.0, 10.0}};
    good.aspect = {1.0};
    if (!validateSolution(p, good, RoutingResult{}).ok) {
        return fail("TM,TR edge block touching TM segment should be legal");
    }
    return 0;
}

int testParallelEdgeBlocksPackWithoutOverlap() {
    Problem p;
    p.outline_w = 6344.0;
    p.outline_h = 3691.0;
    p.blocks = {
        Block{"BLK05", 616160.0, 3851.0, 160.0, 1.0, 1.0, BlockType::Edge, {"TL", "TM", "TR"}, {0.2, 0.4, 0.8, 1.0}, 4, {}},
        Block{"BLK06", 111520.0, 340.0, 328.0, 1.0, 1.0, BlockType::Edge, {"TR", "RT"}, {0.2, 0.4, 0.8, 1.0}, 4, {}},
    };
    p.block_index["BLK05"] = 0;
    p.block_index["BLK06"] = 1;

    std::mt19937 rng(1);
    std::unique_ptr<PlacementEngine> engine = makePlacementEngine("bstar");
    PlacementState state = engine->initialState(p, rng);
    state.edge_location_choice[0] = 2;
    state.edge_location_choice[1] = 0;
    Placement placement = engine->decode(p, state);
    if (anyOverlap(placement)) return fail("edge interval packing should avoid BLK05/BLK06 overlap");
    if (!validateSolution(p, placement, RoutingResult{}).ok) return fail("packed edge placement should validate");
    return 0;
}

int testCheapScoreUsesActualOutline() {
    Problem p = makeTwoMacroProblem(10);
    Placement placement;
    placement.rects = {Rect{0.0, 0.0, 10.0, 10.0}, Rect{20.0, 0.0, 10.0, 10.0}};
    placement.aspect = {1.0, 1.0};
    Score score = cheapScore(p, placement);
    if (!near(score.outline_area, 300.0)) return fail("cheapScore should use placement bounding outline");
    return 0;
}

int testCheapScoreAddsCongestionProxy() {
    Problem p = makeTwoMacroProblem(10000);
    Placement placement;
    placement.rects = {Rect{0.0, 0.0, 10.0, 10.0}, Rect{100.0, 0.0, 10.0, 10.0}};
    placement.aspect = {1.0, 1.0};
    Score score = cheapScore(p, placement);
    if (score.penalty <= 0.0) return fail("cheapScore should penalize obvious high-density routing proxy");
    return 0;
}

class GappedEngine final : public PlacementEngine {
public:
    std::string name() const override { return "gapped"; }
    PlacementState initialState(const Problem&, std::mt19937&) const override { return PlacementState{}; }
    void mutate(const Problem&, PlacementState&, std::mt19937&) const override {}
    bool isValidState(const Problem&, const PlacementState&) const override { return true; }
    Placement decode(const Problem&, const PlacementState&) const override {
        Placement placement;
        placement.rects = {Rect{100.0, 0.0, 10.0, 10.0}, Rect{140.0, 0.0, 10.0, 10.0}};
        placement.aspect = {1.0, 1.0};
        return placement;
    }
};

int testFinalPolishExactEvaluatesCompactedCandidate() {
    Problem p = makeTwoMacroProblem(10);
    SearchOptions options;
    options.iters = 0;
    options.seed = 1;
    options.exact_interval = 1000;
    GappedEngine engine;
    SearchResult result = runPlacementSearch(p, engine, options);
    if (result.exact_evaluations < 2) return fail("final polish should exact-evaluate compacted top candidate");
    if (result.best_placement.rects[0].x > 0.0) return fail("final polish should keep compacted placement when better");
    return 0;
}

}  // namespace

int main() {
    if (int rc = testBomCsvAndMultiPortEdgeRoute()) return rc;
    if (int rc = testParallelLocationRequiresSegmentOverlap()) return rc;
    if (int rc = testParallelEdgeBlocksPackWithoutOverlap()) return rc;
    if (int rc = testCheapScoreUsesActualOutline()) return rc;
    if (int rc = testCheapScoreAddsCongestionProxy()) return rc;
    if (int rc = testFinalPolishExactEvaluatesCompactedCandidate()) return rc;
    std::cerr << "regression_20260618: ok\n";
    return 0;
}
