#include <cassert>
#include <cmath>
#include <iostream>
#include <string>

#include "../src/eval.hpp"
#include "../src/ft.hpp"
#include "../src/model.hpp"
#include "../src/route_cost.hpp"
#include "../src/routing.hpp"

// P2 Task 0 (design p2-design.md §8.1 R0 / plan Global Constraint 2): area-denominate
// channel_overflow so it is comparable, in the scorer's shared 1e6 weight (eval.cpp:143-144), to
// feedthrough_mismatch, which is already um^2. Previously channel_overflow was nets-over-capacity
// -- an apples-to-oranges mismatch that hid the official 2.4x FT win from the scorer.
//
// B4 (b4-design.md, .superpowers/sdd/b4-brief.md): official per-edge marginal pricing. New fixtures
// below test marginalOverflowNets/channelPathSlack directly (route_cost.hpp) and routeProblem's
// re-denominated channel/FT pricing + roundCostOfficial gate end-to-end. Several PRE-EXISTING
// choice-based fixtures in this file (not just the one design §8 names) also had their asserted
// NUMBERS re-derived because B4 deliberately changes which path/chunking the router picks --
// each re-derived fixture says so explicitly at its definition; accrual-based fixtures (channel
// usage/overflow-area/FT-mismatch numbers that flow from COMMITTED routes, not from which path was
// chosen) are confirmed unchanged by construction (commit accrual is untouched by B4) and are not
// modified.

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

// Fixture (a): vertical usage overflow. Two blocks stacked with a wide horizontal gap between
// them so extractChannels carves a single channel whose WIDTH is 80 and whose HEIGHT (span) is
// 610; a bundle whose guide-point path crosses that gap vertically (dy>0, dx==0) drives pure
// vertical_usage. vcap = 25*w = 25*80 = 2000; usage 3600 overflows by 1600 nets. Per Global
// Constraint 2, a vertical overflow's missing capacity is a WIDTH deficit, spanning the channel's
// HEIGHT: missing_width = (3600-2000)/25 = 64; area = 64 * 610 = 39,040 um^2 (old code: 1600).
void testVerticalOverflowIsAreaDenominatedByChannelHeight() {
    Problem p;
    p.outline_w = 80.0;
    p.outline_h = 610.0 + 100.0 + 100.0;  // A (bottom) + channel span 610 + B (top)
    p.blocks = {makeChannelBlock("A", 80.0, 100.0), makeChannelBlock("B", 80.0, 100.0)};
    p.block_index = {{"A", 0}, {"B", 1}};
    p.alpha = 1.0;
    Bundle bun;
    bun.source = 0;
    bun.target = 1;
    bun.nets = 3600;
    p.bundles = {bun};

    Placement pl;
    pl.rects = {Rect{0.0, 0.0, 80.0, 100.0}, Rect{0.0, 710.0, 80.0, 100.0}};
    pl.aspect = {1.0, 1.0};

    RoutingResult r = routeProblem(p, pl);
    assert(r.open_routes == 0);

    // Locate the single channel carved between A and B; confirm the geometry this fixture assumes.
    bool found = false;
    for (const auto& ch : r.channels) {
        if (std::abs(ch.rect.w - 80.0) < kEps && std::abs(ch.rect.h - 610.0) < kEps) {
            found = true;
            assert(std::abs(ch.vertical_usage - 7200.0) < kEps);
            assert(std::abs(ch.horizontal_usage) < kEps);
        }
    }
    assert(found);

    double expected = ((7200.0 - 2000.0) / 25.0) * 610.0;  // 208 * 610 = 126,880  (per-edge: entry+exit charge 3600 each = 7200)
    assert(std::abs(expected - 126880.0) < kEps);
    assert(std::abs(r.channel_overflow - expected) < 1e-3);
    // Direction assert against the OLD (pre-T0) denomination: nets-over-capacity was 1600, a much
    // smaller number than the area-denominated 39,040 -- the fix must have strictly increased the
    // charged overflow on this saturating fixture, not merely kept it nonzero.
    assert(r.channel_overflow > 1600.0);
    std::cout << "testVerticalOverflowIsAreaDenominatedByChannelHeight ok\n";
}

// Fixture (b): horizontal usage overflow (symmetric). Two blocks side by side with a wide
// vertical gap between them so the channel's HEIGHT is 80 and WIDTH (span) is 610; a bundle
// crossing horizontally (dx>0, dy==0) drives pure horizontal_usage. hcap = 25*h = 25*80 = 2000;
// usage 3600 overflows by 1600 nets. Per Global Constraint 2, a horizontal overflow's missing
// capacity is a HEIGHT deficit, spanning the channel's WIDTH: missing_height = 64; area = 64 * 610
// = 39,040 um^2.
void testHorizontalOverflowIsAreaDenominatedByChannelWidth() {
    Problem p;
    p.outline_w = 610.0 + 100.0 + 100.0;
    p.outline_h = 80.0;
    p.blocks = {makeChannelBlock("L", 100.0, 80.0), makeChannelBlock("R", 100.0, 80.0)};
    p.block_index = {{"L", 0}, {"R", 1}};
    p.alpha = 1.0;
    Bundle bun;
    bun.source = 0;
    bun.target = 1;
    bun.nets = 3600;
    p.bundles = {bun};

    Placement pl;
    pl.rects = {Rect{0.0, 0.0, 100.0, 80.0}, Rect{710.0, 0.0, 100.0, 80.0}};
    pl.aspect = {1.0, 1.0};

    RoutingResult r = routeProblem(p, pl);
    assert(r.open_routes == 0);

    bool found = false;
    for (const auto& ch : r.channels) {
        if (std::abs(ch.rect.h - 80.0) < kEps && std::abs(ch.rect.w - 610.0) < kEps) {
            found = true;
            assert(std::abs(ch.horizontal_usage - 7200.0) < kEps);
            assert(std::abs(ch.vertical_usage) < kEps);
        }
    }
    assert(found);

    double expected = ((7200.0 - 2000.0) / 25.0) * 610.0;  // 126,880 (per-edge doubling)
    assert(std::abs(r.channel_overflow - expected) < 1e-3);
    assert(r.channel_overflow > 1600.0);
    std::cout << "testHorizontalOverflowIsAreaDenominatedByChannelWidth ok\n";
}

// Official-example comparability (design p2-design.md §8.1 / plan Task 0): with area-denomination
// landed, channel_overflow and feedthrough_mismatch are now both um^2, so the scorer's shared 1e6
// weight compares like units for the first time. This does not assert the router picks FT (that
// is P2 T1+); it only asserts the units are now COMPARABLE in magnitude -- computed from ft.hpp's
// own functions, not hardcoded on either side, within a 1.3x margin (the exact ratio is ~1.08 per
// the design doc's worked example, base=372,949, tier 40%, 3600 nets).
void testOverflowAreaComparableToFtMismatchMagnitude() {
    double base = 372949.0;
    double rate = ftTierRate(Block{"", 0, base, 0, 0, 1.0, 1.0, BlockType::Soft, {}, {0.2, 0.4, 0.8, 1.0}, 0, {}},
                              3600.0);
    double extend = ftExtend(3600.0, rate);
    double required = ftRequiredArea(base, extend);
    double ft_mismatch = required - base;  // block placed at exactly its base (unexpanded) area

    double overflow_area = ((3600.0 - 2000.0) / 25.0) * 610.0;  // same vertical fixture as (a)

    assert(ft_mismatch > 0.0);
    assert(overflow_area > 0.0);
    double ratio = overflow_area / ft_mismatch;
    assert(ratio > 1.0 / 1.3 && ratio < 1.3);
    std::cout << "testOverflowAreaComparableToFtMismatchMagnitude ok\n";
}

// P2 Task 1 (design p2-design.md §Q1 / plan Global Constraint 1/3/4): pure cost-unit routing edge
// functions in src/route_cost.hpp. Tests below are independent of routing/eval -- they call
// channelMarginalCost / softFtMarginalCost / wireCost directly against hand-computed expectations.

// Fixture (a): slack channel -- usage+n stays within capacity in both directions, so
// channelMarginalCost must be exactly 0 regardless of direction.
void testChannelMarginalCostIsZeroWhenSlack() {
    Channel ch;
    ch.name = "CH1";
    ch.rect = Rect{0.0, 0.0, 80.0, 610.0};
    // vertical cap = 25*w = 2000; usage 500 + n 100 = 600 <= 2000 -> free.
    assert(channelMarginalCost(ch, /*horizontal=*/false, 500.0, 100.0) == 0.0);
    // horizontal cap = 25*h = 15250; usage 1000 + n 100 = 1100 <= 15250 -> free.
    assert(channelMarginalCost(ch, /*horizontal=*/true, 1000.0, 100.0) == 0.0);
    std::cout << "testChannelMarginalCostIsZeroWhenSlack ok\n";
}

// Fixture (b): saturated channel, vertical direction. w=80 h=610 -> vcap = 25*80 = 2000. usage is
// already exactly at cap (2000); n=100 nets are pure marginal overflow. Per T0's area denomination
// (missing width spans the channel's HEIGHT for a vertical overflow): missing_width = n/25 = 4,
// area = 4 * 610 = 2440 um^2; cost = kOverflowAreaWeight * 2440 = 1e6 * 2440 = 2.44e9.
void testChannelMarginalCostSaturatedVertical() {
    Channel ch;
    ch.name = "CH1";
    ch.rect = Rect{0.0, 0.0, 80.0, 610.0};
    double usage = 2000.0;  // exactly at vcap
    double n = 100.0;
    double cost = channelMarginalCost(ch, /*horizontal=*/false, usage, n);
    double expected = kOverflowAreaWeight * ((n / 25.0) * ch.rect.h);
    assert(std::abs(expected - 2.44e9) < 1.0);
    assert(std::abs(cost - expected) < 1.0);
    assert(std::abs(cost - 2.44e9) < 1.0);
    std::cout << "testChannelMarginalCostSaturatedVertical ok\n";
}

// Fixture (c): saturated channel, horizontal direction (symmetric). w=610 h=80 -> hcap = 25*80 =
// 2000. usage at cap, n=100 nets all overflow; missing_height spans the channel's WIDTH:
// missing_height = 100/25 = 4, area = 4 * 610 = 2440 um^2; cost = 1e6 * 2440 = 2.44e9.
void testChannelMarginalCostSaturatedHorizontal() {
    Channel ch;
    ch.name = "CH2";
    ch.rect = Rect{0.0, 0.0, 610.0, 80.0};
    double usage = 2000.0;  // exactly at hcap
    double n = 100.0;
    double cost = channelMarginalCost(ch, /*horizontal=*/true, usage, n);
    double expected = kOverflowAreaWeight * ((n / 25.0) * ch.rect.w);
    assert(std::abs(expected - 2.44e9) < 1.0);
    assert(std::abs(cost - expected) < 1.0);
    std::cout << "testChannelMarginalCostSaturatedHorizontal ok\n";
}

// Fixture (d): straddling the cap -- usage=1900 (below vcap=2000), n=200 pushes to 2100, so only
// 100 nets are over. Marginal cost must charge only the overage, not the full n.
void testChannelMarginalCostStraddlesCap() {
    Channel ch;
    ch.name = "CH1";
    ch.rect = Rect{0.0, 0.0, 80.0, 610.0};
    double usage = 1900.0;
    double n = 200.0;
    double cost = channelMarginalCost(ch, /*horizontal=*/false, usage, n);
    double over = (usage + n) - 2000.0;  // 100 nets over cap
    assert(std::abs(over - 100.0) < kEps);
    double expected = kOverflowAreaWeight * ((over / 25.0) * ch.rect.h);
    assert(std::abs(cost - expected) < 1.0);
    std::cout << "testChannelMarginalCostStraddlesCap ok\n";
}

