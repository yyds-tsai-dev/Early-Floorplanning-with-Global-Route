#include <cassert>
#include <cmath>
#include <iostream>
#include <string>
#include <vector>

#include "../src/eval.hpp"
#include "../src/ft.hpp"
#include "../src/model.hpp"
#include "../src/route_cost.hpp"
#include "../src/routing.hpp"

// L1 (cheap-tier objective realignment, case5-design.md §2.4/§4-L1a/§4-L1b/§4-L1c,
// .superpowers/sdd/case5-l1-brief.md): Problem.cheap_proxy_v2 replaces cheapScore's v1 route-aware
// proxy (routeAwareProxyPenalty: FT-bbox loop + congestion term, both essentially noise on case5)
// AND the centre-to-centre WL proxy with scorer-unit surrogates -- L1a's aggregate FT term, L1b's
// boundary-gap WL proxy, L1c's cut-line-sweep FT/overflow estimator (eval.cpp's
// routeAwareProxyPenaltyV2). Tests below cover the brief's 4 pinned acceptance points:
//   1. v2-off reproduces the v1 formula exactly (guards the `if (problem.cheap_proxy_v2)` branch --
//      the two proxies give visibly different numbers on this fixture, so a branch that always took
//      v2 (or never took v1) would fail this test).
//   2. Cancellation: exactScore's FINAL value does not depend on cheap_proxy_v2 at all -- the v1/v2
//      proxy is added by cheapScore and subtracted by routeAndScoreAt with the identical (w,h) on
//      both flag settings, so the residual (legality + real routed WL/overflow/FT) is identical
//      either way (the exact tier is provably untouched by this lever, design invariant #1).
//   3. L1b: boundary-gap WL proxy is exactly 0 for abutting rects and exactly gap*nets otherwise.
//   4. L1c: demand exceeding a cut's channel capacity flows into est_ft_nets (and so the L1a FT
//      penalty) when a soft block covers the cut; with no soft coverage, it is true hard overflow.

