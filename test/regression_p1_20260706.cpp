#include <cassert>
#include <cmath>
#include <cstdio>
#include <iostream>
#include <memory>
#include <random>
#include <sstream>

#include "../src/eval.hpp"
#include "../src/ft.hpp"
#include "../src/io.hpp"
#include "../src/model.hpp"
#include "../src/placement_engine.hpp"
#include "../src/routing.hpp"
#include "../src/search.hpp"
#include "../src/validate.hpp"

namespace {
using namespace efgr;

Placement twoRectPlacement() {
    Placement pl;
    pl.rects = {Rect{0.0, 0.0, 100.0, 200.0}, Rect{100.0, 0.0, 50.0, 200.0}};
    pl.aspect = {1.0, 1.0};
    return pl;
}

void testPlacementKeyDiffersWithOutline() {
    Placement unset = twoRectPlacement();
    Placement withOutline = twoRectPlacement();
    withOutline.outline_w = 100.0;
    withOutline.outline_h = 200.0;

    std::string key_unset = placementKey(unset);
    std::string key_with_outline = placementKey(withOutline);
    assert(key_unset != key_with_outline);
    std::cout << "testPlacementKeyDiffersWithOutline ok\n";
}

void testPlacementKeyUnsetMatchesTodaysFormat() {
    Placement pl = twoRectPlacement();
    assert(pl.outline_w == 0.0);
    assert(pl.outline_h == 0.0);

    std::ostringstream expected;
    for (const auto& r : pl.rects) {
        expected << std::llround(r.x * 1000.0) << ',' << std::llround(r.y * 1000.0) << ','
                 << std::llround(r.w * 1000.0) << ',' << std::llround(r.h * 1000.0) << ';';
    }
    expected << ':' << std::llround(pl.outline_w * 1000.0) << 'x' << std::llround(pl.outline_h * 1000.0);

    assert(placementKey(pl) == expected.str());
    std::cout << "testPlacementKeyUnsetMatchesTodaysFormat ok\n";
}

double coreBboxArea(const Problem& problem, const Placement& placement) {
    double w = 0.0;
    double h = 0.0;
    for (std::size_t i = 0; i < problem.blocks.size(); ++i) {
        if (isEdge(problem.blocks[i].type)) continue;
        w = std::max(w, placement.rects[i].right());
        h = std::max(h, placement.rects[i].top());
    }
    return std::max(1.0, w) * std::max(1.0, h);
}

void testShrinkWrapEstimateCase0BStarSeed1() {
    Problem problem = parseProblem("test/2026 ICCAD_case.case0.csv");
    std::unique_ptr<PlacementEngine> engine = makePlacementEngine("bstar");
    std::mt19937 rng(1);
    PlacementState state = engine->initialState(problem, rng);
    Placement placement = engine->decode(problem, state);

    OutlineFit fit = shrinkWrapEstimate(problem, placement);
    double max_area = problem.outline_w * problem.outline_h;
    double core_area = coreBboxArea(problem, placement);

    assert(fit.w * fit.h < max_area);
    assert(fit.w * fit.h + 1e-6 >= core_area);
    assert(fit.w <= problem.outline_w + kEps);
    assert(fit.h <= problem.outline_h + kEps);
    std::cout << "testShrinkWrapEstimateCase0BStarSeed1 ok\n";
}

// Synthetic problem with two opposite-corner edge blocks (TL,LT and RB,BR) sized so their
// combined footprint forces the shrink-wrap lower bound to ~MAX on both axes.
void testShrinkWrapEstimateCornerBlocksForceMax() {
    Problem problem;
    auto addBlock = [&](const std::string& name, double w, double h, BlockType type,
                         std::vector<std::string> locs) {
        Block b;
        b.name = name;
        b.width = w;
        b.height = h;
        b.area = w * h;
        b.ar_min = 1.0;
        b.ar_max = 1.0;
        b.type = type;
        b.locations = std::move(locs);
        b.ft_rates = {0.2, 0.4, 0.8, 1.0};
        problem.block_index[name] = static_cast<int>(problem.blocks.size());
        problem.blocks.push_back(b);
    };

    problem.outline_w = 1000.0;
    problem.outline_h = 800.0;
    // Corner blocks sized so their footprints overlap at any (W,H) short of MAX but are
    // disjoint exactly at MAX: TL occupies [0,500]x[H-500,H], BR occupies [W-500,W]x[0,300].
    // At MAX (1000x800) the x-ranges [0,500]/[500,1000] and y-ranges [300,800]/[0,300] both
    // touch with zero positive overlap; at any smaller candidate they genuinely overlap ->
    // forces the feasibility probe to grow all the way to MAX on both axes.
    addBlock("BLK_TL", 500.0, 500.0, BlockType::Edge, {"TL", "LT"});
    addBlock("BLK_BR", 500.0, 300.0, BlockType::Edge, {"RB", "BR"});
    problem.alpha = 0.2;

    Placement placement;
    placement.rects.resize(problem.blocks.size());
    placement.aspect.assign(problem.blocks.size(), 1.0);
    // Seed rough corner positions; shrinkWrapEstimate must not depend on exact input coords
    // for edge blocks beyond their declared dimensions/locations.
    placement.rects[0] = Rect{0.0, problem.outline_h - 500.0, 500.0, 500.0};
    placement.rects[1] = Rect{problem.outline_w - 500.0, 0.0, 500.0, 300.0};

    OutlineFit fit = shrinkWrapEstimate(problem, placement);
    assert(std::abs(fit.w - problem.outline_w) < 1e-6);
    assert(std::abs(fit.h - problem.outline_h) < 1e-6);
    std::cout << "testShrinkWrapEstimateCornerBlocksForceMax ok\n";
}

// No-edge synthetic: two macro blocks only -> estimate must equal the core bbox (floored at 1).
void testShrinkWrapEstimateNoEdgeBlocksEqualsCoreBbox() {
    Problem problem;
    auto addBlock = [&](const std::string& name, double w, double h) {
        Block b;
        b.name = name;
        b.width = w;
        b.height = h;
        b.area = w * h;
        b.ar_min = 1.0;
        b.ar_max = 1.0;
        b.type = BlockType::Macro;
        problem.block_index[name] = static_cast<int>(problem.blocks.size());
        problem.blocks.push_back(b);
    };

    problem.outline_w = 2000.0;
    problem.outline_h = 1500.0;
    addBlock("BLK_A", 300.0, 200.0);
    addBlock("BLK_B", 150.0, 100.0);
    problem.alpha = 0.2;

    Placement placement;
    placement.rects.resize(problem.blocks.size());
    placement.aspect.assign(problem.blocks.size(), 1.0);
    placement.rects[0] = Rect{0.0, 0.0, 300.0, 200.0};
    placement.rects[1] = Rect{300.0, 0.0, 150.0, 100.0};

    OutlineFit fit = shrinkWrapEstimate(problem, placement);
    double expected_w = std::max(1.0, 450.0);
    double expected_h = std::max(1.0, 200.0);
    assert(std::abs(fit.w - expected_w) < 1e-6);
    assert(std::abs(fit.h - expected_h) < 1e-6);
    std::cout << "testShrinkWrapEstimateNoEdgeBlocksEqualsCoreBbox ok\n";
}

}  // namespace

int main() {
    testPlacementKeyDiffersWithOutline();
    testPlacementKeyUnsetMatchesTodaysFormat();
    testShrinkWrapEstimateCase0BStarSeed1();
    testShrinkWrapEstimateCornerBlocksForceMax();
    testShrinkWrapEstimateNoEdgeBlocksEqualsCoreBbox();
    std::cout << "p1 regressions passed\n";
    return 0;
}