// Fixture (e): FT tier-jump pricing. Block base area 372,949 (official example), placed exactly at
// the required area for acc=2900 nets (tier 0, <=3000, rate 0.2). Pushing n=200 more nets crosses
// the 3000-net tier boundary into tier 1 (rate 0.4). softFtMarginalCost must price exactly the
// required-area delta between acc=2900 and acc+n=3100 (computed via ft.hpp directly, not
// hardcoded), since placed_area covers acc's requirement exactly (no free slack beyond it).
void testSoftFtMarginalCostPricesTierJump() {
    Block b;
    b.name = "BLK";
    b.ft_base_area = 372949.0;
    b.area = 372949.0;
    b.type = BlockType::Soft;
    b.ft_rates = {0.2, 0.4, 0.8, 1.0};

    double acc = 2900.0;
    double n = 200.0;
    double required_before = ftRequiredArea(b.ft_base_area, ftExtend(acc, ftTierRate(b, acc)));
    double required_after = ftRequiredArea(b.ft_base_area, ftExtend(acc + n, ftTierRate(b, acc + n)));
    // Confirm this fixture actually crosses the tier boundary (rate changes 0.2 -> 0.4).
    assert(std::abs(ftTierRate(b, acc) - 0.2) < kEps);
    assert(std::abs(ftTierRate(b, acc + n) - 0.4) < kEps);
    assert(required_after > required_before);

    double placed_area = required_before;  // placed exactly covers acc's requirement, no more
    double cost = softFtMarginalCost(b, acc, n, placed_area);
    double expected = kFtMismatchWeight * (required_after - placed_area);
    assert(std::abs(cost - expected) < 1e-3);
    assert(cost > 0.0);
    std::cout << "testSoftFtMarginalCostPricesTierJump ok\n";
}

// Fixture (f): generous placed_area (covers even the post-crossing requirement) -> free.
void testSoftFtMarginalCostFreeWhenPlacedAreaGenerous() {
    Block b;
    b.name = "BLK";
    b.ft_base_area = 372949.0;
    b.area = 372949.0;
    b.type = BlockType::Soft;
    b.ft_rates = {0.2, 0.4, 0.8, 1.0};

    double acc = 2900.0;
    double n = 200.0;
    double required_after = ftRequiredArea(b.ft_base_area, ftExtend(acc + n, ftTierRate(b, acc + n)));
    double placed_area = required_after + 10000.0;  // generous slack beyond the post-crossing need
    double cost = softFtMarginalCost(b, acc, n, placed_area);
    assert(cost == 0.0);
    std::cout << "testSoftFtMarginalCostFreeWhenPlacedAreaGenerous ok\n";
}

// Fixture (g): wireCost trivial sanity.
void testWireCostTrivial() {
    assert(std::abs(wireCost(2.0, 10.0, 5.0) - 100.0) < kEps);
    assert(wireCost(1.0, 0.0, 100.0) == 0.0);
    assert(wireCost(1.0, 10.0, 0.0) == 0.0);
    std::cout << "testWireCostTrivial ok\n";
}

// B4-T1 (design b4-design.md §3.2 / brief test (i)/(iii) support): marginalOverflowNets --
// exact-piecewise marginal overflow in NETS (not area), RAW caps. Independent of routing/eval.

// Slack: usage+n stays under RAW cap in both directions -> exactly 0.
void testMarginalOverflowNetsZeroWhenSlack() {
    Channel ch;
    ch.name = "CH1";
    ch.rect = Rect{0.0, 0.0, 80.0, 610.0};
    // vertical RAW cap = 25*w = 2000; usage 500 + n 100 = 600 <= 2000 -> free.
    assert(marginalOverflowNets(ch, /*horizontal=*/false, 500.0, 100.0) == 0.0);
    // horizontal RAW cap = 25*h = 15250; usage 1000 + n 100 = 1100 <= 15250 -> free.
    assert(marginalOverflowNets(ch, /*horizontal=*/true, 1000.0, 100.0) == 0.0);
    std::cout << "testMarginalOverflowNetsZeroWhenSlack ok\n";
}

// Saturated: usage already >= RAW cap, so every pushed net is marginal overflow -> exactly n.
void testMarginalOverflowNetsSaturated() {
    Channel ch;
    ch.name = "CH1";
    ch.rect = Rect{0.0, 0.0, 80.0, 610.0};
    double cap = 25.0 * ch.rect.w;  // 2000
    double n = 50.0;
    double result = marginalOverflowNets(ch, /*horizontal=*/false, cap + 100.0, n);
    assert(std::abs(result - n) < kEps);
    std::cout << "testMarginalOverflowNetsSaturated ok\n";
}

// Straddle: usage < cap < usage+n -- only the overage counts, not the full n.
void testMarginalOverflowNetsStraddles() {
    Channel ch;
    ch.name = "CH1";
    ch.rect = Rect{0.0, 0.0, 80.0, 610.0};
    double cap = 25.0 * ch.rect.w;  // 2000
    double usage = cap - 50.0;     // 1950, 50 nets of slack left
    double n = 100.0;              // pushes 50 over
    double result = marginalOverflowNets(ch, /*horizontal=*/false, usage, n);
    assert(std::abs(result - 50.0) < kEps);
    std::cout << "testMarginalOverflowNetsStraddles ok\n";
}

// RAW cap vs the OLD clamped cap (channelOverflowArea's max(1.0,dim)) diverge on a sub-1um channel:
// a channel with rect.h=0.5 has RAW horizontal cap 25*0.5=12.5, but the old clamped cap treats it as
// 25*max(1.0,0.5)=25 -- more than double. At usage=15 (already over the RAW cap, still under the
// clamped one), pushing n=5 more nets is ALL marginal overflow under RAW accounting but the OLD
// clamped-cap accounting (channelOverflowArea/channelMarginalCost) sees zero overflow at all, since
// 15 and 15+5=20 both stay under its clamped cap of 25. This is exactly the fidelity gap design §1
// flags: "the old router pricing used clamped caps, a latent fidelity gap B4 also closes."
void testMarginalOverflowNetsRawCapDivergesFromOldClampedCapOnSubMicronChannel() {
    Channel ch;
    ch.name = "CH1";
    ch.rect = Rect{0.0, 0.0, 100.0, 0.5};  // sub-1um height
    double usage = 15.0;
    double n = 5.0;
    double raw_result = marginalOverflowNets(ch, /*horizontal=*/true, usage, n);
    assert(std::abs(raw_result - n) < kEps);  // RAW cap=12.5: usage already over it, all n counts
    double old_before = channelOverflowArea(ch, /*horizontal=*/true, usage);
    double old_after = channelOverflowArea(ch, /*horizontal=*/true, usage + n);
    assert(old_before == 0.0 && old_after == 0.0);  // clamped cap=25: both usage and usage+n fit
    assert(channelMarginalCost(ch, /*horizontal=*/true, usage, n) == 0.0);  // old code: blind to it
    std::cout << "testMarginalOverflowNetsRawCapDivergesFromOldClampedCapOnSubMicronChannel ok\n";
}

// B4-T3 (design b4-design.md §4 / brief test (iii)): channelPathSlack -- the extracted, directly
// unit-testable core of pathSlack's per-node channel residual. Hand-built rects/channel, no
// Problem/Placement/routing needed. Numbers are design §4's own worked examples verbatim.

// Straight pass-through: predecessor and successor both meet the channel on the SAME axis (here,
// horizontal -- pred touches the channel's left edge, channel's right edge touches succ), so
// h_charges=2 and the residual (cap-usage=100) is HALVED to 50.
void testChannelPathSlackStraightPassThroughHalves() {
    Rect pred{0.0, 0.0, 10.0, 10.0};       // x=[0,10)
    Rect succ{15.0, 0.0, 10.0, 10.0};      // x=[15,25)
    Channel ch;
    ch.name = "CH1";
    ch.rect = Rect{10.0, 0.0, 5.0, 8.0};   // x=[10,15), touches pred's right and succ's left
    // cap_H = 25*8 = 200; usage=100 -> cap-usage=100, halved by h_charges=2 -> 50.
    ch.horizontal_usage = 100.0;
    ch.vertical_usage = 0.0;
    double slack = channelPathSlack(pred, ch, succ);
    assert(std::abs(slack - 50.0) < kEps);
    std::cout << "testChannelPathSlackStraightPassThroughHalves ok\n";
}

// L-turn: predecessor meets the channel horizontally (left edge), successor meets it vertically
// (top edge) -- h_charges=1, v_charges=1, so each axis's residual divides by 1 and the TIGHTER axis
// (V, 30 < H's 100) binds. Design §4's own worked numbers: cap_H-hu=100, cap_V-vu=30 -> slack=30.
void testChannelPathSlackLTurnTighterAxisBinds() {
    Rect pred{0.0, 0.0, 10.0, 10.0};        // touches the channel's left edge (horizontal entry)
    Rect succ{10.0, 10.0, 5.0, 10.0};       // touches the channel's top edge (vertical exit)
    Channel ch;
    ch.name = "CH1";
    ch.rect = Rect{10.0, 0.0, 5.0, 10.0};   // x=[10,15), y=[0,10)
    // cap_H = 25*10 = 250; want cap_H-hu=100 -> hu=150.
    ch.horizontal_usage = 150.0;
    // cap_V = 25*5 = 125; want cap_V-vu=30 -> vu=95.
    ch.vertical_usage = 95.0;
    double slack = channelPathSlack(pred, ch, succ);
    assert(std::abs(slack - 30.0) < kEps);
    std::cout << "testChannelPathSlackLTurnTighterAxisBinds ok\n";
}

// P2 Task 2 (design p2-design.md §Q1-Q2 core; plan Global Constraint 7 comparison gate): the
// cost-driven single-pass Dijkstra in routing.cpp routeChunk. Tests below exercise routeProblem's
// new `cost_driven` parameter directly (default false == legacy byte-identical; true == new path).

// Builds a 3-block layout: A (source, hard macro) bottom-left, B (target, hard macro) top-left,
// separated by a 610-tall vertical channel (identical geometry to the T0 fixture (a) above, so its
// vcap=2000 math is already verified) -- AND a soft block S placed to the right of both A and B,
// directly abutting each (zero channel between A-S and S-B), offering an all-block bypass of the
// saturated channel. `ft_base_area` matches the official design-doc worked example (372,949) so
// this fixture's numbers are traceable to p2-design.md §8.1/§3.3 (3600 nets @ 40% tier -> FT
// marginal mismatch 36,261.34 um^2, STRICTLY less than the channel's marginal overflow area 39,040
// um^2 for the same 3600 nets from an empty channel) -- confirmed by hand computation against
// ft.hpp's own functions in the test body below, not hardcoded on either side.
struct ChannelVsFtFixture {
    Problem problem;
    Placement placement;
};

