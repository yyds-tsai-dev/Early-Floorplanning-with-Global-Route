#include <cassert>
#include <cmath>
#include <cstddef>
#include <iostream>
#include <string>

#include "../src/eval.hpp"
#include "../src/ft.hpp"
#include "../src/model.hpp"
#include "../src/route_cost.hpp"
#include "../src/routing.hpp"
#include "../src/search.hpp"

// F-FT (Task F-FT #6, docs/research/2026-07-28-alpha-v5-cost-recalculation-analysis.md §6, Codex
// finding #1): the official V5 FT penalty is an AGGREGATE slack term --
// kBetaFt*max(0, ft_area_sum-OutlineArea) -- but the Dijkstra official-pricing branch was still
// pricing FT hops per-block (kBetaFt*ftMismatchDelta), so an FT shortcut that is officially FREE
// (the aggregate stays under the outline) could still be avoided because its OWN block's mismatch
// looked expensive. This flag-gated "FT-aggressive" variant (Problem.ft_pricing_aggressive,
// model.hpp) prices FT hops at the exact marginal of the real aggregate term instead, following the
// same flag-gated-variant pattern as B5b (abutment_credit) / B5c (move_portfolio_v2).
//
// Tests below cover: (i) the 10-entry variant registry shape (8 historical + 2 F-FT legs); (ii)
// ftRequiredDelta's pure math (route_cost.hpp) against ftMismatchDelta; (iii) the aggressive
// pricing formula's max(0,.) hinge arithmetic in its three regimes; (iv) an end-to-end behavior
// fixture (adapted from regression_p2_20260707.cpp's channel+soft-block-bypass geometry) where
// aggressive pricing finds an officially-free FT shortcut conservative pricing avoids; (v) the
// running rr.ft_area_sum aggregate (accrued incrementally by commitRoutePath, routing.cpp) staying
// consistent with an independent recompute using computeOverflowAndMismatch's own formula.