namespace {
using namespace efgr;

Block makeBlock(const std::string& name, double w, double h, BlockType type) {
    Block b;
    b.name = name;
    b.width = w;
    b.height = h;
    b.area = w * h;
    b.type = type;
    return b;
}

// ---------------------------------------------------------------------------------------------
// 1. v2-off guards the branch: cheapScore(problem, placement) with cheap_proxy_v2 explicitly false
// equals the v1 formula hand-derived for a fixture with only the bundle's own source/target blocks
// (so v1's FT-bbox inner loop, which skips i==source/i==target, contributes nothing) and no
// abutment_credit (default false). A(0,0,100,100), B(300,0,100,100): centre-to-centre distance 300
// (all-x, dy=0), so v1 wire_proxy = 300*nets; v1 congestion narrow=min(a.h,b.h)=100,
// capacity=2500 >= nets, so congestion==0 and the FT loop is empty -- routeAwareProxyPenalty==0.
// ---------------------------------------------------------------------------------------------
void testV2OffMatchesV1FormulaGuardsTheBranch() {
    Problem p;
    p.outline_w = 400.0;
    p.outline_h = 100.0;
    p.alpha = 1.0;
    p.cheap_proxy_v2 = false;  // explicit -- this is the fact under test
    p.blocks = {makeBlock("A", 100.0, 100.0, BlockType::Macro),
                makeBlock("B", 100.0, 100.0, BlockType::Macro)};
    p.block_index = {{"A", 0}, {"B", 1}};
    Bundle bun;
    bun.source = 0;
    bun.target = 1;
    bun.nets = 500;
    p.bundles = {bun};

    Placement pl;
    pl.rects = {Rect{0.0, 0.0, 100.0, 100.0}, Rect{300.0, 0.0, 100.0, 100.0}};
    pl.aspect = {1.0, 1.0};

    Score s = cheapScore(p, pl);
    assert(std::abs(s.outline_area - 40000.0) < 1e-9);
    assert(std::abs(s.wirelength - 150000.0) < 1e-9);  // v1: manhattan(50,50; 350,50)=300 * 500 nets
    assert(std::abs(s.penalty - 0.0) < 1e-9);
    assert(std::abs(s.value - 190000.0) < 1e-9);

    // Sanity that v2 would have given a DIFFERENT wirelength on this same fixture (L1b's boundary
    // gap is 200, not v1's centre-distance 300) -- proves this test actually discriminates the
    // branch rather than passing by coincidence.
    Problem p2 = p;
    p2.cheap_proxy_v2 = true;
    Score s2 = cheapScore(p2, pl);
    assert(std::abs(s2.wirelength - 100000.0) < 1e-9);  // gap=200 * 500 nets
    assert(std::abs(s2.wirelength - s.wirelength) > 1.0);

    std::cout << "testV2OffMatchesV1FormulaGuardsTheBranch ok (v1 wl=" << s.wirelength
              << ", v2 wl=" << s2.wirelength << ")\n";
}

// ---------------------------------------------------------------------------------------------
// 2. Cancellation: exactScore's value/outline_area/wirelength/penalty are IDENTICAL for the same
// placement whether cheap_proxy_v2 is on or off. routeProblem never reads cheap_proxy_v2, so the
// routed RoutingResult is bit-identical either way; the only difference cheapScore's addend makes
// is exactly cancelled by routeAndScoreAt's subtraction (same (w,h), same flag) on both arms, so
// the surviving terms (outline_area, real routed WL, official overflow/FT penalties, legality) are
// the same. Reuses the proven-routable "two blocks + vertical channel gap" pattern from
// regression_p2_20260707.cpp's testVerticalOverflowIsAreaDenominatedByChannelHeight (open_routes==0
// there up to nets=3600; 500 here is comfortably under channel capacity too, so no corrective-grow
// path fires on either arm).
// ---------------------------------------------------------------------------------------------
void testCancellationExactScoreIndependentOfFlag() {
    Problem p;
    p.outline_w = 80.0;
    p.outline_h = 610.0 + 100.0 + 100.0;
    p.alpha = 1.0;
    p.blocks = {makeBlock("A", 80.0, 100.0, BlockType::Macro),
                makeBlock("B", 80.0, 100.0, BlockType::Macro)};
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

    Problem p_off = p;
    p_off.cheap_proxy_v2 = false;
    RoutingResult rr_off;
    Score s_off = exactScore(p_off, pl, &rr_off);
    assert(rr_off.open_routes == 0);

    Problem p_on = p;
    p_on.cheap_proxy_v2 = true;
    RoutingResult rr_on;
    Score s_on = exactScore(p_on, pl, &rr_on);
    assert(rr_on.open_routes == 0);

    assert(std::abs(s_off.value - s_on.value) < 1e-6);
    assert(std::abs(s_off.outline_area - s_on.outline_area) < 1e-9);
    assert(std::abs(s_off.wirelength - s_on.wirelength) < 1e-9);
    assert(std::abs(s_off.penalty - s_on.penalty) < 1e-6);
    assert(std::abs(rr_off.total_wirelength - rr_on.total_wirelength) < 1e-9);

    std::cout << "testCancellationExactScoreIndependentOfFlag ok (value=" << s_off.value << ")\n";
}

// ---------------------------------------------------------------------------------------------
// 3. L1b zero-at-abutment / gap*nets: two directly abutting rects give wire term 0 (matching
// route_geometry.hpp:93-94's "a path of <2 nodes has 0 wirelength"); separated by a gap gives
// exactly gap*nets. cheap_proxy_v2 on; no bundles beyond the pair under test, so score.wirelength
// IS the wire_proxy contribution of that single bundle.
// ---------------------------------------------------------------------------------------------
void testL1bZeroAtAbutmentAndGapTimesNets() {
    Problem p;
    p.outline_w = 500.0;
    p.outline_h = 100.0;
    p.alpha = 1.0;
    p.cheap_proxy_v2 = true;
    p.blocks = {makeBlock("A", 100.0, 100.0, BlockType::Macro),
                makeBlock("B", 100.0, 100.0, BlockType::Macro)};
    p.block_index = {{"A", 0}, {"B", 1}};
    Bundle bun;
    bun.source = 0;
    bun.target = 1;
    bun.nets = 10;
    p.bundles = {bun};

    // Abutting: A=[0,100), B=[100,200) -- shared edge, zero gap on both axes.
    Placement abut;
    abut.rects = {Rect{0.0, 0.0, 100.0, 100.0}, Rect{100.0, 0.0, 100.0, 100.0}};
    abut.aspect = {1.0, 1.0};
    Score s_abut = cheapScore(p, abut);
    assert(std::abs(s_abut.wirelength - 0.0) < 1e-9);

    // Separated: B moved to x=150 -- gap=150-100=50 on x, 0 on y. wire term = (50+0)*10 = 500.
    Placement gap;
    gap.rects = {Rect{0.0, 0.0, 100.0, 100.0}, Rect{150.0, 0.0, 100.0, 100.0}};
    gap.aspect = {1.0, 1.0};
    Score s_gap = cheapScore(p, gap);
    assert(std::abs(s_gap.wirelength - 500.0) < 1e-9);

    std::cout << "testL1bZeroAtAbutmentAndGapTimesNets ok (abut wl=" << s_abut.wirelength
              << ", gap wl=" << s_gap.wirelength << ")\n";
}

// ---------------------------------------------------------------------------------------------
// 4. L1c: a single vertical cut (x=100) between A=[0,50) and B=[150,200) carries bundle demand
// (nets=1000) that exceeds its channel capacity. With a soft block S=[50,150) covering the cut, the
// excess is diverted into est_ft_nets[S] instead of overflow -- L1a's aggregate FT term then prices
// it, so penalty > 0 iff est_ft_area_sum (independently recomputed here via ft.hpp, same as
// eval.cpp's routeAwareProxyPenaltyV2) exceeds w*h. Without S covering the cut (2-block variant),
// the SAME excess has nowhere to feedthrough and becomes true hard overflow, priced via
// officialOverflowPenalty -- independently recomputed here too. outline_h=10 keeps channel capacity
// (25*free_h) small enough that nets=1000 overflows it either way.
// ---------------------------------------------------------------------------------------------
void testL1cSoftCoverageDivertsToFtNoSoftCoverageIsHardOverflow() {
    // -- (a) soft coverage: A, B, and soft S spanning the whole gap between them. --
    {
        Problem p;
        p.outline_w = 200.0;
        p.outline_h = 10.0;
        p.alpha = 1.0;
        p.cheap_proxy_v2 = true;
        p.blocks = {makeBlock("A", 50.0, 10.0, BlockType::Macro),
                    makeBlock("B", 50.0, 10.0, BlockType::Macro),
                    makeBlock("S", 100.0, 10.0, BlockType::Soft)};
        p.blocks[2].ft_rates = {0.2, 0.4, 0.8, 1.0};
        p.block_index = {{"A", 0}, {"B", 1}, {"S", 2}};
        Bundle bun;
        bun.source = 0;
        bun.target = 1;
        bun.nets = 1000;
        p.bundles = {bun};

        Placement pl;
        pl.rects = {Rect{0.0, 0.0, 50.0, 10.0}, Rect{150.0, 0.0, 50.0, 10.0},
                    Rect{50.0, 0.0, 100.0, 10.0}};
        pl.aspect = {1.0, 1.0, 1.0};

        Score s = cheapScore(p, pl);

        // Independent recompute of the L1a/L1c formula for THIS fixture: the sole cut (x=100) is
        // fully covered by S (free_h=0 -> cap=0 -> cap_total=0 -> hard_overflow=0 -> overflow
        // penalty 0), and ALL 1000 demand nets divert to S (only covering soft, full weight).
        double base_s = p.blocks[2].area;  // ft_base_area unset -> falls back to area
        double required_s = ftRequiredArea(base_s, ftExtend(1000.0, ftTierRate(p.blocks[2], 1000.0)));
        double est_ft_area_sum = p.blocks[0].area + p.blocks[1].area + required_s;
        double w_h = p.outline_w * p.outline_h;
        double expected_penalty = officialOverflowPenalty(0.0, w_h) + kBetaFt * std::max(0.0, est_ft_area_sum - w_h);

        assert(est_ft_area_sum > w_h);  // fixture is built so the FT term actually bites
        assert(expected_penalty > 0.0);
        assert(std::abs(s.penalty - expected_penalty) < 1e-6);

        std::cout << "testL1c soft-coverage ok (penalty=" << s.penalty
                  << ", expected=" << expected_penalty << ", est_ft_area_sum=" << est_ft_area_sum
                  << ", w*h=" << w_h << ")\n";
    }

    // -- (b) no soft coverage: only A and B, same gap and demand -- excess is hard overflow. --
    {
        Problem p;
        p.outline_w = 200.0;
        p.outline_h = 10.0;
        p.alpha = 1.0;
        p.cheap_proxy_v2 = true;
        p.blocks = {makeBlock("A", 50.0, 10.0, BlockType::Macro),
                    makeBlock("B", 50.0, 10.0, BlockType::Macro)};
        p.block_index = {{"A", 0}, {"B", 1}};
        Bundle bun;
        bun.source = 0;
        bun.target = 1;
        bun.nets = 1000;
        p.bundles = {bun};

        Placement pl;
        pl.rects = {Rect{0.0, 0.0, 50.0, 10.0}, Rect{150.0, 0.0, 50.0, 10.0}};
        pl.aspect = {1.0, 1.0};

        Score s = cheapScore(p, pl);

        // Independent recompute: cut at x=100 is uncovered -> free_h=10, cap=25*10=250,
        // demand=1000, excess=750 -> ALL hard_overflow (no soft to divert into); FT term is 0 (no
        // soft blocks at all).
        double cap = 25.0 * 10.0;
        double hard_overflow = std::max(0.0, 1000.0 - cap);
        double w_h = p.outline_w * p.outline_h;
        double rate = hard_overflow / std::max(1.0, cap);
        double expected_penalty = officialOverflowPenalty(rate, w_h);  // FT term 0: no soft blocks

        assert(hard_overflow > 0.0);
        assert(expected_penalty > 0.0);
        assert(std::abs(s.penalty - expected_penalty) < 1e-6);

        std::cout << "testL1c no-soft-coverage ok (penalty=" << s.penalty
                  << ", expected=" << expected_penalty << ", hard_overflow=" << hard_overflow << ")\n";
    }
}

}  // namespace

int main() {
    testV2OffMatchesV1FormulaGuardsTheBranch();
    testCancellationExactScoreIndependentOfFlag();
    testL1bZeroAtAbutmentAndGapTimesNets();
    testL1cSoftCoverageDivertsToFtNoSoftCoverageIsHardOverflow();
    std::cout << "cheap_proxy_v2 (L1) regressions passed\n";
    return 0;
}
