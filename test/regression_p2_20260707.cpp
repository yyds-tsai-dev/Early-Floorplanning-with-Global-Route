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
    double expected_legacy_overflow = ((7200.0 - 2000.0) / 25.0) * 610.0;  // 126,880 (per-edge doubling)
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

    // The split still fills exactly 1000 nets at the bottleneck, but per-edge accounting charges the
    // channel's vertical axis on BOTH the entry (from A) and exit (to B) edge, doubling the committed
    // chunk's demand -- so this now OVERSHOOTS vcap=1000 rather than saturating it exactly.
    bool found_channel = false;
    for (const auto& ch : r.channels) {
        if (std::abs(ch.rect.w - 40.0) < kEps && std::abs(ch.rect.h - 120.0) < kEps) {
            found_channel = true;
            assert(std::abs(ch.vertical_usage - 2000.0) < kEps);
        }
    }
    assert(found_channel);
    assert(std::abs(r.channel_overflow - 4800.0) < 1e-3);  // per-edge: committed 1000 nets accrue 2000 vertical demand (entry+exit), overshooting vcap=1000; overflow = ((2000-1000)/25)*120 = 4800 um^2. pathSlack still sizes chunks by entry-axis capacity (documented deferred gap), so it fills 1000 nets, not 500.

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

ElbowMispricingFixture makeElbowMispricingFixture() {
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
    elbow.nets = 300;
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

void testComparisonGatePrefersLegacyWhenCostDrivenFullyRoutedButWorse() {
    ElbowMispricingFixture f = makeElbowMispricingFixture();

    RoutingResult r = routeProblem(f.problem, f.placement, /*cost_driven=*/true);
    // Both sides are FULLY ROUTED -- the original T5 open-routes-only fallback would never have
    // engaged here (nothing was open). This fixture's whole point is that the amendment's full
    // AREA-cost comparison must still catch and correct this case.
    assert(r.open_routes == 0);
    assert(!r.used_fallback);  // per-edge INVERTS this fixture: cost-driven now wins (see below), so the gate keeps cost-driven's own result

    // Non-substitution proof (Beta Task 3, replacing the pre-per-edge "wholesale substitution" check).
    // Under the INVERTED premise (used_fallback=false above, the gate keeps cost-driven's OWN result),
    // r must DIFFER from a direct legacy call on the decisive fields -- proving the gate genuinely kept
    // cost-driven's strictly-better result rather than substituting legacy wholesale. (Mirror image of
    // the OLD assertion, which proved r==legacy when the gate DID substitute; now it proves r<legacy.)
    RoutingResult legacy = routeProblem(f.problem, f.placement, /*cost_driven=*/false);
    assert(r.open_routes == legacy.open_routes);                    // both fully routed (0 == 0)
    assert(r.total_wirelength < legacy.total_wirelength - kEps);    // cost-driven WL strictly lower
    assert(r.channel_overflow < legacy.channel_overflow - kEps);    // cost-driven overflow strictly lower

    // Hand-derived numbers (measured post-per-edge): per-edge doubling now makes the 900-net straight
    // vertical filler alone overshoot CH1 (2*900=1800 > vcap=1000) on BOTH sides regardless of the
    // elbow bundle's routing choice, so legacy no longer achieves zero overflow here -- it is now
    // STRICTLY WORSE than cost-driven's own diversion of the elbow bundle (4,880 um^2), confirming
    // cost-driven's win above.
    assert(std::abs(legacy.channel_overflow - 19520.0) < 1.0);
    assert(std::abs(legacy.total_wirelength - 625500.0) < 1.0);

    // Cross-check the LOSING side's own numbers directly (without the gate) to confirm the fixture
    // actually exercises the per-edge-driven inversion described above, not some other divergence.
    // area_cost_legacy = alpha*wl + kOverflowAreaWeight*overflow = 0.001*625500 + 1e6*19520
    //                   = 625.5 + 1.952e10 = 19,520,000,625.5
    double area_cost_legacy = f.problem.alpha * legacy.total_wirelength +
                               kOverflowAreaWeight * legacy.channel_overflow +
                               kFtMismatchWeight * legacy.feedthrough_mismatch;
    assert(std::abs(area_cost_legacy - 19520000625.5) < 100.0);
    assert(std::abs(r.channel_overflow - 4880.0) < 1.0);

    std::cout << "testComparisonGatePrefersLegacyWhenCostDrivenFullyRoutedButWorse ok (cd_overflow="
              << r.channel_overflow << ", legacy_wl=" << legacy.total_wirelength
              << ", legacy_overflow=" << legacy.channel_overflow << ")\n";
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
    testFeasibilityFallbackNeverWorseThanLegacyOnGenuineDeadEnd();
    testComparisonGatePrefersLegacyWhenCostDrivenFullyRoutedButWorse();
    testComparisonGateDeterministic();
    testRoundConsistencyOfSoftFeedthroughFields();
    std::cout << "P2 T0+T1+T2+T3+T4+T5+T5.1+T6 regressions passed\n";
    return 0;
}