ChannelVsFtFixture makeChannelVsFtFixture() {
    ChannelVsFtFixture f;
    Problem& p = f.problem;
    p.outline_w = 80.0 + 700.0;  // A/B column (80 wide) + soft block S (700 wide) to the right
    p.outline_h = 610.0 + 100.0 + 100.0;  // A (bottom) + channel span 610 + B (top)
    p.alpha = 1.0;

    Block a = makeChannelBlock("A", 80.0, 100.0);
    Block b = makeChannelBlock("B", 80.0, 100.0);
    Block s;
    s.name = "S";
    s.width = 700.0;
    s.height = 810.0;
    s.area = 700.0 * 810.0;
    s.ft_base_area = 372949.0;  // official design-doc worked example base area
    s.type = BlockType::Soft;
    s.ft_rates = {0.2, 0.4, 0.8, 1.0};

    p.blocks = {a, b, s};
    p.block_index = {{"A", 0}, {"B", 1}, {"S", 2}};

    Bundle bun;
    bun.source = 0;
    bun.target = 1;
    bun.nets = 3600;
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

// Fixture (a) RED->GREEN: cost_driven=false must route A->B through the channel (legacy pass-0
// channels-only semantics), matching the T0 fixture's overflow exactly (39,040 um^2, since the
// channel geometry is identical). cost_driven=true must instead divert through S once the channel's
// marginal cost exceeds S's FT marginal tier delta -- asserted via soft_feedthrough_nets[S]>0 AND
// channel_overflow strictly lower than the non-cost-driven route of this same fixture.
// B4 re-baseline (design b4-design.md §5/§8(iv) forward direction; brief's ONE explicitly-named
// re-derivation): this fixture's FT-vs-channel CHOICE still goes the same way under official
// pricing (confirmed empirically via a scratch probe against the built router, matching this
// codebase's established practice for deriving fixture constants) -- but the reasoning and the
// post-cd numbers are now official-units, and the router does BETTER than before (drives overflow
// to exactly 0, not just "lower than legacy"), because it fills the channel to capacity for free
// (both-edges pricing is 0 while under cap) before diverting the remainder to FT. The OLD
// area-denominated cross-check (ft_marginal vs expected_legacy_overflow, both computed from
// ft.hpp/channelOverflowArea directly -- functions B4 does not touch) still holds unchanged and is
// kept; a NEW cross-check adds the OFFICIAL per-net exchange rate from design §5's worked formula:
// FT is chosen iff Delta(required area)/n < (20/3)*(A/C).
void testCostDrivenPrefersFtOverSaturatedChannel() {
    ChannelVsFtFixture f = makeChannelVsFtFixture();

    RoutingResult legacy = routeProblem(f.problem, f.placement, /*cost_driven=*/false);
    assert(legacy.open_routes == 0);
    double expected_legacy_overflow = ((7200.0 - 2000.0) / 25.0) * 610.0;  // 126,880 (per-edge doubling)
    assert(std::abs(legacy.channel_overflow - expected_legacy_overflow) < 1e-3);
    assert(legacy.soft_feedthrough_nets[2] == 0.0);  // legacy pass-0 never touches S

    // Hand-verify the FT-vs-channel decision this fixture is built to test, against ft.hpp directly
    // (OLD area-units cross-check -- ft.hpp/channelOverflowArea are untouched by B4, so this still
    // holds exactly as before).
    double rate = ftTierRate(f.problem.blocks[2], 3600.0);
    assert(std::abs(rate - 0.4) < kEps);  // confirms the 40% tier from the design-doc worked example
    double required = ftRequiredArea(f.problem.blocks[2].ft_base_area, ftExtend(3600.0, rate));
    double ft_marginal = required - f.problem.blocks[2].ft_base_area;  // S starts unexpanded (area==ft_base_area)
    assert(ft_marginal < expected_legacy_overflow);  // 36,261.34 < 39,040 -- FT must win this fixture

    RoutingResult cd = routeProblem(f.problem, f.placement, /*cost_driven=*/true);
    assert(cd.open_routes == 0);
    assert(cd.soft_feedthrough_nets[2] > 0.0);
    assert(cd.channel_overflow < legacy.channel_overflow);

    // NEW (design §5's official per-net exchange rate, using this fixture's OWN measured A/C so the
    // check is not a magic constant): A = declared outline area (fixed, geometric); C = RAW total
    // channel capacity (cd.channel_capacity_total, also fixed/geometric -- same on both sides).
    // FT wins iff its marginal area per net is below (20/3)*(A/C).
    double A = f.problem.outline_w * f.problem.outline_h;
    double per_net_breakeven = (20.0 / 3.0) * (A / cd.channel_capacity_total);
    assert((ft_marginal / 3600.0) < per_net_breakeven);

    // B4 measured (scratch-probe-derived, official units): the router now drives overflow to
    // EXACTLY 0 (not merely "lower than legacy") -- it fills the channel to capacity for free
    // (marginal pricing is 0 while under cap, design §3.2), THEN diverts the remainder to FT, so
    // neither resource ever overflows. S has generous physical slack (area=567,000 >> any required
    // area at this bundle size), so feedthrough_mismatch stays 0 too.
    assert(cd.channel_overflow == 0.0);
    assert(cd.channel_overflow_nets == 0.0);
    assert(cd.feedthrough_mismatch == 0.0);
    assert(std::abs(cd.soft_feedthrough_nets[2] - 2600.0) < kEps);
    assert(std::abs(cd.total_wirelength - 1533000.0) < 1.0);

    // The gate's own key (roundCostOfficial) must rank cd decisively ahead of legacy -- the SAME
    // key routeProblem's internal gate used to keep cd's own result (used_fallback==false).
    assert(!cd.used_fallback);
    auto key_legacy = roundCostOfficial(legacy, f.problem.alpha, A);
    auto key_cd = roundCostOfficial(cd, f.problem.alpha, A);
    assert(key_cd < key_legacy);

    std::cout << "testCostDrivenPrefersFtOverSaturatedChannel ok (legacy overflow=" << legacy.channel_overflow
               << ", cost_driven overflow=" << cd.channel_overflow << ", cd key=" << key_cd.second
               << ", legacy key=" << key_legacy.second << ")\n";
}

// B4 NEW (design b4-design.md §8(iv), reverse direction -- guards against "always-FT"): shrink the
// per-net exchange rate below breakeven by making the SAME soft block's FT growth much steeper
// (a much larger ft_base_area -- ft.hpp's required-area scales with sqrt(base_area), so a bigger
// base makes each net's incremental area cost bigger too), while leaving A/C (purely geometric,
// independent of ft_base_area) UNCHANGED. Confirmed via the same scratch-probe methodology: at
// ft_base_area=5e8 the router now prefers the CHANNEL outright (soft_feedthrough_nets[S]==0),
// proving the choice is a genuine exchange-rate comparison, not a fixed bias toward FT.
void testCostDrivenPrefersChannelWhenFtExpensiveUnderOfficialRate() {
    ChannelVsFtFixture f = makeChannelVsFtFixture();
    f.problem.blocks[2].ft_base_area = 5.0e8;  // same physical S, much steeper FT growth per net

    RoutingResult legacy = routeProblem(f.problem, f.placement, /*cost_driven=*/false);
    RoutingResult cd = routeProblem(f.problem, f.placement, /*cost_driven=*/true);
    assert(legacy.open_routes == 0);
    assert(cd.open_routes == 0);

    // Cross-check the official per-net rate directly: Delta(required)/n now EXCEEDS the breakeven
    // (20/3)*(A/C) -- the mirror image of the forward test's assertion.
    double acc = 0.0, n = 3600.0;
    double required = ftRequiredArea(f.problem.blocks[2].ft_base_area,
                                      ftExtend(acc + n, ftTierRate(f.problem.blocks[2], acc + n)));
    double ft_marginal = required - f.problem.blocks[2].ft_base_area;
    double A = f.problem.outline_w * f.problem.outline_h;
    double per_net_breakeven = (20.0 / 3.0) * (A / legacy.channel_capacity_total);
    assert((ft_marginal / n) > per_net_breakeven);

    // The router accepts channel overflow (rather than expensive FT) -- soft_feedthrough_nets[S]
    // stays exactly 0, proving the pricing genuinely compares the exchange rate rather than always
    // preferring FT once it is merely available.
    assert(cd.soft_feedthrough_nets[2] == 0.0);
    assert(cd.channel_overflow_nets > 0.0);  // it does overflow the channel -- accepting overflow is
                                              // cheaper than the now-expensive FT detour
    std::cout << "testCostDrivenPrefersChannelWhenFtExpensiveUnderOfficialRate ok (overflow_nets="
              << cd.channel_overflow_nets << ", used_fallback=" << cd.used_fallback << ")\n";
}

// Fixture (b): zero-WL direct-abut fixture (GC5) is unchanged under cost_driven=true -- two blocks
// placed side by side with no gap route with wirelength 0, no channel intermediate, no FT.
void testCostDrivenZeroWlDirectAbutUnchanged() {
    Problem p;
    p.outline_w = 160.0;
    p.outline_h = 100.0;
    p.alpha = 1.0;
    p.blocks = {makeChannelBlock("A", 80.0, 100.0), makeChannelBlock("B", 80.0, 100.0)};
    p.block_index = {{"A", 0}, {"B", 1}};
    Bundle bun;
    bun.source = 0;
    bun.target = 1;
    bun.nets = 500;
    p.bundles = {bun};

    Placement pl;
    pl.rects = {Rect{0.0, 0.0, 80.0, 100.0}, Rect{80.0, 0.0, 80.0, 100.0}};  // directly abutting
    pl.aspect = {1.0, 1.0};

    RoutingResult r = routeProblem(p, pl, /*cost_driven=*/true);
    assert(r.open_routes == 0);
    assert(r.routes.size() == 1);
    assert(r.routes[0].node_path.size() == 2);  // direct 2-node hop, no channel/soft intermediate
    assert(r.routes[0].wirelength == 0.0);
    assert(r.channel_overflow == 0.0);
    assert(r.feedthrough_mismatch == 0.0);
    std::cout << "testCostDrivenZeroWlDirectAbutUnchanged ok\n";
}

// Fixture (c): determinism -- cost_driven routing of the same placement decoded/routed twice
// yields identical routing (total_wirelength, overflow, and per-route node_paths), per GC6
// purity/determinism (routeProblem must be a pure function of (problem, placement), no hidden
// state leaking across calls).
void testCostDrivenDeterministic() {
    ChannelVsFtFixture f = makeChannelVsFtFixture();

    RoutingResult r1 = routeProblem(f.problem, f.placement, /*cost_driven=*/true);
    RoutingResult r2 = routeProblem(f.problem, f.placement, /*cost_driven=*/true);

    assert(r1.open_routes == r2.open_routes);
    assert(std::abs(r1.total_wirelength - r2.total_wirelength) < kEps);
    assert(std::abs(r1.channel_overflow - r2.channel_overflow) < kEps);
    assert(std::abs(r1.feedthrough_mismatch - r2.feedthrough_mismatch) < kEps);
    assert(r1.routes.size() == r2.routes.size());
    for (std::size_t i = 0; i < r1.routes.size(); ++i) {
        assert(r1.routes[i].node_path == r2.routes[i].node_path);
        assert(std::abs(r1.routes[i].wirelength - r2.routes[i].wirelength) < kEps);
    }
    std::cout << "testCostDrivenDeterministic ok\n";
}

// P2 Task 3 (design p2-design.md §5.1-5.4; plan Global Constraint 8 adaptive chunking): replace the
// fixed ceil(nets/1000) split with the greedy re-price-per-chunk loop in the cost_driven=true path
// ONLY (cost_driven=false must keep the exact legacy fixed split, verified in a fixture below).

// Fixture geometry: A (source, bottom) and B (target, top) separated by a single vertical channel of
// SPAN (rect.h) 120 and WIDTH (rect.w) 40 -> vcap = 25*40 = 1000 nets exactly. S (soft block) sits to
// the right, directly abutting both A and B (a zero-channel bypass, same "S bypass" shape as
// ChannelVsFtFixture above), with ft_base_area == its placed rect area (245,000 um^2, no starting
// slack) so the FT marginal mismatch is driven purely by ftRequiredArea growth. Hand-verified via the
// same channelMarginalCost/softFtMarginalCost math T1 already tests (not re-derived here): for a
// PROBE of the full remaining bundle (3000 nets) from empty state, the channel's marginal cost
// (9.6e9) is LESS than S's FT marginal cost (12.02e9) -- so Dijkstra picks the channel path and the
// greedy loop's bottleneck-slack read (vcap - usage = 1000 - 0 = 1000) correctly identifies the
// channel as the tightest resource on that path. After committing 1000 nets to the channel (now
// saturated at usage==vcap), a second probe of the remaining 2000 nets flips: channel marginal cost
// for 2000 more nets on an already-saturated channel (9.6e9) EXCEEDS S's FT marginal cost for the
// same 2000 nets (7.98e9) -- so the second chunk's Dijkstra diverts entirely through S. This is
// exactly the design §5.2 bottleneck-fill-then-divert behavior: chunk 1 fills the channel's slack to
// capacity, chunk 2 (and any further chunks) reprice against the now-saturated channel and take the
// now-cheaper alternative.
struct BottleneckFixture {
    Problem problem;
    Placement placement;
};

BottleneckFixture makeBottleneckFixture() {
    BottleneckFixture f;
    Problem& p = f.problem;
    p.outline_w = 40.0 + 700.0;  // A/B column (40 wide) + soft block S (700 wide) to the right
    p.outline_h = 100.0 + 120.0 + 100.0;  // A (bottom) + channel span 120 + B (top)
    p.alpha = 1.0;

    Block a = makeChannelBlock("A", 40.0, 100.0);
    Block b = makeChannelBlock("B", 40.0, 100.0);
    Block s;
    s.name = "S";
    s.width = 700.0;
    s.height = 350.0;
    s.area = 700.0 * 350.0;       // 245,000
    s.ft_base_area = 245000.0;    // matches placed rect area exactly -- zero starting slack
    s.type = BlockType::Soft;
    s.ft_rates = {0.2, 0.4, 0.8, 1.0};

    p.blocks = {a, b, s};
    p.block_index = {{"A", 0}, {"B", 1}, {"S", 2}};

    Bundle bun;
    bun.source = 0;
    bun.target = 1;
    bun.nets = 3000;
    p.bundles = {bun};

    Placement& pl = f.placement;
    pl.rects = {
        Rect{0.0, 0.0, 40.0, 100.0},     // A
        Rect{0.0, 220.0, 40.0, 100.0},   // B (channel gap y in [100,220], height 120)
        Rect{40.0, 0.0, 700.0, 350.0},   // S: touches A's right edge and B's right edge directly
    };
    pl.aspect = {1.0, 1.0, 1.0};
    return f;
}

// Fixture (a) RED->GREEN: cost_driven=false must reproduce the EXACT legacy fixed split for 3000
// nets: ceil(3000/1000)=3 chunks of (1000,1000,1000) -- this is the "legacy byte-identity" hard
// constraint (T2 reviewer-verified); adaptive chunking must NOT touch this path at all.
void testFixedSplitUnchangedForCostDrivenFalse() {
    BottleneckFixture f = makeBottleneckFixture();
    RoutingResult r = routeProblem(f.problem, f.placement, /*cost_driven=*/false);
    assert(r.open_routes == 0);
    assert(r.routes.size() == 3);
    assert(r.routes[0].nets == 1000);
    assert(r.routes[1].nets == 1000);
    assert(r.routes[2].nets == 1000);
    int total = 0;
    for (const auto& route : r.routes) total += route.nets;
    assert(total == 3000);
    std::cout << "testFixedSplitUnchangedForCostDrivenFalse ok\n";
}

// Fixture (b) the core RED->GREEN assertion: cost_driven=true must split ~(1000, 2000) -- the first
// chunk fills the channel's slack exactly to capacity (1000 nets), the second chunk diverts entirely
// through S's feedthrough once the channel is priced as saturated. Chunk count must be well under
// kMaxChunks=16, and total nets must be conserved. Each chunk is one Route (one PATH line, io.cpp
// already writes one line per route -- untouched).
// B4 RE-DERIVED (renamed from testAdaptiveChunkingSplitsAtBottleneck; not the ONE fixture design §8
// names by name, but a necessary collateral re-derivation -- flagged explicitly in the B4 report).
// This fixture's whole PRE-B4 point was the entry-axis-only pricing bug: the channel (vcap=1000)
// looked free/cheap to the OLD Dijkstra at usage=0 for the FULL 3000-net probe (1e6-area-denominated
// channelMarginalCost is 0 while under its own clamped-cap view), so the greedy loop filled it to
// 1000 nets before diverting the rest to FT. Under B4's OFFICIAL per-net rate (measured via a
// scratch probe against the built router, same methodology this file's own history already uses --
// see e.g. the Beta Task 3 "this fixture INVERTS" precedent above): both-edges pricing means even
// a SINGLE net of channel overflow already costs P_ovf on BOTH the entry and exit charge, which
// here dwarfs the cost of diverting the ENTIRE remaining bundle through FT -- so the very FIRST
// probe (priced at the full 3000 nets) already finds FT cheaper for the WHOLE bundle. The channel is
// never touched at all, and adaptive chunking degenerates to a single FT-only chunk. This is not a
// defect: it is B4 correctly recognizing that, for this bundle/channel/FT-cost combination,
// splitting at the bottleneck is no longer the cheapest option -- diverting everything is. The NEW
// fixture below (a fresh, minimal one, design §8 fixture (i)) is what now demonstrates the genuine
// "fill-to-cap-for-free, then divert the remainder" 2-chunk split B4 is designed to produce when the
// channel and FT rates are closer together.
void testBottleneckFixtureNowDivertsEntirelyToFtUnderOfficialPricing() {
    BottleneckFixture f = makeBottleneckFixture();
    RoutingResult r = routeProblem(f.problem, f.placement, /*cost_driven=*/true);
    assert(r.open_routes == 0);
    assert(!r.used_fallback);

    // Single chunk, all through FT -- the channel is never used at all.
    assert(r.routes.size() == 1);
    assert(r.routes[0].nets == 3000);
    assert(std::abs(r.soft_feedthrough_nets[2] - 3000.0) < kEps);

    bool found_channel = false;
    for (const auto& ch : r.channels) {
        if (std::abs(ch.rect.w - 40.0) < kEps && std::abs(ch.rect.h - 120.0) < kEps) {
            found_channel = true;
            assert(ch.horizontal_usage == 0.0);
            assert(ch.vertical_usage == 0.0);
        }
    }
    assert(found_channel);
    assert(r.channel_overflow == 0.0);
    assert(r.channel_overflow_nets == 0.0);
    // S has NO starting FT slack (ft_base_area==its placed area), so diverting the whole bundle
    // through it does grow required area past placed area -- a genuine, nonzero (but official-rate-
    // cheaper-than-the-channel) mismatch.
    assert(r.feedthrough_mismatch > 0.0);
    assert(std::abs(r.feedthrough_mismatch - 12023.39) < 1.0);
    assert(std::abs(r.total_wirelength - 330000.0) < 1.0);

    std::cout << "testBottleneckFixtureNowDivertsEntirelyToFtUnderOfficialPricing ok (mismatch="
               << r.feedthrough_mismatch << ")\n";
}

// B4 NEW (design b4-design.md §8 fixture (i)): straight pass-through both-edges pricing, the
// canonical case the design's Q-A/Q-B sections build the whole fix around. A minimal, purpose-built
// fixture (channel cap_H=100 exactly) rather than a re-tune of BottleneckFixture above, so the
// numbers are clean round ones traceable directly to design §4's own worked example (pathSlack
// halves 100 -> 50) instead of depending on a near-threshold coincidence between two competing
// rates. L (source) and R (target) sit side by side with a narrow horizontal gap between them
// (channel height 4 -> cap_H = 25*4 = 100); S is a soft block spanning the full width ABOVE both L
// and R (touching each directly, a zero-channel FT bypass), with NO starting slack
// (ft_base_area==its placed area) so any diverted nets are genuinely priced.
//
// Bundle = 100 nets, exactly filling cap_H if routed as one lump. B4: pathSlack (both-edges,
// design §4) returns 50 for the first probe (h_charges=2, (100-0)/2), so the greedy loop commits
// only 50 nets through the channel -- exactly saturating it (hu=100=cap, zero overflow). The
// remaining 50 nets reprice against the now-saturated channel (marginal cost > 0) vs. S's modest FT
// growth, and divert entirely to S. RED under the OLD (pre-B4) code: entry-axis-only pathSlack
// returns 100 (unhalved -- both old and new channelMarginalCost/marginalOverflowNets price a
// probe of exactly 100 nets against a cap of exactly 100 as FREE, so the OLD code's greedy loop
// commits the WHOLE bundle in one chunk since it sees no reason to split), so hu ends at 2*100=200,
// overflowing the cap by 100 nets that the OLD code never priced -- exactly design §2.2's overshoot.
void testStraightPassThroughBothEdgesPricingSaturatesChannelExactly() {
    Problem p;
    p.outline_w = 110.0;
    p.outline_h = 14.0;
    p.alpha = 1.0;
    Block l = makeChannelBlock("L", 50.0, 4.0);
    Block r = makeChannelBlock("R", 50.0, 4.0);
    Block s;
    s.name = "S";
    s.width = 110.0;
    s.height = 10.0;
    s.area = 110.0 * 10.0;
    s.ft_base_area = s.area;  // no starting slack
    s.type = BlockType::Soft;
    s.ft_rates = {0.2, 0.4, 0.8, 1.0};
    p.blocks = {l, r, s};
    p.block_index = {{"L", 0}, {"R", 1}, {"S", 2}};
    Bundle bun;
    bun.source = 0;
    bun.target = 1;
    bun.nets = 100;
    p.bundles = {bun};

    Placement pl;
    pl.rects = {
        Rect{0.0, 0.0, 50.0, 4.0},    // L: x=[0,50)
        Rect{60.0, 0.0, 50.0, 4.0},   // R: x=[60,110) -- gap x=[50,60), channel height 4 -> cap_H=100
        Rect{0.0, 4.0, 110.0, 10.0},  // S: spans the full width above both, direct FT bypass
    };
    pl.aspect = {1.0, 1.0, 1.0};

    RoutingResult r_ = routeProblem(p, pl, /*cost_driven=*/true);
    assert(r_.open_routes == 0);

    bool found_channel = false;
    for (const auto& ch : r_.channels) {
        if (std::abs(ch.rect.w - 10.0) < kEps && std::abs(ch.rect.h - 4.0) < kEps) {
            found_channel = true;
            // GREEN under B4: exactly saturated, never overshot.
            assert(ch.horizontal_usage <= 100.0 + kEps);
            assert(std::abs(ch.horizontal_usage - 100.0) < kEps);
        }
    }
    assert(found_channel);
    assert(r_.channel_overflow_nets == 0.0);
    assert(r_.channel_overflow == 0.0);

    // The split: 50 nets through the channel (fills it exactly), 50 diverted to S.
    assert(r_.routes.size() == 2);
    assert(r_.routes[0].nets == 50);
    assert(r_.routes[1].nets == 50);
    assert(std::abs(r_.soft_feedthrough_nets[2] - 50.0) < kEps);
    assert(r_.feedthrough_mismatch > 0.0);  // S has no starting slack -- diverted nets do cost something

    std::cout << "testStraightPassThroughBothEdgesPricingSaturatesChannelExactly ok (chunks="
              << r_.routes.size() << ")\n";
}

// Fixture (c): adaptive chunking must never emit more than kMaxChunks routes for a single bundle,
// even on a saturated-with-no-alternative bundle (no soft block reachable) that would otherwise want
// to keep re-probing forever. Two hard macros connected only by a small channel with no soft-block
// bypass; a huge bundle (50,000 nets) must still terminate in <=16 chunks (accepting graded overflow
// on the excess per design §5.2 step 3, since no cheaper split exists once saturated).
void testAdaptiveChunkingRespectsMaxChunksWithNoAlternative() {
    Problem p;
    p.outline_w = 40.0;
    p.outline_h = 100.0 + 120.0 + 100.0;
    p.alpha = 1.0;
    p.blocks = {makeChannelBlock("A", 40.0, 100.0), makeChannelBlock("B", 40.0, 100.0)};
    p.block_index = {{"A", 0}, {"B", 1}};
    Bundle bun;
    bun.source = 0;
    bun.target = 1;
    bun.nets = 50000;
    p.bundles = {bun};

    Placement pl;
    pl.rects = {Rect{0.0, 0.0, 40.0, 100.0}, Rect{0.0, 220.0, 40.0, 100.0}};
    pl.aspect = {1.0, 1.0};

    RoutingResult r = routeProblem(p, pl, /*cost_driven=*/true);
    assert(r.open_routes == 0);
    assert(r.routes.size() <= 16);
    int total = 0;
    for (const auto& route : r.routes) total += route.nets;
    assert(total == 50000);
    std::cout << "testAdaptiveChunkingRespectsMaxChunksWithNoAlternative ok (chunks=" << r.routes.size() << ")\n";
}

// Fixture (d): determinism of the adaptive-chunking path specifically (GC6 purity/determinism, same
// invariant T2's testCostDrivenDeterministic asserts for the single-Dijkstra path) -- the greedy loop
// introduces new call-local state (min_chunk, remaining, chunks_emitted) that must not leak across
// calls or depend on anything but (problem, placement); routing the same bottleneck fixture twice
// must yield identical chunk counts, per-chunk nets, node_paths, and aggregate totals.
void testAdaptiveChunkingDeterministic() {
    BottleneckFixture f = makeBottleneckFixture();

    RoutingResult r1 = routeProblem(f.problem, f.placement, /*cost_driven=*/true);
    RoutingResult r2 = routeProblem(f.problem, f.placement, /*cost_driven=*/true);

    assert(r1.routes.size() == r2.routes.size());
    for (std::size_t i = 0; i < r1.routes.size(); ++i) {
        assert(r1.routes[i].nets == r2.routes[i].nets);
        assert(r1.routes[i].node_path == r2.routes[i].node_path);
    }
    assert(std::abs(r1.channel_overflow - r2.channel_overflow) < kEps);
    assert(std::abs(r1.feedthrough_mismatch - r2.feedthrough_mismatch) < kEps);
    std::cout << "testAdaptiveChunkingDeterministic ok\n";
}

// P2 Task 4 (design p2-design.md §4.1-4.4; plan Global Constraint 9 negotiated rip-up/reroute):
// two bundles contending one narrow channel where an alternate slack path exists, but ONLY for the
// heavier bundle -- forcing genuine round-0 overflow that negotiation (history-driven reroute of
// the bundle that HAS an alternative) then clears.
//
// Topology (a "ladder" with a wall separating two channels, hand-derived and CONFIRMED against the
// real routeProblem via a scratch EFGR_ROUTE_TRACE harness before being transcribed here -- see the
// T4 report for the raw trace):
//   SRC_A (bottom rail, x=[60,161), y=[0,10)): touches BOTH the narrow shared channel (x=[100,140),
//     w=40 -> vcap=1000) AND the wide alternate channel (x=[161,300), w=139 -> vcap=3475, reached via
//     a deliberate 1-unit sliver overlap) -- its centroid sits much closer to the narrow channel, so
//     it is the cheaper pick whenever both are slack.
//   SRC_B (x=[60,100), y=[10,90)): touches ONLY the narrow shared channel (CAP_TOP seals its column
//     so no shortcut channel forms that would let it reach TGT for free, and TGT itself is shrunk to
//     x=[100,300) so it never touches SRC_B directly).
//   WALL (x=[140,160)) separates the two channels so they are not graph-adjacent to each other.
//   TGT (x=[100,300), y=[90,100)) touches both channels.
// Bundle A (900 nets, source=SRC_A) is heavier than bundle B (400 nets, source=SRC_B), so it is
// routed FIRST every round (design §4.1 nets-descending order). Round 0 (all-zero history): A finds
// the narrow channel cheaper (closer) than the alternate and takes all 900 nets there (900<1000, no
// overflow for A alone); B then has nowhere else to go (its ONLY reachable resource is the SAME
// narrow channel) and is forced through it too, pushing usage to 1300 > 1000 cap -- confirmed via
// trace: round 0 channel_overflow = ((1300-1000)/25)*80 = 960.0 um^2, open_routes=0. After round 0,
// the narrow channel ended over capacity, so history[narrow][vertical] is bumped
// (kHistoryBump*(1300-1000)/1000 = 0.3). Round 1 begins by decaying history (kHistoryDecay=0.5), so
// the LIVE value used when A is re-priced is 0.3*0.5 = 0.15; the resulting surcharge
// (kOverflowAreaWeight * 0.15 * 900 = 1.35e8) still dwarfs the small extra wireCost distance of the
// alternate channel (alpha=0.001 keeps the base term negligible against 1e6-scale area terms) -- A's
// cheapest path flips entirely to the alternate channel, freeing the narrow channel for B alone
// (400 < 1000 cap) -- confirmed via trace: round 1 channel_overflow = 0.0, and this round has zero
// bumps (any_bump=false) so the early-exit (design requirement 6) stops before round 2.
struct NegotiationFixture {
    Problem problem;
    Placement placement;
};

NegotiationFixture makeNegotiationFixture() {
    NegotiationFixture f;
    Problem& p = f.problem;
    p.outline_w = 300.0;
    p.outline_h = 100.0;
    p.alpha = 0.001;  // small but nonzero: distance differences matter without drowning area terms

    Block src_a = makeChannelBlock("SRC_A", 101.0, 10.0);
    Block src_b = makeChannelBlock("SRC_B", 40.0, 80.0);
    Block cap_top = makeChannelBlock("CAP_TOP", 40.0, 10.0);
    Block wall = makeChannelBlock("WALL", 20.0, 80.0);
    Block tgt = makeChannelBlock("TGT", 200.0, 10.0);

    p.blocks = {src_a, src_b, cap_top, wall, tgt};
    p.block_index = {{"SRC_A", 0}, {"SRC_B", 1}, {"CAP_TOP", 2}, {"WALL", 3}, {"TGT", 4}};

    Bundle bA;
    bA.source = 0;  // SRC_A
    bA.target = 4;  // TGT
    bA.nets = 900;
    Bundle bB;
    bB.source = 1;  // SRC_B
    bB.target = 4;  // TGT
    bB.nets = 400;
    p.bundles = {bA, bB};  // heavier bundle first is also the nets-descending order T4 requires

    Placement& pl = f.placement;
    pl.rects = {
        Rect{60.0, 0.0, 101.0, 10.0},    // SRC_A: x=[60,161)
        Rect{60.0, 10.0, 40.0, 80.0},    // SRC_B: x=[60,100), y=[10,90)
        Rect{60.0, 90.0, 40.0, 10.0},    // CAP_TOP: x=[60,100), y=[90,100)
        Rect{140.0, 10.0, 20.0, 80.0},   // WALL: x=[140,160)
        Rect{100.0, 90.0, 200.0, 10.0},  // TGT: x=[100,300)
    };
    pl.aspect.assign(pl.rects.size(), 1.0);
    return f;
}

// Fixture (a) the core RED->GREEN assertion: negotiated cost_driven=true routing must reach
// channel_overflow == 0 (strictly below round 0's provably-nonzero 960.0, see the fixture comment
// above), by rerouting bundle A off the contended channel once history prices it out in round 1.
void testNegotiationClearsRoundZeroOverflow() {
    NegotiationFixture f = makeNegotiationFixture();
    RoutingResult r = routeProblem(f.problem, f.placement, /*cost_driven=*/true);
    assert(r.open_routes == 0);

    // The honesty check (documented in the T4 report): with kNegotiationRounds temporarily forced
    // to 1, this exact assertion fails (best-round == round 0's 960.0 um^2 overflow) -- proving the
    // test actually exercises multi-round negotiation, not merely round 0's adaptive chunking.
    assert(r.channel_overflow == 0.0);
    assert(r.feedthrough_mismatch == 0.0);

    // Confirm the MECHANISM, not just the aggregate number: bundle A must have vacated the narrow
    // shared channel (vcap=1000, w=40,h=80) for the wide alternate (vcap=3475, w=139,h=90) once
    // history priced the shared one out, leaving the shared channel at exactly bundle B's 400 nets.
    bool found_narrow = false, found_alt = false;
    for (const auto& ch : r.channels) {
        if (std::abs(ch.rect.w - 40.0) < kEps && std::abs(ch.rect.h - 80.0) < kEps) {
            found_narrow = true;
            assert(std::abs(ch.vertical_usage - 400.0) < kEps);
        }
        if (std::abs(ch.rect.w - 139.0) < kEps && std::abs(ch.rect.h - 90.0) < kEps) {
            found_alt = true;
            assert(std::abs(ch.vertical_usage - 900.0) < kEps);
        }
    }
    assert(found_narrow);
    assert(found_alt);

    // P2 Task 5.1 (controller amendment, comparison gate): this fixture's whole point is that
    // negotiation clears round 0's overflow entirely (960 um^2 -> 0), something legacy's non-
    // negotiated single pass cannot do (legacy has no rip-up/reroute; it is stuck with round-0-
    // equivalent behavior on this contended channel). The new gate must NOT flip this: cost-driven's
    // fully-negotiated, zero-overflow result must still beat (or at worst tie) legacy's own run, so
    // the fallback must not have engaged here.
    assert(!r.used_fallback);
    std::cout << "testNegotiationClearsRoundZeroOverflow ok (overflow=" << r.channel_overflow << ")\n";
}

// Fixture (b): determinism -- two calls of the negotiated router on the identical (problem,
// placement) must be bit-identical (design §4.4/§6, GC6 purity: history/usage/order are all
// call-local, no static/global/RNG). Checks node_paths, per-route wirelength, and the aggregate
// overflow/mismatch/open_routes fields element-wise, matching the T2/T3 determinism fixtures' shape.
void testNegotiatedRoutingDeterministic() {
    NegotiationFixture f = makeNegotiationFixture();

    RoutingResult r1 = routeProblem(f.problem, f.placement, /*cost_driven=*/true);
    RoutingResult r2 = routeProblem(f.problem, f.placement, /*cost_driven=*/true);

    assert(r1.open_routes == r2.open_routes);
    assert(std::abs(r1.total_wirelength - r2.total_wirelength) < kEps);
    assert(std::abs(r1.channel_overflow - r2.channel_overflow) < kEps);
    assert(std::abs(r1.feedthrough_mismatch - r2.feedthrough_mismatch) < kEps);
    assert(r1.routes.size() == r2.routes.size());
    for (std::size_t i = 0; i < r1.routes.size(); ++i) {
        assert(r1.routes[i].node_path == r2.routes[i].node_path);
        assert(r1.routes[i].nets == r2.routes[i].nets);
        assert(std::abs(r1.routes[i].wirelength - r2.routes[i].wirelength) < kEps);
    }
    std::cout << "testNegotiatedRoutingDeterministic ok\n";
}

// P2 Task 5 (design p2-design.md §9 T5; plan Global Constraint 10 comparison gate/feasibility
// fallback): "never emit an open route" guarantee. `findRoutePath`'s reachability gates
// (`allowed`/`endpointEdgeAllowed`, routing.cpp) are IDENTICAL between the legacy two-pass router's
// pass 1 (channels + soft blocks, allow_soft=true) and cost_driven's single unified pass
// (allow_soft=true unconditionally) -- same node set, same port-edge checks, same non-negative edge
// costs -- so Dijkstra reachability (path exists vs. not) can never differ between the two: a bundle
// unroutable by cost_driven is, by construction, EQUALLY unroutable by legacy, and vice versa. That
// makes "legacy succeeds, cost-driven fails" NOT constructible (confirmed against the running code,
// not just by inspection -- see the T5 report). Per the brief's fallback clause, this is instead a
// CHARACTERIZATION fixture: a genuine topology dead end (unroutable by BOTH), used to exercise the
// fallback CODE PATH and prove `routeProblem(...,true)` is never worse than `routeProblem(...,false)`.
//
// P2 Task 5.1 update (controller amendment, full comparison gate): this fixture's "ok" bundle (A->B)
// routes identically under both algorithms (same unique channel, zero congestion at 10 nets), so its
// contribution to the lexicographic (open_routes, cost) key is IDENTICAL on both sides; the "dead"
// bundle (open on both sides) contributes only to the open_routes count (also identical: 1 vs 1), not
// to the area-cost term. The gate's key is therefore an EXACT TIE, and per the gate's tie rule (ties
// go to the cost-driven result, routing.cpp's comparison gate), `routeProblem(...,true)` now returns
// its OWN `best` here, NOT legacy substituted wholesale -- `used_fallback` is false for this fixture,
// a DIFFERENT outcome than the original T5-only fallback (which engaged unconditionally on
// best.open_routes>0). This is the documented, deterministic tie-break outcome, verified against the
// running code (not merely asserted): both `cd` and `legacy` end up numerically equal on every
// aggregate field (so those equality assertions below still hold either way), but the ONE field that
// distinguishes "cost-driven's own open-route bookkeeping" from "legacy's" -- `Route::nets` on the
// dead bundle's open route (legacy's `routeChunk` leaves it at the default 0; cost-driven's
// exhausted-probe fallthrough explicitly sets `probe.nets = remaining`) -- now reveals `cd` is
// genuinely cost-driven's OWN result, not a substituted copy.
//
// Layout (single row, height 40, outline 200x40): A[0,40) -- CH[40,60) -- B[60,100) -- WALL[100,140)
// -- D_SRC[140,160) -- D_TGT[160,200). A and B are channel-connected only via the [40,60) gap; B,
// WALL, D_SRC, D_TGT tile the rest of the row with zero gaps, so D_SRC/D_TGT form an isolated pocket
// reachable only from EACH OTHER, by direct abut on D_TGT's LEFT edge (edge code 1, `edgeFromTo`).
// D_TGT.port_edges = {2} (TOP only) excludes that edge code entirely, so no traversable edge into
// D_TGT exists anywhere in the graph -- a genuine, port-blocked dead end (brief's suggested
// construction: "all finite-cost edges port-blocked").
struct DeadEndFixture {
    Problem problem;
    Placement placement;
};

DeadEndFixture makeDeadEndFixture() {
    DeadEndFixture f;
    Problem& p = f.problem;
    p.outline_w = 200.0;
    p.outline_h = 40.0;
    p.alpha = 1.0;

    Block a = makeChannelBlock("A", 40.0, 40.0);
    Block b = makeChannelBlock("B", 40.0, 40.0);
    Block wall = makeChannelBlock("WALL", 40.0, 40.0);
    Block d_src = makeChannelBlock("D_SRC", 20.0, 40.0);
    Block d_tgt = makeChannelBlock("D_TGT", 40.0, 40.0);
    d_tgt.port_edges = {2};  // TOP only -- excludes D_SRC's LEFT-edge (code 1) touch entirely

    p.blocks = {a, b, wall, d_src, d_tgt};
    p.block_index = {{"A", 0}, {"B", 1}, {"WALL", 2}, {"D_SRC", 3}, {"D_TGT", 4}};

    Bundle ok;
    ok.source = 0;  // A
    ok.target = 1;  // B
    ok.nets = 10;
    Bundle dead;
    dead.source = 3;  // D_SRC
    dead.target = 4;  // D_TGT
    dead.nets = 10;
    p.bundles = {ok, dead};

    Placement& pl = f.placement;
    pl.rects = {
        Rect{0.0, 0.0, 40.0, 40.0},    // A: x=[0,40)
        Rect{60.0, 0.0, 40.0, 40.0},   // B: x=[60,100)
        Rect{100.0, 0.0, 40.0, 40.0},  // WALL: x=[100,140)
        Rect{140.0, 0.0, 20.0, 40.0},  // D_SRC: x=[140,160)
        Rect{160.0, 0.0, 40.0, 40.0},  // D_TGT: x=[160,200)
    };
    pl.aspect.assign(pl.rects.size(), 1.0);
    return f;
}

void testFeasibilityFallbackNeverWorseThanLegacyOnGenuineDeadEnd() {
    DeadEndFixture f = makeDeadEndFixture();

    RoutingResult legacy = routeProblem(f.problem, f.placement, /*cost_driven=*/false);
    // Establish genuineness: the dead end is unroutable by the pre-P2 baseline too (both passes
    // share the same port-edge gate).
    assert(legacy.open_routes == 1);

    RoutingResult cd = routeProblem(f.problem, f.placement, /*cost_driven=*/true);
    // T5's never-worse guarantee: cost-driven's negotiation loop hits the SAME dead end every round
    // (best.open_routes stays 1 for all kNegotiationRounds), so open_routes can never be worse than
    // legacy's own open-route count -- true regardless of which side the T5.1 gate ultimately picks.
    assert(cd.open_routes == legacy.open_routes);

    // Aggregate fields match legacy's numerically on this fixture (see the updated fixture-level
    // comment above): NOT because the gate substituted legacy wholesale here (it did not -- see the
    // used_fallback/nets checks below), but because the "ok" bundle routes identically either way and
    // the "dead" bundle contributes nothing to these fields on either side.
    assert(cd.routes.size() == legacy.routes.size());
    assert(std::abs(cd.total_wirelength - legacy.total_wirelength) < kEps);
    assert(std::abs(cd.channel_overflow - legacy.channel_overflow) < kEps);
    assert(std::abs(cd.feedthrough_mismatch - legacy.feedthrough_mismatch) < kEps);

    // P2 Task 5.1 (controller amendment): the gate's key is an EXACT TIE here (open_routes 1==1, area
    // cost equal per the fixture comment), and ties go to the cost-driven result -- so the fallback
    // must NOT have engaged for this fixture, unlike the original T5-only (open-routes-triggers-
    // unconditionally) behavior. This is the "documented outcome" the gate's tie rule guarantees
    // deterministically, verified against the running code (see the nets-field check below).
    assert(!cd.used_fallback);

    // The co-existing OK bundle (A->B) still routed successfully in both -- the fallback didn't
    // collateral-damage an otherwise-healthy bundle.
    int ok_routes_legacy = 0, ok_routes_cd = 0;
    for (const auto& r : legacy.routes) {
        if (r.source == 0 && r.target == 1) {
            assert(!r.open);
            ++ok_routes_legacy;
        }
    }
    for (const auto& r : cd.routes) {
        if (r.source == 0 && r.target == 1) {
            assert(!r.open);
            ++ok_routes_cd;
        }
    }
    assert(ok_routes_legacy > 0);
    assert(ok_routes_cd == ok_routes_legacy);

    // Decisive TIE-BREAK check on the DEAD bundle's own open route (P2 Task 5.1 update -- this
    // assertion's expected outcome FLIPPED from the original T5-only test): legacy's `findRoutePath`
    // intentionally leaves an open route's `.nets` at its 0 default (routing.cpp's "route.nets is
    // intentionally NOT set here" comment -- `routeChunk` returns it as-is on an empty node_path,
    // never reaching `commitRoutePath`). The negotiation loop's OWN exhausted-probe fallthrough, by
    // contrast, explicitly sets `probe.nets = remaining` before pushing it. Since the gate's tie rule
    // (ties go to cost-driven, asserted above via `!cd.used_fallback`) means `cd` here IS genuinely
    // cost-driven's own `best` -- NOT legacy substituted -- `cd_dead->nets` must equal cost-driven's
    // OWN convention (10, the bundle's full remaining nets), disagreeing with legacy's 0 default. This
    // is the one field that proves the tie-break actually kept cost-driven's result, not a copy of
    // legacy's: it is the mirror image of the original T5 test's wholesale-substitution proof.
    const Route* legacy_dead = nullptr;
    const Route* cd_dead = nullptr;
    for (const auto& r : legacy.routes) {
        if (r.source == 3 && r.target == 4) legacy_dead = &r;
    }
    for (const auto& r : cd.routes) {
        if (r.source == 3 && r.target == 4) cd_dead = &r;
    }
    assert(legacy_dead != nullptr && cd_dead != nullptr);
    assert(legacy_dead->open && cd_dead->open);
    assert(legacy_dead->nets == 0);
    assert(cd_dead->nets == 10);
    assert(cd_dead->nets != legacy_dead->nets);

    std::cout << "testFeasibilityFallbackNeverWorseThanLegacyOnGenuineDeadEnd ok (open_routes="
              << cd.open_routes << ")\n";
}

// Beta Task 3 note: under per-edge channel demand this fixture INVERTS -- the 900-net straight
// filler now overflows CH1 (1800 > vcap 1000) on BOTH sides, so cost-driven's elbow diversion makes
// it the WINNER (used_fallback=false); the numbers below describe the pre-per-edge single-charge
// model.
//
// P2 Task 5.1 (design p2-design.md §9 T5 preamble; controller amendment -- FULL comparison gate).
// The binding design text mandates routeProblem return "the cost-driven result when it is fully
// routed AND NO WORSE, else the fallback" -- a full lexicographic cost comparison, not merely T5's
// original open-routes-only fallback. The fixture below is the RED->GREEN case that only the full
// gate (not the open-routes check alone) can pass: cost_driven is FULLY ROUTED (open_routes==0 on
// BOTH sides -- the old T5 fallback would never even look at this fixture) yet LOSES the (open_routes,
// cost) key to legacy, so the gate must still switch.
//
// The loss is constructed from a DOCUMENTED gap in the cost-driven Dijkstra pricing itself
// (routing.cpp's findRoutePath channel-hop comment: "Pricing itself... uses only this single
// entry-edge axis as an admissible one-sided proxy"): a bundle that ENTERS a channel on one axis and
// EXITS it on the orthogonal axis (an "elbow") accrues usage on BOTH axes at commit time, but the
// Dijkstra search only ever prices the ENTRY axis. Geometry: CH1 (the gap extracted between
// SRC1/TGT1, rect w=40,h=610 -> vcap=25*40=1000, hcap=25*610=15250) is filled to vertical_usage=900
// by a straight VERTICAL filler bundle (SRC1->TGT1, entering AND exiting CH1 on the vertical axis --
// correctly priced, no overflow alone). SRC2 sits to CH1's right at mid-height (x=[40,80),
// y=[300,320)); a second bundle SRC2->TGT1 (300 nets) can reach TGT1 either (1) via an ELBOW through
// CH1 -- entering HORIZONTALLY from SRC2 (CH1's horizontal_usage=0, hcap huge, so the search prices
// this hop as ~free, blind to CH1's ALREADY-high vertical_usage=900 since only the entry axis is
// charged) then exiting vertically to TGT1 -- or (2) via CH4 (extractChannels' gap directly above
// SRC2 in SRC2's own x-strip, x=[40,80) y=[320,650), uncongested and geometrically the shorter path
// in this layout). Cost-driven's Dijkstra takes the mispriced-cheap elbow through CH1; committing it
// accrues 300 nets onto BOTH ch.horizontal_usage (300, fine, hcap=15250) AND ch.vertical_usage
// (900+300=1200, exceeding vcap=1000 by 200) -- an overflow the search never priced. Legacy's two-pass
// router (routing.cpp's `else` edge-cost branch: `10000*nets*(horizontal_usage/hcap +
// vertical_usage/vcap)`) sums BOTH axes' CURRENT usage regardless of entry direction, so it correctly
// sees CH1's existing vertical_usage=900 and detours to CH4 instead -- zero overflow, and (this
// layout's x-strips happen to make CH4 the shorter path too) even slightly lower wirelength.
//
// Hand-derived key values (both open_routes==0, so the AREA-cost term decides; confirmed against the
// running code via a scratch probe before being transcribed here):
//   cost-driven (round 0, unchanged across all kNegotiationRounds -- no cheaper alternative is ever
//   discovered by the negotiation history mechanism for this single-shot elbow mispricing):
//     wl=648,000; overflow=4,880 um^2 (missing_width=(1200-1000)/25=8, area=8*610=4,880); mismatch=0
//     key = alpha*wl + kOverflowAreaWeight*overflow = 0.001*648000 + 1e6*4880 = 4,880,000,648
//   legacy: wl=646,500; overflow=0; mismatch=0 -> key = 0.001*646500 = 646.5
// Legacy's key is smaller by more than 7 orders of magnitude (the 1e6-weighted overflow term
// dominates), so the gate MUST return legacy's result wholesale with used_fallback=true, despite
// cost-driven having found a fully-routed solution -- exactly the amendment's target gap.
struct ElbowMispricingFixture {
    Problem problem;
    Placement placement;
};

// `elbow_nets` (Beta Task 3 review Finding 1 fix, default 300 = this fixture's original value used
// by testComparisonGateKeepsCostDrivenWhenItWins/testComparisonGateDeterministic below): scaling the
// elbow bundle's own net count scales cost-driven's blind self-overflow on CH1 (its exit hop into
// TGT1 is never priced, see routing.cpp:231-239) roughly linearly, while legacy's overflow stays
// pinned at 19,520 um^2 (legacy always detours the elbow via CH4 entirely, so only the filler's own
// unavoidable pass-through ever touches CH1). Swept empirically against the compiled router: cost-
// driven still narrowly self-wins at elbow_nets=850 (self-overflow 18,300 < legacy's 19,520); by 900
// the two cross and used_fallback flips true. testComparisonGateSwitchesToLegacyWhenCostDrivenFully
// RoutedButWorse below uses elbow_nets=1000, comfortably past that crossover.
ElbowMispricingFixture makeElbowMispricingFixture(int elbow_nets = 300) {
    ElbowMispricingFixture f;
    Problem& p = f.problem;
    p.outline_w = 200.0;
    p.outline_h = 650.0;
    p.alpha = 0.001;  // small but nonzero (matches T4's fixture): WL differences must not drown the
                       // 1e6-scale area terms, but must still be large enough to be hand-traceable.

    Block src1 = makeChannelBlock("SRC1", 40.0, 10.0);
    Block tgt1 = makeChannelBlock("TGT1", 40.0, 10.0);
    Block src2 = makeChannelBlock("SRC2", 40.0, 20.0);

    p.blocks = {src1, tgt1, src2};
    p.block_index = {{"SRC1", 0}, {"TGT1", 1}, {"SRC2", 2}};

    Bundle filler;  // straight vertical: fills CH1's vertical_usage to 900 (< vcap=1000, no overflow
                     // alone), entering/exiting CH1 on the vertical axis both ends (correctly priced).
    filler.source = 0;
    filler.target = 1;
    filler.nets = 900;
    Bundle elbow;  // SRC2 -> TGT1: cost-driven takes the mispriced CH1 elbow; legacy detours via CH4.
    elbow.source = 2;
    elbow.target = 1;
    elbow.nets = elbow_nets;
    p.bundles = {filler, elbow};

    Placement& pl = f.placement;
    pl.rects = {
        Rect{0.0, 0.0, 40.0, 10.0},      // SRC1: x=[0,40) y=[0,10)
        Rect{0.0, 620.0, 40.0, 10.0},    // TGT1: x=[0,40) y=[620,630)
        Rect{40.0, 300.0, 40.0, 20.0},   // SRC2: x=[40,80) y=[300,320)
    };
    pl.aspect.assign(pl.rects.size(), 1.0);
    return f;
}

// Beta Task 3 review renamed this from testComparisonGatePrefersLegacyWhenCostDrivenFullyRoutedBut
// Worse: per-edge accounting inverted the fixture's outcome (see makeElbowMispricingFixture's comment
// above) so it now asserts cost-driven WINS, not legacy -- the name must describe what the test
// actually checks. testComparisonGateSwitchesToLegacyWhenCostDrivenFullyRoutedButWorse below (new)
// restores coverage of the used_fallback=true branch this test's old name promised but no longer
// exercised.
//
// B4 RE-DERIVED (design b4-design.md §8 fixture (ii) L-turn coverage): this fixture IS an L-turn
// scenario -- CH1's V-axis is pre-saturated by the 900-net straight filler, and the elbow bundle
// (SRC2->TGT1) enters CH1 HORIZONTALLY then would exit VERTICALLY if it took CH1 at all (an L-turn
// through the same channel), vs. a straight alternative via the uncongested CH4. The OLD entry-
// axis-only pricing (design §2.2) primed only the H axis for this elbow, never seeing CH1's already-
// saturated V axis. Under B4's both-edges pricing, that V-axis marginal is now correctly counted
// (design §3.3), so the negotiated router avoids creating ANY overflow at all here (measured via a
// scratch probe: overflow now 0, not the pre-B4 code's residual 4,880 um^2) -- an even stronger
// result than before, because pathSlack's halving (design §4) ALSO stops the filler itself from
// blindly overshooting CH1's cap the way the old single-charge slack view did.
void testComparisonGateKeepsCostDrivenWhenItWins() {
    ElbowMispricingFixture f = makeElbowMispricingFixture();

    RoutingResult r = routeProblem(f.problem, f.placement, /*cost_driven=*/true);
    // Both sides are FULLY ROUTED -- the original T5 open-routes-only fallback would never have
    // engaged here (nothing was open). This fixture's whole point is that the amendment's full
    // AREA-cost comparison must still catch and correct this case.
    assert(r.open_routes == 0);
    assert(!r.used_fallback);  // cost-driven wins, so the gate keeps cost-driven's own result

    // Non-substitution proof: r must DIFFER from a direct legacy call on the decisive fields --
    // proving the gate genuinely kept cost-driven's strictly-better result rather than substituting
    // legacy wholesale.
    RoutingResult legacy = routeProblem(f.problem, f.placement, /*cost_driven=*/false);
    assert(r.open_routes == legacy.open_routes);                    // both fully routed (0 == 0)
    assert(r.total_wirelength < legacy.total_wirelength - kEps);    // cost-driven WL strictly lower
    assert(r.channel_overflow < legacy.channel_overflow - kEps);    // cost-driven overflow strictly lower

    // Legacy pricing is UNCHANGED by B4 -- these numbers still hold exactly as before. The 900-net
    // straight vertical filler alone overshoots CH1 (2*900=1800 > vcap=1000) on BOTH sides regardless
    // of the elbow bundle's routing choice, so legacy never achieves zero overflow here.
    assert(std::abs(legacy.channel_overflow - 19520.0) < 1.0);
    assert(std::abs(legacy.total_wirelength - 625500.0) < 1.0);

    // B4 measured (scratch-probe-derived, official units): cost-driven now drives overflow to
    // EXACTLY 0 (a stronger result than the pre-B4 code's residual 4,880 um^2), and the gate's OWN
    // key (roundCostOfficial) ranks it decisively ahead of legacy.
    assert(r.channel_overflow == 0.0);
    assert(r.channel_overflow_nets == 0.0);
    assert(r.feedthrough_mismatch == 0.0);
    assert(std::abs(r.total_wirelength - 572500.0) < 1.0);
    double A = f.problem.outline_w * f.problem.outline_h;
    auto key_legacy = roundCostOfficial(legacy, f.problem.alpha, A);
    auto key_r = roundCostOfficial(r, f.problem.alpha, A);
    assert(key_r < key_legacy);

    std::cout << "testComparisonGateKeepsCostDrivenWhenItWins ok (cd_overflow="
              << r.channel_overflow << ", legacy_wl=" << legacy.total_wirelength
              << ", legacy_overflow=" << legacy.channel_overflow << ")\n";
}

// P2 Task 5.1 legacy-switch coverage (Beta Task 3 review Finding 1): the fixture's DEFAULT elbow_nets
// (300, above) no longer exercises the used_fallback=true branch under per-edge accounting -- see
// testComparisonGateKeepsCostDrivenWhenItWins.
//
// B4 RE-DERIVED: B4's official pricing is now EFFECTIVE ENOUGH at this fixture's scale that
// cost-driven's own negotiated result keeps winning outright even at elbow_nets=1000 (measured via
// a scratch probe: used_fallback stays false all the way up through 10,000 -- B4 does not merely
// shrink this fixture's old margin, it eliminates the bug class the fixture was built to
// characterize, for any elbow size in that range). This branch's only remaining trigger in this
// fixture family, found by sweeping elbow_nets empirically (same "confirmed via a scratch probe
// against the compiled router" discipline the surrounding fixtures already use), is a much larger
// elbow (50,000 nets) where BOTH channels the filler/elbow can use saturate heavily regardless of
// pricing model, and cost-driven's own greedy/history-priced choices end up a hair worse than
// simply running legacy once.
void testComparisonGateSwitchesToLegacyWhenCostDrivenFullyRoutedButWorse() {
    ElbowMispricingFixture f = makeElbowMispricingFixture(/*elbow_nets=*/50000);

    RoutingResult r = routeProblem(f.problem, f.placement, /*cost_driven=*/true);
    RoutingResult legacy = routeProblem(f.problem, f.placement, /*cost_driven=*/false);

    // Both sides fully routed -- this exercises the FULL cost comparison, not the older T5
    // open-routes-only fallback (which would never engage here since nothing is open on either side).
    assert(r.open_routes == 0);
    assert(legacy.open_routes == 0);
    assert(r.used_fallback);  // the branch this test exists to cover

    // The gate must return the legacy pass's result WHOLESALE (not a blend): decisive metrics are
    // bit-identical to a direct legacy call on the same (problem, placement) -- probe-confirmed
    // values total_wirelength=15,111,500, channel_overflow(area)=988,720, overflow_nets=76,300,
    // feedthrough_mismatch=0, routes.size()=9.
    assert(r.open_routes == legacy.open_routes);
    assert(std::abs(r.total_wirelength - legacy.total_wirelength) < kEps);
    assert(std::abs(r.channel_overflow - legacy.channel_overflow) < kEps);
    assert(std::abs(r.feedthrough_mismatch - legacy.feedthrough_mismatch) < kEps);
    assert(r.routes.size() == legacy.routes.size());
    assert(std::abs(r.total_wirelength - 15111500.0) < 1.0);
    assert(std::abs(r.channel_overflow - 988720.0) < 1.0);
    assert(std::abs(r.channel_overflow_nets - 76300.0) < 1.0);

    // Determinism (same discipline as testComparisonGateDeterministic below): two cost_driven=true
    // calls on the identical (problem, placement) must be bit-identical, including used_fallback.
    RoutingResult r2 = routeProblem(f.problem, f.placement, /*cost_driven=*/true);
    assert(r2.used_fallback == r.used_fallback);
    assert(std::abs(r2.total_wirelength - r.total_wirelength) < kEps);
    assert(std::abs(r2.channel_overflow - r.channel_overflow) < kEps);

    std::cout << "testComparisonGateSwitchesToLegacyWhenCostDrivenFullyRoutedButWorse ok (wl="
              << r.total_wirelength << ", overflow=" << r.channel_overflow
              << ", used_fallback=" << r.used_fallback << ")\n";
}

// Determinism of the comparison gate itself (GC6 purity, same invariant every other P2 fixture
// asserts): two calls on the identical (problem, placement) must be bit-identical, including the
// NEW `used_fallback` telemetry field.
void testComparisonGateDeterministic() {
    ElbowMispricingFixture f = makeElbowMispricingFixture();

    RoutingResult r1 = routeProblem(f.problem, f.placement, /*cost_driven=*/true);
    RoutingResult r2 = routeProblem(f.problem, f.placement, /*cost_driven=*/true);

    assert(r1.used_fallback == r2.used_fallback);
    assert(r1.open_routes == r2.open_routes);
    assert(std::abs(r1.total_wirelength - r2.total_wirelength) < kEps);
    assert(std::abs(r1.channel_overflow - r2.channel_overflow) < kEps);
    assert(std::abs(r1.feedthrough_mismatch - r2.feedthrough_mismatch) < kEps);
    assert(r1.routes.size() == r2.routes.size());
    std::cout << "testComparisonGateDeterministic ok\n";
}

// P2 Task 6 Rider A (design p2-design.md §9 T6; T4/T5.1 review action item): round-consistency check
// for T6's default-on flip. The comparison gate (T5.1) and negotiation loop (T4) select a single
// "winning" RoutingResult (a negotiated round or the legacy fallback) and copy it wholesale -- but a
// bug that mixed fields from DIFFERENT rounds/branches (e.g. `soft_feedthrough_nets` from one round's
// accrual while `feedthrough_mismatch` was computed against another) would not be caught by any
// existing single-field assertion. This fixture uses a soft block with NO starting FT slack (unlike
// ChannelVsFtFixture's S, whose generous placed area keeps feedthrough_mismatch at exactly 0 --
// deliberately different here so the cross-check below is a nontrivial nonzero equality, not 0==0),
// on a contended A->B flow whose channel saturates before the full bundle fits (vcap=25*80=2000 <
// bundle.nets=3600), forcing the router to divert the overflow through S.
struct SoftFtRoundConsistencyFixture {
    Problem problem;
    Placement placement;
};

SoftFtRoundConsistencyFixture makeSoftFtRoundConsistencyFixture() {
    SoftFtRoundConsistencyFixture f;
    Problem& p = f.problem;
    p.outline_w = 80.0 + 200.0;          // A/B column (80 wide) + soft block S (200 wide) to the right
    p.outline_h = 610.0 + 100.0 + 100.0;  // A (bottom) + channel span 610 + B (top), same geometry as
                                          // the T0 overflow fixture above (vcap = 25*80 = 2000).
    p.alpha = 1.0;

    Block a = makeChannelBlock("A", 80.0, 100.0);
    Block b = makeChannelBlock("B", 80.0, 100.0);
    Block s;
    s.name = "S";
    s.width = 200.0;
    s.height = 810.0;
    s.area = 200.0 * 810.0;
    s.ft_base_area = s.area;  // NO starting slack: any nets diverted through S immediately grow the
                               // required area past the placed area, so feedthrough_mismatch is
                               // nonzero whenever soft_feedthrough_nets[S] is nonzero.
    s.type = BlockType::Soft;
    s.ft_rates = {0.2, 0.4, 0.8, 1.0};

    p.blocks = {a, b, s};
    p.block_index = {{"A", 0}, {"B", 1}, {"S", 2}};

    Bundle bun;
    bun.source = 0;
    bun.target = 1;
    bun.nets = 3600;  // 1600 nets exceed the channel's 2000-net capacity -- the router must divert
                       // the excess (or all of it, depending on marginal pricing) through S.
    p.bundles = {bun};

    Placement& pl = f.placement;
    pl.rects = {
        Rect{0.0, 0.0, 80.0, 100.0},     // A
        Rect{0.0, 710.0, 80.0, 100.0},   // B
        Rect{80.0, 0.0, 200.0, 810.0},   // S: touches A's right edge and B's right edge directly
    };
    pl.aspect = {1.0, 1.0, 1.0};
    return f;
}

void testRoundConsistencyOfSoftFeedthroughFields() {
    SoftFtRoundConsistencyFixture f = makeSoftFtRoundConsistencyFixture();

    // T6: the default arg now IS cost_driven=true (negotiation + comparison gate) -- this exercises
    // the exact call shape production callers use (eval.cpp's routeProblem(scoped, placement)).
    RoutingResult r = routeProblem(f.problem, f.placement);
    assert(r.open_routes == 0);
    assert(r.soft_feedthrough_nets[2] > 0.0);  // task requirement 3: soft_feedthrough_nets nonzero

    // Cross-check 1: soft_feedthrough_nets[S] must equal the sum of nets routed through S as an
    // INTERMEDIATE node across every route in the returned struct's own `routes` list -- this is
    // literally how commitRoutePath accrues soft_feedthrough_nets (routing.cpp), so if the returned
    // `routes` came from a different round/branch than the returned `soft_feedthrough_nets` vector,
    // this sum would diverge.
    double nets_via_routes = 0.0;
    for (const auto& route : r.routes) {
        for (std::size_t i = 1; i + 1 < route.node_path.size(); ++i) {
            if (route.node_path[i] == 2) nets_via_routes += route.nets;  // S is block index 2
        }
    }
    assert(std::abs(nets_via_routes - r.soft_feedthrough_nets[2]) < 1e-6);

    // Cross-check 2 (the task's named check): recompute feedthrough_mismatch from the returned
    // soft_feedthrough_nets via ft.hpp -- the EXACT formula computeOverflowAndMismatch (routing.cpp)
    // uses -- and assert it equals the returned struct's feedthrough_mismatch. A mismatch computed
    // against a different round's soft_feedthrough_nets (or vice versa) would fail this equality.
    const Block& s = f.problem.blocks[2];
    double base = s.ft_base_area > 0.0 ? s.ft_base_area : s.area;
    double nets = r.soft_feedthrough_nets[2];
    double required_area = ftRequiredArea(base, ftExtend(nets, ftTierRate(s, nets)));
    double actual_area = round2(f.placement.rects[2].w * f.placement.rects[2].h);
    double expected_mismatch = std::max(0.0, required_area - actual_area);
    assert(expected_mismatch > 0.0);  // confirms this fixture exercises a NONTRIVIAL (nonzero) check,
                                       // not the 0==0 case ChannelVsFtFixture's slack-absorbed S would
                                       // give.
    assert(std::abs(r.feedthrough_mismatch - expected_mismatch) < 1e-6);

    // Determinism (task requirement 3's "determinism across two calls"), same discipline as
    // testCostDrivenDeterministic/testNegotiatedRoutingDeterministic/testComparisonGateDeterministic
    // above.
    RoutingResult r2 = routeProblem(f.problem, f.placement);
    assert(r2.open_routes == r.open_routes);
    assert(std::abs(r2.soft_feedthrough_nets[2] - r.soft_feedthrough_nets[2]) < kEps);
    assert(std::abs(r2.feedthrough_mismatch - r.feedthrough_mismatch) < kEps);
    assert(r2.routes.size() == r.routes.size());
    for (std::size_t i = 0; i < r.routes.size(); ++i) {
        assert(r2.routes[i].node_path == r.routes[i].node_path);
        assert(r2.routes[i].nets == r.routes[i].nets);
    }

    std::cout << "testRoundConsistencyOfSoftFeedthroughFields ok (soft_feedthrough_nets[S]="
              << r.soft_feedthrough_nets[2] << ", feedthrough_mismatch=" << r.feedthrough_mismatch
              << ")\n";
}

// B4 NEW (design b4-design.md §7.2/§8 fixture (v), R2's hard unit assertion -- THE algebraic tie
// that makes the comparison gate scorer-faithful): roundCostOfficial(rr, alpha, outline_area).second
// must equal exactScore(...).value - outline_area EXACTLY, for a routing with open_routes==0 and
// zero legality penalty (no overlap / out-of-outline) -- eval.cpp's routeAndScoreAt charges
// `outline_area + alpha*wirelength + [legality] + open_routes*1e15 + kBetaLin*A*R +
// kBetaTail*A*max(0,R-threshold) + kBetaFt*mismatch`; subtracting outline_area and requiring
// open_routes==0/legality==0 leaves exactly roundCostOfficial's own formula, computed over the SAME
// RoutingResult eval.cpp routed. Reuses SoftFtRoundConsistencyFixture (already known fully-routed,
// legal, and FT-mismatch-nonzero from the test above) with its Placement's outline stamped to
// exactly the Problem's own declared outline, so exactScore takes the direct routeAndScoreAt path
// (no shrinkWrapOutline/corrective-grow indirection) and both sides score/route the IDENTICAL
// (problem, placement) at the IDENTICAL declared outline.
void testRoundCostOfficialAlgebraicallyTiesToExactScore() {
    SoftFtRoundConsistencyFixture f = makeSoftFtRoundConsistencyFixture();
    f.placement.outline_w = f.problem.outline_w;
    f.placement.outline_h = f.problem.outline_h;

    RoutingResult routing;
    Score score = exactScore(f.problem, f.placement, &routing);
    assert(routing.open_routes == 0);
    // Legality penalty is 0 for this fixture's non-overlapping, in-bounds placement -- confirmed via
    // the identity itself (any nonzero legality/overlap term would show up as a nonzero diff below).
    assert(routing.feedthrough_mismatch > 0.0);  // nontrivial: not a vacuous all-zero-terms check

    auto key = roundCostOfficial(routing, f.problem.alpha, score.outline_area);
    double scorer_side = score.value - score.outline_area;
    assert(key.first == 0);
    assert(std::abs(key.second - scorer_side) < 1e-6);

    std::cout << "testRoundCostOfficialAlgebraicallyTiesToExactScore ok (roundCostOfficial="
              << key.second << ", exactScore.value-outline_area=" << scorer_side << ")\n";
}

}  // namespace

