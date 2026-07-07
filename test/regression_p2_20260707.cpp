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
    std::cout << "P2 T0+T1 regressions passed\n";
    return 0;
}
