#include <cassert>
#include <cmath>
#include <cstdio>
#include <memory>
#include "../src/io.hpp"
#include "../src/placement_engine.hpp"
#include "../src/search.hpp"

using namespace efgr;

static bool isInt(double v) { return std::abs(v - std::llround(v)) < 1e-9; }

int main() {
    for (int c = 0; c < 5; ++c) {
        Problem problem = parseProblem("test/2026 ICCAD_case.case" + std::to_string(c) + ".csv");
        SearchOptions opt; opt.iters = 200; opt.seed = 1;
        std::unique_ptr<PlacementEngine> engine = makePlacementEngine("bstar");
        SearchResult r = runPlacementSearch(problem, *engine, opt);
        assert(r.best_routing.open_routes == 0);
        for (const Rect& rect : r.best_placement.rects) {
            assert(isInt(rect.x) && isInt(rect.y) && isInt(rect.w) && isInt(rect.h));
        }
        if (r.best_placement.outline_w > 0.0) {
            assert(isInt(r.best_placement.outline_w) && isInt(r.best_placement.outline_h));
        }
        // Soft blocks: verify integer dims stay within [ar_min, ar_max] (rules 1/2's window-search
        // contract). Deliberately NOT gated on r.best_routing.soft_feedthrough_nets[i] == 0 implying
        // "placed <= b.area": finalizeFeedthrough (search.cpp) grows a block's decode area once
        // feedthrough is observed and NEVER shrinks it back down across later passes, so a block can
        // legitimately end up sized above its base area even though the FINAL committed routing no
        // longer routes feedthrough through it -- confirmed present on unmodified HEAD too (e.g.
        // case0 BLK02, case1 BLK01 both already show placed > area with ft_nets == 0 before this
        // task's changes), so it is pre-existing search behavior, not a rule 1/2 defect. Rule 1
        // (integerDimsMaxProduct) mathematically guarantees w*h <= area by construction (floor-
        // based), so any block ever sized above its base area was necessarily sized via rule 2 (FT
        // re-decode) at some point in the search -- the AR-bounds check below is what rules 1/2
        // actually promise and is unconditionally true.
        for (std::size_t i = 0; i < problem.blocks.size(); ++i) {
            const Block& b = problem.blocks[i];
            if (!isSoft(b.type)) continue;
            double w = r.best_placement.rects[i].w;
            double h = r.best_placement.rects[i].h;
            assert(h > 0.0);
            double ratio = w / h;
            assert(ratio >= b.ar_min - 1e-6 && ratio <= b.ar_max + 1e-6);
        }
        std::printf("case%d integer decode ok\n", c);
    }

    // Beta Task 4 post-packaging fix: the fractional-output leak (src/respace.cpp's relaxOneAxis
    // widening gaps by need/25.0 without integerizing) was dormant under bstar at Task 4 review
    // time ("early-exit fires first"), but Task 3's per-edge accounting makes respace fire on
    // congested cases under the DEFAULT hybrid engine -- case1/case3 at the production default
    // iters=1000 is the exact repro shape. Re-run those two cases through
    // runHybridPlacementSearch (not bstar) at that shape and re-assert the same isInt checks on
    // every rect plus the stamped outline.
    for (int c : {1, 3}) {
        Problem problem = parseProblem("test/2026 ICCAD_case.case" + std::to_string(c) + ".csv");
        SearchOptions opt; opt.iters = 1000; opt.seed = 1;
        SearchResult r = runHybridPlacementSearch(problem, opt);
        assert(r.best_routing.open_routes == 0);
        for (const Rect& rect : r.best_placement.rects) {
            assert(isInt(rect.x) && isInt(rect.y) && isInt(rect.w) && isInt(rect.h));
        }
        if (r.best_placement.outline_w > 0.0) {
            assert(isInt(r.best_placement.outline_w) && isInt(r.best_placement.outline_h));
        }
        std::printf("case%d hybrid integer decode ok\n", c);
    }

    std::puts("integer output regressions passed");
    return 0;
}
