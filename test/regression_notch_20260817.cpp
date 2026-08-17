// D4/A5 pack (notch/pocket whitespace penalty, .superpowers/sdd/levers-batch2-briefs-20260817.md
// Brief 3): regression coverage for eval.cpp's notchPocketPenalty. That function has internal
// linkage (anonymous namespace, mirroring routeAwareProxyPenalty/V2's own precedent), so every test
// here goes through the public cheapScore/exactScore surface -- (b)-(g) isolate the penalty as a
// PURE DELTA: cheapScore(problem_with_notch_off) vs cheapScore(problem_with_notch_on) on the SAME
// fixture differ ONLY by the notchPocketPenalty addend (an `if`, nothing else in cheapScore reads
// problem.notch_penalty), so `on.penalty - off.penalty` recovers the raw function value exactly.
// Same minimal link line as run_v5_tail_pricing_regressions.sh (eval.cpp fixtures need
// placement_engine.cpp + routing.cpp transitively) -- see run_notch_regressions.sh.
#include <cassert>
#include <cmath>
#include <iostream>
#include <string>
#include <vector>

#include "../src/eval.hpp"
#include "../src/model.hpp"

namespace {
using namespace efgr;

constexpr double kTol = 1e-6;
constexpr double kKnownNotchWeight = 0.25;  // pinned knob value (eval.cpp's kNotchWeight)

Block makeBlock(const std::string& name, double x, double y, double w, double h) {
    Block b;
    b.name = name;
    b.width = w;
    b.height = h;
    b.area = w * h;
    b.type = BlockType::Macro;
    (void)x;
    (void)y;
    return b;
}

// Isolates notchPocketPenalty's own contribution: cheapScore differs ONLY by the `if
// (problem.notch_penalty) score.penalty += notchPocketPenalty(...)` addend (same w,h resolved
// either way, since notch_penalty never touches the w/h resolution above it), so this delta IS the
// raw function value on (placement, w=pl.outline_w, h=pl.outline_h).
double notchDelta(const Problem& base, const Placement& pl) {
    Problem p_off = base;
    p_off.notch_penalty = false;
    Problem p_on = base;
    p_on.notch_penalty = true;
    Score s_off = cheapScore(p_off, pl);
    Score s_on = cheapScore(p_on, pl);
    return s_on.penalty - s_off.penalty;
}

Problem makeBaseProblem(double outline_w, double outline_h) {
    Problem p;
    p.outline_w = outline_w;
    p.outline_h = outline_h;
    p.alpha = 1.0;
    return p;
}

// ---- (a) CANCELLATION: exactScore bit-identical flag on/off, crossed with cheap_proxy_v2 ----------

// Reuses the routable "two blocks + vertical channel gap" pattern regression_cheap_proxy_v2.cpp's
// own cancellation test established (proven routable, open_routes==0, no corrective-grow path).
void testCancellationExactScoreIndependentOfNotchFlag() {
    Problem p = makeBaseProblem(80.0, 610.0 + 100.0 + 100.0);
    p.blocks = {makeBlock("A", 0, 0, 80.0, 100.0), makeBlock("B", 0, 710.0, 80.0, 100.0)};
    p.block_index = {{"A", 0}, {"B", 1}};
    Bundle bun;
    bun.source = 0;
    bun.target = 1;
    bun.nets = 500;
    p.bundles = {bun};

    Placement pl;
    pl.rects = {Rect{0.0, 0.0, 80.0, 100.0}, Rect{0.0, 710.0, 80.0, 100.0}};
    pl.aspect = {1.0, 1.0};
    pl.outline_w = p.outline_w;
    pl.outline_h = p.outline_h;

    for (bool v2 : {false, true}) {
        Problem p_off = p;
        p_off.cheap_proxy_v2 = v2;
        p_off.notch_penalty = false;
        RoutingResult rr_off;
        Score s_off = exactScore(p_off, pl, &rr_off);
        assert(rr_off.open_routes == 0);

        Problem p_on = p;
        p_on.cheap_proxy_v2 = v2;
        p_on.notch_penalty = true;
        RoutingResult rr_on;
        Score s_on = exactScore(p_on, pl, &rr_on);
        assert(rr_on.open_routes == 0);

        assert(std::abs(s_off.value - s_on.value) < 1e-6);
        assert(std::abs(s_off.outline_area - s_on.outline_area) < 1e-9);
        assert(std::abs(s_off.wirelength - s_on.wirelength) < 1e-9);
        assert(std::abs(s_off.penalty - s_on.penalty) < 1e-6);
        assert(std::abs(rr_off.total_wirelength - rr_on.total_wirelength) < 1e-9);
    }
    std::cout << "testCancellationExactScoreIndependentOfNotchFlag ok\n";
}

// ---- (b) abutting blocks filling the outline => 0 --------------------------------------------------

void testAbuttingBlocksFillingOutlineIsZero() {
    Problem p = makeBaseProblem(200.0, 100.0);
    p.blocks = {makeBlock("A", 0, 0, 100.0, 100.0), makeBlock("B", 100, 0, 100.0, 100.0)};

    Placement pl;
    pl.rects = {Rect{0.0, 0.0, 100.0, 100.0}, Rect{100.0, 0.0, 100.0, 100.0}};
    pl.aspect = {1.0, 1.0};
    pl.outline_w = p.outline_w;
    pl.outline_h = p.outline_h;

    double delta = notchDelta(p, pl);
    assert(std::abs(delta - 0.0) < kTol);
    std::cout << "testAbuttingBlocksFillingOutlineIsZero ok\n";
}

// ---- (c) hand-built 100x100 pocket in 2000x2000 => exactly kNotchWeight*sqrt(4e6*1e4) -------------

// Four blocks frame a single isolated 100x100 hole at the center: L/R cover the full-height left/
// right bands, T/Bt cover the top/bottom slivers directly above/below the hole -- see the module
// doc comment for the coordinate derivation (grid cells: col{0,1,2} x row{0,1,2}, hole = (col1,row1)).
void testHandBuiltPocketExactValue() {
    Problem p = makeBaseProblem(2000.0, 2000.0);
    p.blocks = {
        makeBlock("L", 0, 0, 950.0, 2000.0),
        makeBlock("R", 1050, 0, 950.0, 2000.0),
        makeBlock("T", 950, 0, 100.0, 950.0),
        makeBlock("Bt", 950, 1050, 100.0, 950.0),
    };
    Placement pl;
    pl.rects = {
        Rect{0.0, 0.0, 950.0, 2000.0},
        Rect{1050.0, 0.0, 950.0, 2000.0},
        Rect{950.0, 0.0, 100.0, 950.0},
        Rect{950.0, 1050.0, 100.0, 950.0},
    };
    pl.aspect = {1.0, 1.0, 1.0, 1.0};
    pl.outline_w = p.outline_w;
    pl.outline_h = p.outline_h;

    double delta = notchDelta(p, pl);
    double expected = kKnownNotchWeight * std::sqrt(2000.0 * 2000.0 * 100.0 * 100.0);
    assert(std::abs(expected - 50000.0) < kTol);  // sanity: matches the brief's own pinned number
    assert(std::abs(delta - expected) < 1e-3);
    std::cout << "testHandBuiltPocketExactValue ok (delta=" << delta << " expected=" << expected << ")\n";
}

// ---- (d) 10x1500 channel (in a 2000x2000 outline) => 0 ---------------------------------------------

// Top/Bottom bands sandwich a middle band split into Left-middle/Right-middle by a 10-wide gap
// spanning the full 1500-tall middle band -- long and thin (bbox_h=1500 fails h/10=200), matching a
// legitimate channel shape rather than a notch.
void testLongThinChannelIsZero() {
    Problem p = makeBaseProblem(2000.0, 2000.0);
    p.blocks = {
        makeBlock("Top", 0, 1750, 2000.0, 250.0),
        makeBlock("Bottom", 0, 0, 2000.0, 250.0),
        makeBlock("LeftMid", 0, 250, 950.0, 1500.0),
        makeBlock("RightMid", 960, 250, 1040.0, 1500.0),
    };
    Placement pl;
    pl.rects = {
        Rect{0.0, 1750.0, 2000.0, 250.0},
        Rect{0.0, 0.0, 2000.0, 250.0},
        Rect{0.0, 250.0, 950.0, 1500.0},
        Rect{960.0, 250.0, 1040.0, 1500.0},
    };
    pl.aspect = {1.0, 1.0, 1.0, 1.0};
    pl.outline_w = p.outline_w;
    pl.outline_h = p.outline_h;

    double delta = notchDelta(p, pl);
    assert(std::abs(delta - 0.0) < kTol);
    std::cout << "testLongThinChannelIsZero ok\n";
}

// ---- (e1) two separate pockets => sum ---------------------------------------------------------------

// Two independent copies of (c)'s 4-block frame, side by side in a 4000x2000 outline (holes at
// x=[950,1050] and x=[2950,3050], both y=[950,1050]) -- each hole is fully enclosed by its own
// frame, so they are necessarily two disjoint 4-connected components regardless of what else is (or
// isn't) between the two frames.
void testTwoPocketsSum() {
    Problem p = makeBaseProblem(4000.0, 2000.0);
    p.blocks = {
        makeBlock("L1", 0, 0, 950.0, 2000.0),      makeBlock("R1", 1050, 0, 950.0, 2000.0),
        makeBlock("T1", 950, 0, 100.0, 950.0),     makeBlock("B1", 950, 1050, 100.0, 950.0),
        makeBlock("L2", 2000, 0, 950.0, 2000.0),   makeBlock("R2", 3050, 0, 950.0, 2000.0),
        makeBlock("T2", 2950, 0, 100.0, 950.0),    makeBlock("B2", 2950, 1050, 100.0, 950.0),
    };
    Placement pl;
    pl.rects = {
        Rect{0.0, 0.0, 950.0, 2000.0},     Rect{1050.0, 0.0, 950.0, 2000.0},
        Rect{950.0, 0.0, 100.0, 950.0},    Rect{950.0, 1050.0, 100.0, 950.0},
        Rect{2000.0, 0.0, 950.0, 2000.0},  Rect{3050.0, 0.0, 950.0, 2000.0},
        Rect{2950.0, 0.0, 100.0, 950.0},   Rect{2950.0, 1050.0, 100.0, 950.0},
    };
    pl.aspect.assign(8, 1.0);
    pl.outline_w = p.outline_w;
    pl.outline_h = p.outline_h;

    double delta = notchDelta(p, pl);
    double expected_each = kKnownNotchWeight * std::sqrt(4000.0 * 2000.0 * 100.0 * 100.0);
    assert(std::abs(delta - 2.0 * expected_each) < 1e-2);
    std::cout << "testTwoPocketsSum ok (delta=" << delta << " expected=" << 2.0 * expected_each << ")\n";
}

// ---- (e2) L-shaped empty region => ONE component by bbox (merged bbox fails the notch test) -------

// Grid cells: col{0,1,2,3} = x-edges {0,950,1050,1150,2000}, row{0,1,2,3} = y-edges
// {0,950,1050,1150,2000} (same 950/100/100/850 spacing on both axes). Empty cells (col1,row1),
// (col1,row2), (col2,row2) form an L (a vertical leg + a horizontal leg sharing the corner cell) --
// 4-connected, so flood fill must treat them as ONE component with bbox x=[950,1150] (200 wide),
// y=[950,1150] (200 tall). 200 is NOT < w/10=200 (strict <) -- the MERGED bbox correctly fails the
// notch test (long/wide, not a small pocket) => delta 0. If the three cells were (incorrectly)
// scored as three SEPARATE 100x100 notches instead, each would individually pass the size test and
// the delta would be strongly positive (3 * kNotchWeight*sqrt(2000*2000*100*100) = 150000) -- this
// test's 0 vs 150000 contrast is exactly what pins "connected empty cells are ONE component, not
// several," i.e. the flood fill (not a per-cell notch test) is what the algorithm actually runs.
void testLShapedRegionIsOneComponent() {
    Problem p = makeBaseProblem(2000.0, 2000.0);
    // Row0 (y=[0,950]): fully occupied.
    // Row1 (y=[950,1050]): occupied except col1 -- two rects (col0, cols2-3).
    // Row2 (y=[1050,1150]): occupied except col1,col2 -- two rects (col0, col3).
    // Row3 (y=[1150,2000]): fully occupied.
    p.blocks = {
        makeBlock("Row0", 0, 0, 2000.0, 950.0),
        makeBlock("Row1a", 0, 950, 950.0, 100.0),
        makeBlock("Row1b", 1050, 950, 950.0, 100.0),
        makeBlock("Row2a", 0, 1050, 950.0, 100.0),
        makeBlock("Row2b", 1150, 1050, 850.0, 100.0),
        makeBlock("Row3", 0, 1150, 2000.0, 850.0),
    };
    Placement pl;
    pl.rects = {
        Rect{0.0, 0.0, 2000.0, 950.0},
        Rect{0.0, 950.0, 950.0, 100.0},
        Rect{1050.0, 950.0, 950.0, 100.0},
        Rect{0.0, 1050.0, 950.0, 100.0},
        Rect{1150.0, 1050.0, 850.0, 100.0},
        Rect{0.0, 1150.0, 2000.0, 850.0},
    };
    pl.aspect.assign(6, 1.0);
    pl.outline_w = p.outline_w;
    pl.outline_h = p.outline_h;

    double delta = notchDelta(p, pl);
    assert(std::abs(delta - 0.0) < kTol);  // merged L bbox (200x200) fails the notch test
    std::cout << "testLShapedRegionIsOneComponent ok (delta=" << delta << ")\n";
}

// ---- (f) degenerate / overlapping input => no crash ------------------------------------------------

void testNoBlocksNoCrash() {
    Problem p = makeBaseProblem(500.0, 500.0);
    Placement pl;
    pl.outline_w = p.outline_w;
    pl.outline_h = p.outline_h;
    double delta = notchDelta(p, pl);
    assert(std::isfinite(delta));
    // Whole outline is one empty component with bbox == outline itself (500x500), which never
    // satisfies bbox < outline/10 -- not a notch, just open space.
    assert(std::abs(delta - 0.0) < kTol);
    std::cout << "testNoBlocksNoCrash ok\n";
}

void testOverlappingRectsNoCrash() {
    Problem p = makeBaseProblem(500.0, 500.0);
    p.blocks = {makeBlock("A", 0, 0, 200.0, 200.0), makeBlock("B", 100, 100, 200.0, 200.0)};
    Placement pl;
    pl.rects = {Rect{0.0, 0.0, 200.0, 200.0}, Rect{100.0, 100.0, 200.0, 200.0}};
    pl.aspect = {1.0, 1.0};
    pl.outline_w = p.outline_w;
    pl.outline_h = p.outline_h;
    double delta1 = notchDelta(p, pl);
    double delta2 = notchDelta(p, pl);
    assert(std::isfinite(delta1));
    assert(delta1 >= 0.0);
    assert(delta1 == delta2);  // also covers (g) determinism on a non-trivial fixture
    std::cout << "testOverlappingRectsNoCrash ok (delta=" << delta1 << ")\n";
}

// ---- (g) determinism (fixture c, repeated) ----------------------------------------------------------

void testDeterminism() {
    Problem p = makeBaseProblem(2000.0, 2000.0);
    p.blocks = {
        makeBlock("L", 0, 0, 950.0, 2000.0),
        makeBlock("R", 1050, 0, 950.0, 2000.0),
        makeBlock("T", 950, 0, 100.0, 950.0),
        makeBlock("Bt", 950, 1050, 100.0, 950.0),
    };
    Placement pl;
    pl.rects = {
        Rect{0.0, 0.0, 950.0, 2000.0},
        Rect{1050.0, 0.0, 950.0, 2000.0},
        Rect{950.0, 0.0, 100.0, 950.0},
        Rect{950.0, 1050.0, 100.0, 950.0},
    };
    pl.aspect = {1.0, 1.0, 1.0, 1.0};
    pl.outline_w = p.outline_w;
    pl.outline_h = p.outline_h;

    double d1 = notchDelta(p, pl);
    double d2 = notchDelta(p, pl);
    assert(d1 == d2);
    std::cout << "testDeterminism ok\n";
}

}  // namespace

int main() {
    testCancellationExactScoreIndependentOfNotchFlag();
    testAbuttingBlocksFillingOutlineIsZero();
    testHandBuiltPocketExactValue();
    testLongThinChannelIsZero();
    testTwoPocketsSum();
    testLShapedRegionIsOneComponent();
    testNoBlocksNoCrash();
    testOverlappingRectsNoCrash();
    testDeterminism();
    std::cout << "notch/pocket whitespace penalty (D4/A5) regressions passed\n";
    return 0;
}
