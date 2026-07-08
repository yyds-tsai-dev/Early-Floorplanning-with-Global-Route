#include <cassert>
#include <cmath>
#include <iostream>
#include <string>

#include "../src/ft.hpp"
#include "../src/model.hpp"
#include "../src/route_cost.hpp"
#include "../src/routing.hpp"

// P2 Task 0 (design p2-design.md §8.1 R0 / plan Global Constraint 2): area-denominate
// channel_overflow so it is comparable, in the scorer's shared 1e6 weight (eval.cpp:143-144), to
// feedthrough_mismatch, which is already um^2. Previously channel_overflow was nets-over-capacity
// -- an apples-to-oranges mismatch that hid the official 2.4x FT win from the scorer.

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
            assert(std::abs(ch.vertical_usage - 3600.0) < kEps);
            assert(std::abs(ch.horizontal_usage) < kEps);
        }
    }
    assert(found);

    double expected = ((3600.0 - 2000.0) / 25.0) * 610.0;  // 64 * 610 = 39,040
    assert(std::abs(expected - 39040.0) < kEps);
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
            assert(std::abs(ch.horizontal_usage - 3600.0) < kEps);
            assert(std::abs(ch.vertical_usage) < kEps);
        }
    }
    assert(found);

    double expected = ((3600.0 - 2000.0) / 25.0) * 610.0;  // 39,040
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
void testCostDrivenPrefersFtOverSaturatedChannel() {
    ChannelVsFtFixture f = makeChannelVsFtFixture();

    RoutingResult legacy = routeProblem(f.problem, f.placement, /*cost_driven=*/false);
    assert(legacy.open_routes == 0);
    double expected_legacy_overflow = ((3600.0 - 2000.0) / 25.0) * 610.0;  // 39,040, T0 fixture (a)
    assert(std::abs(legacy.channel_overflow - expected_legacy_overflow) < 1e-3);
    assert(legacy.soft_feedthrough_nets[2] == 0.0);  // legacy pass-0 never touches S

    // Hand-verify the FT-vs-channel decision this fixture is built to test, against ft.hpp directly.
    double rate = ftTierRate(f.problem.blocks[2], 3600.0);
    assert(std::abs(rate - 0.4) < kEps);  // confirms the 40% tier from the design-doc worked example
    double required = ftRequiredArea(f.problem.blocks[2].ft_base_area, ftExtend(3600.0, rate));
    double ft_marginal = required - f.problem.blocks[2].ft_base_area;  // S starts unexpanded (area==ft_base_area)
    assert(ft_marginal < expected_legacy_overflow);  // 36,261.34 < 39,040 -- FT must win this fixture

    RoutingResult cd = routeProblem(f.problem, f.placement, /*cost_driven=*/true);
    assert(cd.open_routes == 0);
    assert(cd.soft_feedthrough_nets[2] > 0.0);
    assert(cd.channel_overflow < legacy.channel_overflow);
    std::cout << "testCostDrivenPrefersFtOverSaturatedChannel ok (legacy overflow=" << legacy.channel_overflow
               << ", cost_driven overflow=" << cd.channel_overflow << ")\n";
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
void testAdaptiveChunkingSplitsAtBottleneck() {
    BottleneckFixture f = makeBottleneckFixture();
    RoutingResult r = routeProblem(f.problem, f.placement, /*cost_driven=*/true);
    assert(r.open_routes == 0);

    // Chunk count is the hard discriminator against the legacy fixed split: ceil(3000/1000)=3 fixed
    // chunks of 1000 each is what the OLD algorithm always produces for this bundle size, regardless
    // of routing decisions -- a coincidental per-chunk routing choice could still land on (1000,1000,
    // 1000) with two of the three diverting through S, which would satisfy every OTHER assertion in
    // this test without actually exercising the greedy bottleneck-split loop. Asserting exactly 2
    // chunks (this fixture's hand-traced §5.2 expectation: fill slack=1000, divert remainder=2000 in
    // ONE further chunk since kMinChunk=46.875 < slack=3000-0 and remaining=2000 clamps it) is the
    // only way this test can distinguish "greedy split-at-bottleneck implemented" from "fixed split
    // whose per-chunk cost-driven Dijkstra happens to route the same way."
    assert(r.routes.size() == 2);
    assert(r.routes.size() <= 16);

    // Total nets conserved across emitted chunks (Sigma chunk nets == bundle nets).
    int total = 0;
    for (const auto& route : r.routes) {
        assert(route.nets > 0);  // no pathological zero/negative chunks
        total += route.nets;
    }
    assert(total == 3000);

    // The bottleneck-fill-then-divert shape: first chunk fills the channel's slack to exactly 1000
    // nets (vcap=1000, starting from an empty channel), remaining chunk(s) divert through S's FT
    // (soft_feedthrough_nets[S] must account for the diverted remainder, 2000 nets).
    assert(r.routes[0].nets == 1000);
    double diverted = 0.0;
    for (std::size_t i = 1; i < r.routes.size(); ++i) diverted += r.routes[i].nets;
    assert(std::abs(diverted - 2000.0) < kEps);
    assert(std::abs(r.soft_feedthrough_nets[2] - 2000.0) < kEps);

    // The channel must show exactly the 1000-net chunk's usage (no overflow -- it filled to exactly
    // capacity, not past it), confirming the split point was precisely at the bottleneck, not before
    // or after it.
    bool found_channel = false;
    for (const auto& ch : r.channels) {
        if (std::abs(ch.rect.w - 40.0) < kEps && std::abs(ch.rect.h - 120.0) < kEps) {
            found_channel = true;
            assert(std::abs(ch.vertical_usage - 1000.0) < kEps);
        }
    }
    assert(found_channel);
    assert(r.channel_overflow == 0.0);  // filled to exactly capacity, never exceeded it

    std::cout << "testAdaptiveChunkingSplitsAtBottleneck ok (chunks=" << r.routes.size()
               << ", diverted=" << diverted << ")\n";
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
    testCostDrivenPrefersFtOverSaturatedChannel();
    testCostDrivenZeroWlDirectAbutUnchanged();
    testCostDrivenDeterministic();
    testFixedSplitUnchangedForCostDrivenFalse();
    testAdaptiveChunkingSplitsAtBottleneck();
    testAdaptiveChunkingRespectsMaxChunksWithNoAlternative();
    testAdaptiveChunkingDeterministic();
    testNegotiationClearsRoundZeroOverflow();
    testNegotiatedRoutingDeterministic();
    std::cout << "P2 T0+T1+T2+T3+T4 regressions passed\n";
    return 0;
}