namespace {
using namespace efgr;

Block makeChannelBlock(const std::string& name, double w, double h) {
    Block b;
    b.name = name;
    b.width = w;
    b.height = h;
    b.area = w * h;
    b.type = BlockType::Macro;
    return b;
}

// ---------------------------------------------------------------------------------------------
// (i) registry shape: defaultVariants() has 10 entries; entries 0-7 equal the historical 8 (now
// with a trailing ft_pricing_aggressive=false); entries 8-9 are F-FT's 2 new legs
// (official, v2/v1-admission, credit-on, moves-on, ftagg-on), admission-v2-then-v1 order, mirroring
// B5b/B5c's own admission split.
// ---------------------------------------------------------------------------------------------
void testDefaultVariantsRegistryShape() {
    std::vector<SearchVariant> variants = defaultVariants();
    assert(variants.size() == 10);

    // Historical 8 (search.cpp, pre-F-FT), byte-identical on their first 4 fields; all 8 must keep
    // ft_pricing_aggressive == false (bit-exact with pre-rung history).
    struct Legacy4 {
        bool pricing_official;
        bool elite_admission_v2;
        bool abutment_credit;
        bool move_portfolio_v2;
    };
    std::vector<Legacy4> historical = {
        {true, false, false, false},   {false, false, false, false}, {true, true, false, false},
        {false, true, false, false},   {true, true, true, false},    {true, false, true, false},
        {true, true, true, true},      {true, false, true, true},
    };
    for (std::size_t i = 0; i < historical.size(); ++i) {
        assert(variants[i].pricing_official == historical[i].pricing_official);
        assert(variants[i].elite_admission_v2 == historical[i].elite_admission_v2);
        assert(variants[i].abutment_credit == historical[i].abutment_credit);
        assert(variants[i].move_portfolio_v2 == historical[i].move_portfolio_v2);
        assert(variants[i].ft_pricing_aggressive == false);
    }

    // F-FT's 2 new legs: official pricing, admission-v2 then admission-v1, both credit-on/moves-on,
    // both ftagg-on -- stacked on top of B5c's own 2 legs (variants[6]/[7] above), per the task's
    // specified order.
    assert(variants[8].pricing_official == true);
    assert(variants[8].elite_admission_v2 == true);
    assert(variants[8].abutment_credit == true);
    assert(variants[8].move_portfolio_v2 == true);
    assert(variants[8].ft_pricing_aggressive == true);

    assert(variants[9].pricing_official == true);
    assert(variants[9].elite_admission_v2 == false);
    assert(variants[9].abutment_credit == true);
    assert(variants[9].move_portfolio_v2 == true);
    assert(variants[9].ft_pricing_aggressive == true);

    std::cout << "testDefaultVariantsRegistryShape ok\n";
}

// ---------------------------------------------------------------------------------------------
// (ii) ftRequiredDelta pure math (route_cost.hpp). Same synthetic soft Block pattern as
// regression_p2_20260707.cpp's testSoftFtMarginalCostPricesTierJump / FreeWhenPlacedAreaGenerous.
// ---------------------------------------------------------------------------------------------

// delta == ftMismatchDelta when placed_area <= base (so required_before/after both exceed
// placed_area and ftMismatchDelta's max(0,.) clamp never activates) -- and delta > 0 across the
// 0.2->0.4 tier jump at acc=2900/n=200 (same worked example as testSoftFtMarginalCostPricesTierJump).
void testFtRequiredDeltaMatchesMismatchDeltaWhenClampInactive() {
    Block b;
    b.name = "BLK";
    b.ft_base_area = 372949.0;
    b.area = 372949.0;
    b.type = BlockType::Soft;
    b.ft_rates = {0.2, 0.4, 0.8, 1.0};

    double acc = 2900.0;
    double n = 200.0;
    assert(std::abs(ftTierRate(b, acc) - 0.2) < kEps);
    assert(std::abs(ftTierRate(b, acc + n) - 0.4) < kEps);

    double placed_area = b.ft_base_area;  // == base -- required_before/after both exceed this (extend>0)
    double delta = ftRequiredDelta(b, acc, n);
    double mismatch_delta = ftMismatchDelta(b, acc, n, placed_area);
    assert(delta > 0.0);
    assert(std::abs(delta - mismatch_delta) < 1e-6);
    std::cout << "testFtRequiredDeltaMatchesMismatchDeltaWhenClampInactive ok (delta=" << delta << ")\n";
}

// ftRequiredDelta deliberately has NO placed-area clamp (route_cost.hpp doc comment) -- this is
// exactly why F-FT needs it: ftMismatchDelta can be masked to 0 by a generous placed_area even
// though the block's TRUE required-area footprint keeps growing, which is what the official V5
// aggregate (RoutingResult.ft_area_sum) actually sums. Reuses
// testSoftFtMarginalCostFreeWhenPlacedAreaGenerous's fixture/numbers (regression_p2_20260707.cpp).
void testFtRequiredDeltaIgnoresPlacedAreaClamp() {
    Block b;
    b.name = "BLK";
    b.ft_base_area = 372949.0;
    b.area = 372949.0;
    b.type = BlockType::Soft;
    b.ft_rates = {0.2, 0.4, 0.8, 1.0};

    double acc = 2900.0;
    double n = 200.0;
    double required_after = ftRequiredArea(b.ft_base_area, ftExtend(acc + n, ftTierRate(b, acc + n)));
    double placed_area = required_after + 10000.0;  // generous -- masks ftMismatchDelta to 0
    double mismatch_delta = ftMismatchDelta(b, acc, n, placed_area);
    assert(mismatch_delta == 0.0);

    double delta = ftRequiredDelta(b, acc, n);
    assert(delta > 0.0);  // unaware of placed_area -- still charges the true required-area growth
    std::cout << "testFtRequiredDeltaIgnoresPlacedAreaClamp ok (delta=" << delta << ", masked mismatch=0)\n";
}

// ---------------------------------------------------------------------------------------------
// (iii) aggressive pricing formula's max(0,.) hinge arithmetic in isolation (no routeProblem/
// Dijkstra involved) -- the three regimes routing.cpp's ft_pricing_aggressive branch
// (kBetaFt*(max(0,S+dreq-OA)-max(0,S-OA))) can land in: S=ft_area_sum, OA=outline_area,
// dreq=ftRequiredDelta for this hop.
// ---------------------------------------------------------------------------------------------
void testAggressivePricingHingeArithmeticThreeRegimes() {
    double OA = 10000.0;

    // Regime 1: full slack -- S<OA and S+dreq<=OA -- marginal cost is exactly 0 (officially free).
    {
        double S = 5000.0;
        double dreq = 2000.0;  // S+dreq = 7000 <= OA
        double marginal = kBetaFt * (std::max(0.0, S + dreq - OA) - std::max(0.0, S - OA));
        assert(marginal == 0.0);
    }

    // Regime 2: straddle -- S<OA<S+dreq -- marginal cost is the exact partial overage (S+dreq-OA),
    // strictly less than the full kBetaFt*dreq a flat aggregate charge would apply.
    {
        double S = 9000.0;
        double dreq = 3000.0;  // S+dreq = 12000 > OA=10000 > S=9000
        double marginal = kBetaFt * (std::max(0.0, S + dreq - OA) - std::max(0.0, S - OA));
        double expected = kBetaFt * (S + dreq - OA);
        assert(std::abs(marginal - expected) < 1e-9);
        assert(marginal > 0.0);
        assert(marginal < kBetaFt * dreq);
    }

    // Regime 3: fully saturated -- S>OA (so S+dreq>OA too, since dreq>=0) -- marginal cost is the
    // FULL kBetaFt*dreq, identical to what an unconditional per-hop charge would apply.
    {
        double S = 11000.0;
        double dreq = 1500.0;
        double marginal = kBetaFt * (std::max(0.0, S + dreq - OA) - std::max(0.0, S - OA));
        double expected = kBetaFt * dreq;
        assert(std::abs(marginal - expected) < 1e-9);
    }

    std::cout << "testAggressivePricingHingeArithmeticThreeRegimes ok\n";
}

// ---------------------------------------------------------------------------------------------
// (iv)/(v) behavior fixture: adapted from regression_p2_20260707.cpp's
// makeChannelVsFtFixture/testCostDrivenPrefersFtOverSaturatedChannel (channel + soft-block bypass:
// A bottom-left / B top-left / soft block S to the right, touching both directly). SAME channel
// geometry (vcap=25*80=2000) and SAME outline (780x810, outline_area=631,800) -- but S is given NO
// starting FT slack (ft_base_area == its own placed area, matching regression_p2_20260707.cpp's
// SoftFtRoundConsistencyFixture pattern) instead of the original's generous
// ft_base_area=372,949 << placed area=567,000. The original fixture's generous margin made
// feedthrough_mismatch==0 throughout, so conservative and aggressive pricing agreed trivially
// (verified as a TIE during this task's own probing); removing that margin creates a real per-block
// mismatch (conservative sees a cost) while the outline stays generous relative to S's
// ft_base_area (base_ft_area_sum=583,000 vs outline_area=631,800 -- 48,800 of aggregate slack) --
// exactly the "officially-free FT shortcut" scenario Codex finding #1 describes.
//
// Bundle size 2200 = channel cap (2000, free) + 200 "excess" nets that must go somewhere. Pinned
// numbers below were derived empirically via a scratch probe against the built router (this file's
// own established method -- see testCostDrivenPrefersFtOverSaturatedChannel's own doc comment in
// regression_p2_20260707.cpp), not hand-derived.
struct FtAggressiveFixture {
    Problem problem;
    Placement placement;
};

FtAggressiveFixture makeFtAggressiveFixture() {
    FtAggressiveFixture f;
    Problem& p = f.problem;
    p.outline_w = 80.0 + 700.0;
    p.outline_h = 610.0 + 100.0 + 100.0;
    p.alpha = 1.0;

    Block a = makeChannelBlock("A", 80.0, 100.0);
    Block b = makeChannelBlock("B", 80.0, 100.0);
    Block s;
    s.name = "S";
    s.width = 700.0;
    s.height = 810.0;
    s.area = 700.0 * 810.0;
    s.ft_base_area = s.area;  // NO starting slack (unlike the p2 original's 372,949)
    s.type = BlockType::Soft;
    s.ft_rates = {0.2, 0.4, 0.8, 1.0};

    p.blocks = {a, b, s};
    p.block_index = {{"A", 0}, {"B", 1}, {"S", 2}};

    Bundle bun;
    bun.source = 0;
    bun.target = 1;
    bun.nets = 2200;
    p.bundles = {bun};

    Placement& pl = f.placement;
    pl.rects = {
        Rect{0.0, 0.0, 80.0, 100.0},     // A
        Rect{0.0, 710.0, 80.0, 100.0},   // B
        Rect{80.0, 0.0, 700.0, 810.0},   // S: touches A's right edge and B's right edge directly
    };
    pl.aspect = {1.0, 1.0, 1.0};
    return f;
}

// (v) Independent recomputation of ft_area_sum from a RoutingResult's soft_feedthrough_nets, using
// the SAME formula computeOverflowAndMismatch (routing.cpp) uses for its own full recompute.
double recomputeFtAreaSum(const Problem& problem, const RoutingResult& rr) {
    double sum = 0.0;
    for (std::size_t i = 0; i < problem.blocks.size(); ++i) {
        const Block& blk = problem.blocks[i];
        if (isSoft(blk.type)) {
            double base = blk.ft_base_area > 0.0 ? blk.ft_base_area : blk.area;
            double nets_i = rr.soft_feedthrough_nets[i];
            sum += ftRequiredArea(base, ftExtend(nets_i, ftTierRate(blk, nets_i)));
        } else {
            sum += blk.area;
        }
    }
    return sum;
}

void testAggressivePricingWinsOverConservativeOnOfficiallyFreeFtShortcut() {
    FtAggressiveFixture f = makeFtAggressiveFixture();
    double outline_area = f.problem.outline_w * f.problem.outline_h;
    assert(std::abs(outline_area - 631800.0) < kEps);

    // Sanity: base aggregate has comfortable slack under the outline before any nets route at all
    // (the "generous outline" precondition this fixture is built to exercise).
    double base_ft_area_sum = f.problem.blocks[0].area + f.problem.blocks[1].area + f.problem.blocks[2].ft_base_area;
    assert(std::abs(base_ft_area_sum - 583000.0) < kEps);
    assert(base_ft_area_sum < outline_area);

    Problem conservative = f.problem;
    conservative.ft_pricing_aggressive = false;
    RoutingResult rc = routeProblem(conservative, f.placement, /*cost_driven=*/true);
    assert(rc.open_routes == 0);

    Problem aggressive = f.problem;
    aggressive.ft_pricing_aggressive = true;
    RoutingResult ra = routeProblem(aggressive, f.placement, /*cost_driven=*/true);
    assert(ra.open_routes == 0);

    // Pinned (scratch-probe-derived): conservative never finds the FT detour cheap enough to beat
    // the channel (S sits much farther from A/B than the channel does, so its wire-cost premium
    // dominates the router's per-block-mismatch-priced FT option) -- it eats the 200-net channel
    // overflow instead. Aggressive recognizes the SAME detour keeps the aggregate under
    // outline_area (officially free) and takes it, clearing the overflow entirely.
    assert(rc.soft_feedthrough_nets[2] == 0.0);
    assert(std::abs(rc.channel_overflow_nets - 200.0) < kEps);
    assert(std::abs(rc.channel_overflow - 4880.0) < 1e-3);
    assert(std::abs(rc.total_wirelength - 1342000.0) < 1.0);
    assert(std::abs(rc.ft_area_sum - 583000.0) < 1e-3);

    assert(std::abs(ra.soft_feedthrough_nets[2] - 200.0) < kEps);
    assert(ra.channel_overflow_nets == 0.0);
    assert(ra.channel_overflow == 0.0);
    assert(std::abs(ra.total_wirelength - 1362000.0) < 1.0);
    assert(std::abs(ra.ft_area_sum - 584506.99) < 1e-3);
    assert(ra.ft_area_sum < outline_area);  // aggregate stays under OA -- officially free, as designed

    // (iv) "at least as many nets diverted through S under aggressive as conservative": here
    // strictly more (200 > 0), not a tie -- `>=` is asserted so the test would still hold (and say
    // so, below) if some future change to this fixture ever produced an exact tie instead.
    assert(ra.soft_feedthrough_nets[2] >= rc.soft_feedthrough_nets[2]);
    bool nets_tie = (ra.soft_feedthrough_nets[2] == rc.soft_feedthrough_nets[2]);

    auto key_c = roundCostOfficial(rc, conservative.alpha, outline_area);
    auto key_a = roundCostOfficial(ra, aggressive.alpha, outline_area);
    assert(key_c.first == 0 && key_a.first == 0);
    assert(std::abs(key_c.second - 1488504.347826) < 1e-3);
    assert(std::abs(key_a.second - 1362000.0) < 1e-3);
    assert(key_a.second <= key_c.second);  // value-gated: aggressive is never worse than conservative
    bool value_tie = std::abs(key_a.second - key_c.second) < 1e-6;

    // Same comparison via exactScore (direct declared-outline path, no shrink-wrap/corrective-grow
    // indirection -- same discipline as regression_p2_20260707.cpp's
    // testRoundCostOfficialAlgebraicallyTiesToExactScore).
    Placement pl_c = f.placement;
    pl_c.outline_w = f.problem.outline_w;
    pl_c.outline_h = f.problem.outline_h;
    RoutingResult routing_c;
    Score score_c = exactScore(conservative, pl_c, &routing_c);

    Placement pl_a = f.placement;
    pl_a.outline_w = f.problem.outline_w;
    pl_a.outline_h = f.problem.outline_h;
    RoutingResult routing_a;
    Score score_a = exactScore(aggressive, pl_a, &routing_a);

    assert(std::abs(score_c.value - 2120304.347826) < 1e-3);
    assert(std::abs(score_a.value - 1993800.0) < 1e-3);
    assert(score_a.value <= score_c.value);

    if (value_tie || nets_tie) {
        std::cout << "  NOTE: aggressive tied conservative exactly on this fixture (nets_tie="
                  << nets_tie << ", value_tie=" << value_tie << ") -- still a valid (never-worse) outcome.\n";
    } else {
        // What this fixture actually demonstrates (Codex finding #1): a strict, substantial win --
        // not merely a tie -- by avoiding the channel-overflow penalty entirely.
        assert(score_a.value < score_c.value - 1.0);
        assert(key_a.second < key_c.second - 1.0);
    }

    // (v) running-sum consistency: rr.ft_area_sum (accrued incrementally by commitRoutePath, seeded
    // by base_ft_area_sum, routing.cpp) equals an independent recompute using
    // computeOverflowAndMismatch's own formula, for every RoutingResult this test produced.
    assert(std::abs(recomputeFtAreaSum(conservative, rc) - rc.ft_area_sum) < 1e-6);
    assert(std::abs(recomputeFtAreaSum(aggressive, ra) - ra.ft_area_sum) < 1e-6);
    assert(std::abs(recomputeFtAreaSum(conservative, routing_c) - routing_c.ft_area_sum) < 1e-6);
    assert(std::abs(recomputeFtAreaSum(aggressive, routing_a) - routing_a.ft_area_sum) < 1e-6);

    std::cout << "testAggressivePricingWinsOverConservativeOnOfficiallyFreeFtShortcut ok "
                 "(conservative key=" << key_c.second << ", aggressive key=" << key_a.second
              << ", nets[S] conservative=" << rc.soft_feedthrough_nets[2]
              << " aggressive=" << ra.soft_feedthrough_nets[2] << ")\n";
}

}  // namespace

int main() {
    testDefaultVariantsRegistryShape();
    testFtRequiredDeltaMatchesMismatchDeltaWhenClampInactive();
    testFtRequiredDeltaIgnoresPlacedAreaClamp();
    testAggressivePricingHingeArithmeticThreeRegimes();
    testAggressivePricingWinsOverConservativeOnOfficiallyFreeFtShortcut();
    std::cout << "F-FT (ft_pricing_aggressive) regressions passed\n";
    return 0;
}
