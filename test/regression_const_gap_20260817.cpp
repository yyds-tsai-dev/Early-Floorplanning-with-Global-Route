// B1 (constant-gap negotiation pricing, .superpowers/sdd/levers-batch2-briefs-20260817.md Brief
// 2): regression coverage for constantGapHistoryUnits' algebra (route_cost.hpp), the negotiation
// mechanism it drives (routing.cpp's ASSIGN-not-accumulate channel-history bump + sticky/no-decay
// history under problem.route_const_gap), and roundCostOfficial's lexicographic ordering of the two
// flag arms on the SAME congested fixture. Same minimal link line as
// run_v5_tail_pricing_regressions.sh (proven sufficient for respace.cpp+routing.cpp fixtures) --
// see run_const_gap_regressions.sh.
#include <cassert>
#include <cmath>
#include <iostream>
#include <string>

#include "../src/model.hpp"
#include "../src/route_cost.hpp"
#include "../src/routing.hpp"

namespace {
using namespace efgr;

constexpr double kTol = 1e-9;

Block makeChannelBlock(const std::string& name, double w, double h) {
    Block b;
    b.name = name;
    b.width = w;
    b.height = h;
    b.area = w * h;
    b.type = BlockType::Macro;
    return b;
}

// Adapted from regression_p2_20260707.cpp's ChannelVsFtFixture (channel between two stacked macros
// A/B, an FT-capable soft block S alongside): A(0,0,80,100), B(0,710,80,100) -- a vertical channel
// of height 610 between them (raw vertical-usage capacity 25*80=2000 nets) -- S(80,0,700,810),
// touching both A and B, ft_base_area=372949 (design-doc worked example base), ft_rates={0.2, 0.4,
// 0.8, 1.0} (tiers at 3000/6000/9000 accumulated crossing nets). `nets` is the caller's own single
// A->B bundle size -- deliberately parameterized so (b)/(d) and (c) below share one builder.
Problem makeFixtureProblem(int nets) {
    Problem p;
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
    s.ft_base_area = 372949.0;
    s.type = BlockType::Soft;
    s.ft_rates = {0.2, 0.4, 0.8, 1.0};

    p.blocks = {a, b, s};
    p.block_index = {{"A", 0}, {"B", 1}, {"S", 2}};

    Bundle bun;
    bun.source = 0;
    bun.target = 1;
    bun.nets = nets;
    p.bundles = {bun};
    return p;
}

Placement makeFixturePlacement() {
    Placement pl;
    pl.rects = {
        Rect{0.0, 0.0, 80.0, 100.0},
        Rect{0.0, 710.0, 80.0, 100.0},
        Rect{80.0, 0.0, 700.0, 810.0},
    };
    pl.aspect = {1.0, 1.0, 1.0};
    return pl;
}

// ---- (a) constantGapHistoryUnits algebra + price<=0 => 0 -----------------------------------------

void testConstantGapHistoryUnitsAlgebra() {
    // p_ovf*hist == kConstGapDetourFactor*alpha*(W+H) exactly, by construction of the formula.
    double v = constantGapHistoryUnits(/*alpha=*/2.0, /*outline_w=*/100.0, /*outline_h=*/50.0, /*price=*/10.0);
    assert(std::abs(v - 30.0) < kTol);  // 1.0*2.0*(100+50)/10.0 = 30.0
    assert(std::abs(v * 10.0 - kConstGapDetourFactor * 2.0 * 150.0) < kTol);

    double v2 = constantGapHistoryUnits(1.0, 780.0, 810.0, 100.0);
    assert(std::abs(v2 - 15.9) < kTol);  // 1.0*1.0*(780+810)/100.0

    assert(constantGapHistoryUnits(1.0, 100.0, 100.0, 0.0) == 0.0);
    assert(constantGapHistoryUnits(1.0, 100.0, 100.0, -5.0) == 0.0);

    std::cout << "testConstantGapHistoryUnitsAlgebra ok\n";
}

// ---- (b) flag-on round 1 reroutes the congested bundle; flag-off reproduces today's path ----------

// nets=4000 against the channel's raw 2000-net cap: legacy's escalating-but-tiny history bump can't
// push the router past the FT tier-rate jump at 3000 ACCUMULATED crossing nets (ftTierRate jumps
// 0.2->0.4 there, making the marginal FT price spike right where legacy would need to divert the
// bulk of the excess) -- flag-off settles at exactly channel_overflow_nets=2000 (every net at/over
// cap stays in the channel, i.e. reproduces the pre-B1 router's own outcome, which flag-off is BY
// CONSTRUCTION byte-identical to). Flag-on's much larger, sticky, size-correct surcharge instead
// clears the tier wall and diverts the whole bundle to S, landing at channel_overflow_nets=0.
// Confirmed via a scratch probe against the built router (matching this codebase's established
// fixture-derivation practice -- see e.g. regression_p2_20260707.cpp's own B4/V5 re-baseline notes).
void testFlagOnReroutesCongestedBundleFlagOffStaysOverflowed() {
    Problem p = makeFixtureProblem(4000);
    Placement pl = makeFixturePlacement();

    Problem p_off = p;
    p_off.route_const_gap = false;
    RoutingResult rr_off = routeProblem(p_off, pl, /*cost_driven=*/true);
    assert(rr_off.open_routes == 0);
    assert(std::abs(rr_off.channel_overflow_nets - 2000.0) < 1e-6);  // stuck: today's path

    Problem p_on = p;
    p_on.route_const_gap = true;
    RoutingResult rr_on = routeProblem(p_on, pl, /*cost_driven=*/true);
    assert(rr_on.open_routes == 0);
    assert(std::abs(rr_on.channel_overflow_nets - 0.0) < 1e-6);  // fully rerouted
    assert(rr_on.soft_feedthrough_nets[2] > 0.0);

    std::cout << "testFlagOnReroutesCongestedBundleFlagOffStaysOverflowed ok (off_ovf="
              << rr_off.channel_overflow_nets << " on_ovf=" << rr_on.channel_overflow_nets << ")\n";
}

// ---- (c) history unchanged across rounds when nothing overflows -----------------------------------

// nets=1000, comfortably under the channel's 2000-net cap: neither the decay guard nor the
// bump/assign branch ever fires under either flag (both are gated on "this resource ended the round
// over capacity", which is never true here), so the two flags' RoutingResults must be bit-identical.
void testHistoryUnchangedWhenNothingOverflows() {
    Problem p = makeFixtureProblem(1000);
    Placement pl = makeFixturePlacement();

    Problem p_off = p;
    p_off.route_const_gap = false;
    RoutingResult rr_off = routeProblem(p_off, pl, /*cost_driven=*/true);

    Problem p_on = p;
    p_on.route_const_gap = true;
    RoutingResult rr_on = routeProblem(p_on, pl, /*cost_driven=*/true);

    assert(rr_off.open_routes == 0 && rr_on.open_routes == 0);
    assert(rr_off.channel_overflow_nets == 0.0 && rr_on.channel_overflow_nets == 0.0);
    assert(rr_off.total_wirelength == rr_on.total_wirelength);
    assert(rr_off.soft_feedthrough_nets[2] == rr_on.soft_feedthrough_nets[2]);

    std::cout << "testHistoryUnchangedWhenNothingOverflows ok (wl=" << rr_off.total_wirelength << ")\n";
}

// ---- (d) roundCostOfficial lexicographic selection under both flags -------------------------------

// Reuses (b)'s congested fixture: the OFFICIAL scorer-unit key (not the raw nets-overflow count)
// must also rank flag-on's routed result at least as good as flag-off's -- i.e. the negotiation
// mechanism's local per-edge Dijkstra pricing decision, once realized into a full routing, is not
// just "less raw overflow" but genuinely cheaper (or no worse) under the SAME key routeProblem's own
// round-selection / legacy-comparison gate uses.
void testRoundCostOfficialOrdersFlagOnNoWorseThanFlagOff() {
    Problem p = makeFixtureProblem(4000);
    Placement pl = makeFixturePlacement();
    double outline_area = p.outline_w * p.outline_h;

    Problem p_off = p;
    p_off.route_const_gap = false;
    RoutingResult rr_off = routeProblem(p_off, pl, /*cost_driven=*/true);

    Problem p_on = p;
    p_on.route_const_gap = true;
    RoutingResult rr_on = routeProblem(p_on, pl, /*cost_driven=*/true);

    std::pair<int, double> key_off = roundCostOfficial(rr_off, p.alpha, outline_area);
    std::pair<int, double> key_on = roundCostOfficial(rr_on, p.alpha, outline_area);

    assert(key_off.first == 0 && key_on.first == 0);  // both fully routed
    assert(key_on.second < key_off.second);            // flag-on strictly cheaper in official units

    std::cout << "testRoundCostOfficialOrdersFlagOnNoWorseThanFlagOff ok (off=" << key_off.second
              << " on=" << key_on.second << ")\n";
}

}  // namespace

int main() {
    testConstantGapHistoryUnitsAlgebra();
    testFlagOnReroutesCongestedBundleFlagOffStaysOverflowed();
    testHistoryUnchangedWhenNothingOverflows();
    testRoundCostOfficialOrdersFlagOnNoWorseThanFlagOff();
    std::cout << "const-gap negotiation (B1) regressions passed\n";
    return 0;
}