int main() {
    testVerticalOverflowIsAreaDenominatedByChannelHeight();
    testHorizontalOverflowIsAreaDenominatedByChannelWidth();
    testOverflowAreaComparableToFtMismatchMagnitude();
    testChannelMarginalCostIsZeroWhenSlack();
    testChannelMarginalCostSaturatedVertical();
    testChannelMarginalCostSaturatedHorizontal();
    testChannelMarginalCostStraddlesCap();
    testSoftFtMarginalCostPricesTierJump();
    testSoftFtMarginalCostFreeWhenPlacedAreaGenerous();
    testWireCostTrivial();
    testMarginalOverflowNetsZeroWhenSlack();
    testMarginalOverflowNetsSaturated();
    testMarginalOverflowNetsStraddles();
    testMarginalOverflowNetsRawCapDivergesFromOldClampedCapOnSubMicronChannel();
    testChannelPathSlackStraightPassThroughHalves();
    testChannelPathSlackLTurnTighterAxisBinds();
    testCostDrivenPrefersFtOverSaturatedChannel();
    testCostDrivenPrefersChannelWhenFtExpensiveUnderOfficialRate();
    testCostDrivenZeroWlDirectAbutUnchanged();
    testCostDrivenDeterministic();
    testFixedSplitUnchangedForCostDrivenFalse();
    testBottleneckFixtureNowDivertsEntirelyToFtUnderOfficialPricing();
    testStraightPassThroughBothEdgesPricingSaturatesChannelExactly();
    testAdaptiveChunkingRespectsMaxChunksWithNoAlternative();
    testAdaptiveChunkingDeterministic();
    testNegotiationClearsRoundZeroOverflow();
    testNegotiatedRoutingDeterministic();
    testFeasibilityFallbackNeverWorseThanLegacyOnGenuineDeadEnd();
    testComparisonGateKeepsCostDrivenWhenItWins();
    testComparisonGateSwitchesToLegacyWhenCostDrivenFullyRoutedButWorse();
    testComparisonGateDeterministic();
    testRoundConsistencyOfSoftFeedthroughFields();
    testRoundCostOfficialAlgebraicallyTiesToExactScore();
    std::cout << "P2 T0+T1+T2+T3+T4+T5+T5.1+T6+B4 regressions passed\n";
    return 0;
}
