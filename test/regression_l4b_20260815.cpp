// L4b (shape-aware shelf seed + aspect stamp, case5-design.md §4-L4b): unit coverage for
// shelfSeed's deterministic row packing (partition, row heights, feasibility clamp, hard-block
// dims) and stampSoftAspectFromPlacement's clamped stamp, plus a seeded-search smoke test proving
// the plumbing (runHybridPlacementSearch's seed_placement) produces a structurally valid result.
// The budget-off byte-identity hard gate (seed_placement=nullptr on every non-deep path) is the
// manual 8-case battery, per the same convention b2/l3bc's regression files document.
#include <cassert>
#include <cmath>
#include <iostream>
#include <string>
#include <vector>

#include "../src/model.hpp"
#include "../src/placement_engine.hpp"
#include "../src/search.hpp"

namespace {
using namespace efgr;

Block makeSoft(const std::string& name, double area, double ar_min = 0.25, double ar_max = 4.0) {
    Block b;
    b.name = name;
    b.area = area;
    b.width = std::sqrt(area);
    b.height = std::sqrt(area);
    b.ar_min = ar_min;
    b.ar_max = ar_max;
    b.type = BlockType::Soft;
    return b;
}

Block makeMacro(const std::string& name, double w, double h) {
    Block b;
    b.name = name;
    b.width = w;
    b.height = h;
    b.area = w * h;
    b.type = BlockType::Macro;
    return b;
}

Problem fourSoftProblem() {
    Problem p;
    p.outline_w = 1000.0;
    p.outline_h = 1000.0;
    p.alpha = 1.0;
    p.blocks = {makeSoft("A", 10000.0), makeSoft("B", 10000.0), makeSoft("C", 10000.0),
                makeSoft("D", 10000.0)};
    p.block_index = {{"A", 0}, {"B", 1}, {"C", 2}, {"D", 3}};
    return p;
}

void testShelfSeedDeterministicRowStructure() {
    Problem p = fourSoftProblem();
    // nrows=2, W=200: greedy least-loaded puts 2 blocks per row; each row's area sum is 20000, so
    // h_k = 100 -- feasible for every block (sqrt(10000)=100, band [50,200]), so every rect is
    // exactly 100x100, packed two per row, rows at y=0 and y=100.
    Placement a = shelfSeed(p, 2, 200.0);
    Placement b = shelfSeed(p, 2, 200.0);
    assert(a.rects.size() == 4 && b.rects.size() == 4);
    for (std::size_t i = 0; i < 4; ++i) {
        assert(a.rects[i].x == b.rects[i].x && a.rects[i].y == b.rects[i].y &&
               a.rects[i].w == b.rects[i].w && a.rects[i].h == b.rects[i].h);  // bitwise determinism
        assert(std::abs(a.rects[i].w - 100.0) < 1e-9);
        assert(std::abs(a.rects[i].h - 100.0) < 1e-9);
        assert(std::abs(a.rects[i].w * a.rects[i].h - 10000.0) < 1e-6);  // soft area preserved
    }
    // Two rows, two members each: y in {0, 100}, and within a row x in {0, 100} (abutting).
    int at_y0 = 0;
    int at_y100 = 0;
    for (std::size_t i = 0; i < 4; ++i) {
        if (std::abs(a.rects[i].y - 0.0) < 1e-9) ++at_y0;
        if (std::abs(a.rects[i].y - 100.0) < 1e-9) ++at_y100;
    }
    assert(at_y0 == 2 && at_y100 == 2);
    for (std::size_t i = 0; i < 4; ++i) {
        for (std::size_t j = i + 1; j < 4; ++j) {
            assert(overlapArea(a.rects[i], a.rects[j]) < 1e-9);  // shelf never self-overlaps here
        }
    }
    std::cout << "testShelfSeedDeterministicRowStructure ok\n";
}

void testShelfSeedFeasibilityClampAndHardDims() {
    Problem p;
    p.outline_w = 1000.0;
    p.outline_h = 1000.0;
    // Soft with tight band [0.5, 2.0] and area 10000: feasible h in [sqrt(10000/2)=70.7,
    // sqrt(10000/0.5)=141.4]. Big soft neighbour forces a much taller row than 141.4, so the small
    // block must clamp to h=141.4 (aspect 0.5) instead of matching the row height.
    p.blocks = {makeSoft("BIG", 250000.0), makeSoft("SMALL", 10000.0, 0.5, 2.0),
                makeMacro("HARD", 30.0, 40.0)};
    p.block_index = {{"BIG", 0}, {"SMALL", 1}, {"HARD", 2}};
    // nrows=1, W=500: single row, area sum 261200 -> h = 522.4; BIG clamps to its own band top
    // sqrt(250000/0.25)=1000 > 522.4 -> h=522.4 feasible for BIG (band [250,1000]); SMALL clamps
    // to 141.42; HARD keeps 30x40 exactly.
    Placement pl = shelfSeed(p, 1, 500.0);
    const double row_h = 261200.0 / 500.0;
    assert(std::abs(pl.rects[0].h - row_h) < 1e-9);                       // BIG matches row height
    assert(std::abs(pl.rects[1].h - std::sqrt(10000.0 / 0.5)) < 1e-9);    // SMALL clamped to band
    double small_aspect = pl.rects[1].w / pl.rects[1].h;
    assert(small_aspect >= 0.5 - 1e-9 && small_aspect <= 2.0 + 1e-9);
    assert(std::abs(pl.rects[1].w * pl.rects[1].h - 10000.0) < 1e-6);     // area exact
    assert(std::abs(pl.rects[2].w - 30.0) < 1e-12 && std::abs(pl.rects[2].h - 40.0) < 1e-12);
    std::cout << "testShelfSeedFeasibilityClampAndHardDims ok (row_h=" << row_h << ")\n";
}

void testStampSoftAspectClampsAndSkips() {
    Problem p;
    p.blocks = {makeSoft("S1", 10000.0, 0.5, 2.0), makeMacro("M", 10.0, 10.0),
                makeSoft("S2", 10000.0, 0.5, 2.0)};
    p.block_index = {{"S1", 0}, {"M", 1}, {"S2", 2}};
    Placement pl;
    pl.rects = {Rect{0.0, 0.0, 300.0, 33.4},   // aspect ~8.98 -> must clamp to 2.0
                Rect{0.0, 0.0, 10.0, 10.0},
                Rect{0.0, 0.0, 0.0, 0.0}};     // zero-sized placeholder -> skipped
    PlacementState state;
    state.soft_aspect = {1.0, 1.0, 1.0};
    stampSoftAspectFromPlacement(p, pl, state);
    assert(std::abs(state.soft_aspect[0] - 2.0) < 1e-12);  // clamped to ar_max
    assert(std::abs(state.soft_aspect[1] - 1.0) < 1e-12);  // non-soft untouched
    assert(std::abs(state.soft_aspect[2] - 1.0) < 1e-12);  // zero rect skipped
    std::cout << "testStampSoftAspectClampsAndSkips ok\n";
}

void testSeededHybridSmoke() {
    // Plumbing smoke: a shelf-seeded hybrid run on the 4-soft fixture returns a structurally
    // valid result (rects for every block, positive best value), and the nullptr path still works
    // identically to the historical 2-arg call (same function, default arg -- compile-level check).
    Problem p = fourSoftProblem();
    Placement shelf = shelfSeed(p, 2, 200.0);
    SearchOptions options;
    options.iters = 60;
    options.seed = 1;
    SearchResult seeded = runHybridPlacementSearch(p, options, &shelf);
    assert(seeded.best_placement.rects.size() == 4);
    assert(seeded.best_score.value > 0.0);
    SearchResult unseeded = runHybridPlacementSearch(p, options);
    assert(unseeded.best_placement.rects.size() == 4);
    std::cout << "testSeededHybridSmoke ok (seeded=" << seeded.best_score.value
              << ", unseeded=" << unseeded.best_score.value << ")\n";
}

}  // namespace

int main() {
    testShelfSeedDeterministicRowStructure();
    testShelfSeedFeasibilityClampAndHardDims();
    testStampSoftAspectClampsAndSkips();
    testSeededHybridSmoke();
    std::cout << "l4b (shelf seed + aspect stamp) regressions passed\n";
    return 0;
}
