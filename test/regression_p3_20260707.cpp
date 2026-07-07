#include <cassert>
#include <cmath>
#include <iostream>

#include "../src/model.hpp"
#include "../src/respace.hpp"
#include "../src/routing.hpp"

namespace {
using namespace efgr;

Block makeBlock(const std::string& name, double w, double h) {
    Block b;
    b.name = name;
    b.width = w;
    b.height = h;
    b.area = w * h;
    b.type = BlockType::Macro;
    return b;
}

// Fixture (a): A below, B above, L/R flank the A-B gap in x so the channel between A and B is
// bounded left=L, right=R, below=A, above=B. The A->B bundle's guide-point path crosses the
// channel vertically (dy>0, dx==0 at the guide points -- verified empirically against
// routing.cpp's guidePoint/usage accrual before writing this fixture), producing pure
// vertical_usage. Per the axis-crossing invariant (p3-design.md §1.2-1.3) this must land as
// need_w on the (left,right)=(L,R) pair, with need_h == 0.
void testVerticalUsageChargesNeedWOnLeftRight() {
    Problem p;
    p.blocks = {makeBlock("A", 100, 100), makeBlock("B", 100, 100), makeBlock("L", 50, 300), makeBlock("R", 50, 300)};
    p.block_index = {{"A", 0}, {"B", 1}, {"L", 2}, {"R", 3}};
    p.outline_w = 200;
    p.outline_h = 300;
    p.alpha = 1.0;
    Bundle bun;
    bun.source = 0;
    bun.target = 1;
    bun.nets = 5;
    p.bundles = {bun};

    Placement pl;
    pl.rects = {Rect{50, 0, 100, 100}, Rect{50, 200, 100, 100}, Rect{0, 0, 50, 300}, Rect{150, 0, 50, 300}};
    pl.aspect = {1.0, 1.0, 1.0, 1.0};

    RoutingResult routing = routeProblem(p, pl);
    assert(routing.open_routes == 0);
    assert(routing.channels.size() == 1);
    assert(routing.channels[0].vertical_usage > kEps);
    assert(routing.channels[0].horizontal_usage <= kEps);

    std::vector<StripDemand> demands = channelDemands(p, pl, routing);
    assert(demands.size() == 1);
    const StripDemand& d = demands[0];
    assert(d.left == 2);   // L
    assert(d.right == 3);  // R
    assert(d.below == 0);  // A
    assert(d.above == 1);  // B
    assert(std::abs(d.need_w - routing.channels[0].vertical_usage / 25.0) < kEps);
    assert(d.need_h <= kEps);
    std::cout << "testVerticalUsageChargesNeedWOnLeftRight ok\n";
}

// Fixture (b): the transposed fixture -- L, R side-by-side with the gap between them flanked
// above/below by T and Bo, so the channel is bounded left=L, right=R, below=Bo, above=T. The
// L->R bundle's path crosses the channel horizontally (dx>0, dy==0), producing pure
// horizontal_usage. Per the invariant this must land as need_h on the (below,above)=(Bo,T) pair,
// with need_w == 0.
void testHorizontalUsageChargesNeedHOnBelowAbove() {
    Problem p;
    p.blocks = {makeBlock("L", 100, 100), makeBlock("R", 100, 100), makeBlock("T", 300, 50), makeBlock("Bo", 300, 50)};
    p.block_index = {{"L", 0}, {"R", 1}, {"T", 2}, {"Bo", 3}};
    p.outline_w = 300;
    p.outline_h = 200;
    p.alpha = 1.0;
    Bundle bun;
    bun.source = 0;
    bun.target = 1;
    bun.nets = 7;
    p.bundles = {bun};

    Placement pl;
    pl.rects = {Rect{0, 50, 100, 100}, Rect{200, 50, 100, 100}, Rect{0, 150, 300, 50}, Rect{0, 0, 300, 50}};
    pl.aspect = {1.0, 1.0, 1.0, 1.0};

    RoutingResult routing = routeProblem(p, pl);
    assert(routing.open_routes == 0);
    assert(routing.channels.size() == 1);
    assert(routing.channels[0].horizontal_usage > kEps);
    assert(routing.channels[0].vertical_usage <= kEps);

    std::vector<StripDemand> demands = channelDemands(p, pl, routing);
    assert(demands.size() == 1);
    const StripDemand& d = demands[0];
    assert(d.left == 0);   // L
    assert(d.right == 1);  // R
    assert(d.below == 3);  // Bo
    assert(d.above == 2);  // T
    assert(std::abs(d.need_h - routing.channels[0].horizontal_usage / 25.0) < kEps);
    assert(d.need_w <= kEps);
    std::cout << "testHorizontalUsageChargesNeedHOnBelowAbove ok\n";
}

// Fixture (c): reuse fixture (a)'s geometry but shrink L/R away from the outline border so one
// side of the channel is bounded by the outline WALL instead of a block. Concretely: drop L, so
// the channel's left edge sits at x==0 (the outline wall) with only R bounding it on the right.
// channelBounds must report left == -1 for the wall-bounded side.
void testWallBoundedChannelSideIsMinusOne() {
    Problem p;
    p.blocks = {makeBlock("A", 100, 100), makeBlock("B", 100, 100), makeBlock("R", 50, 300)};
    p.block_index = {{"A", 0}, {"B", 1}, {"R", 2}};
    p.outline_w = 150;
    p.outline_h = 300;
    p.alpha = 1.0;
    Bundle bun;
    bun.source = 0;
    bun.target = 1;
    bun.nets = 5;
    p.bundles = {bun};

    Placement pl;
    pl.rects = {Rect{0, 0, 100, 100}, Rect{0, 200, 100, 100}, Rect{100, 0, 50, 300}};
    pl.aspect = {1.0, 1.0, 1.0};

    RoutingResult routing = routeProblem(p, pl);
    assert(routing.open_routes == 0);
    assert(routing.channels.size() == 1);
    assert(routing.channels[0].vertical_usage > kEps);

    std::vector<StripDemand> demands = channelDemands(p, pl, routing);
    assert(demands.size() == 1);
    const StripDemand& d = demands[0];
    assert(d.left == -1);  // outline wall at x==0
    assert(d.right == 2);  // R
    assert(d.below == 0);  // A
    assert(d.above == 1);  // B
    assert(d.need_w > kEps);
    std::cout << "testWallBoundedChannelSideIsMinusOne ok\n";
}

// Fixture (d): two blocks with no bundle at all -- both channels adjacent to them carry zero
// usage in both directions, so channelDemands must produce no StripDemand entries at all.
void testZeroUsageChannelsProduceNoDemand() {
    Problem p;
    p.blocks = {makeBlock("A", 100, 100), makeBlock("B", 100, 100)};
    p.block_index = {{"A", 0}, {"B", 1}};
    p.outline_w = 200;
    p.outline_h = 300;
    p.alpha = 1.0;
    // No bundles -- nothing routed, so every channel's usage stays at its zero default.

    Placement pl;
    pl.rects = {Rect{50, 0, 100, 100}, Rect{50, 200, 100, 100}};
    pl.aspect = {1.0, 1.0};

    RoutingResult routing = routeProblem(p, pl);
    assert(!routing.channels.empty());
    for (const auto& ch : routing.channels) {
        assert(ch.horizontal_usage <= kEps);
        assert(ch.vertical_usage <= kEps);
    }

    std::vector<StripDemand> demands = channelDemands(p, pl, routing);
    assert(demands.empty());
    std::cout << "testZeroUsageChannelsProduceNoDemand ok\n";
}

// --- T2: constraint-graph relaxToDemand (design decision 4, p3-design.md §2) ---

// Fixture (e): 3-block row A|B|C, all sharing the same y-band so they mutually y-overlap (this
// is required for the frozen x-adjacency to actually chain A->B->C -- see task-2 review point 3
// on test (a)'s trap: if C only overlapped A, C's predecessor would be A, not B, and the demanded
// gap on (A,B) would never propagate to C). need_w=50 charged ONLY on (A,B) (index 0,1); no
// demand on (B,C). Per the design, the graph's absolute-gap semantics mean B must land exactly
// A.right()+50, and because C's edge weight from B is unchanged (just B.w, no demand), C shifts
// by the same 50 as B. A is untouched (nothing charges the wall->A edge). No pair should ever
// swap order.
void testThreeBlockRowShiftsByExactDeficit() {
    Problem p;
    p.blocks = {makeBlock("A", 100, 100), makeBlock("B", 100, 100), makeBlock("C", 100, 100)};
    p.block_index = {{"A", 0}, {"B", 1}, {"C", 2}};
    p.outline_w = 1000;
    p.outline_h = 100;
    p.alpha = 1.0;

    Placement pl;
    pl.rects = {Rect{0, 0, 100, 100}, Rect{100, 0, 100, 100}, Rect{200, 0, 100, 100}};
    pl.aspect = {1.0, 1.0, 1.0};

    StripDemand d;
    d.left = 0;
    d.right = 1;
    d.need_w = 50.0;
    std::vector<StripDemand> demands = {d};

    bool changed = relaxToDemand(p, pl, demands);
    assert(changed);
    assert(std::abs(pl.rects[0].x - 0.0) < kEps);     // A unmoved
    assert(std::abs(pl.rects[1].x - 150.0) < kEps);   // B shifts right by exactly 50
    assert(std::abs(pl.rects[2].x - 250.0) < kEps);   // C carries the same 50 shift
    // Order-freeze: A < B < C in x, strictly, both before and after.
    assert(pl.rects[0].x + kEps < pl.rects[1].x);
    assert(pl.rects[1].x + kEps < pl.rects[2].x);
    std::cout << "testThreeBlockRowShiftsByExactDeficit ok\n";
}

// Fixture (f): V-pass equivalent of (e) -- 3-block column Bo|M|T (bottom, middle, top), all
// sharing the same x-band so they mutually x-overlap. need_h=40 charged only on (Bo,M); M and T
// shift up by exactly 40, Bo untouched, order preserved.
void testThreeBlockColumnShiftsByExactDeficit() {
    Problem p;
    p.blocks = {makeBlock("Bo", 100, 100), makeBlock("M", 100, 100), makeBlock("T", 100, 100)};
    p.block_index = {{"Bo", 0}, {"M", 1}, {"T", 2}};
    p.outline_w = 100;
    p.outline_h = 1000;
    p.alpha = 1.0;

    Placement pl;
    pl.rects = {Rect{0, 0, 100, 100}, Rect{0, 100, 100, 100}, Rect{0, 200, 100, 100}};
    pl.aspect = {1.0, 1.0, 1.0};

    StripDemand d;
    d.below = 0;
    d.above = 1;
    d.need_h = 40.0;
    std::vector<StripDemand> demands = {d};

    bool changed = relaxToDemand(p, pl, demands);
    assert(changed);
    assert(std::abs(pl.rects[0].y - 0.0) < kEps);    // Bo unmoved
    assert(std::abs(pl.rects[1].y - 140.0) < kEps);  // M shifts up by exactly 40
    assert(std::abs(pl.rects[2].y - 240.0) < kEps);  // T carries the same 40 shift
    assert(pl.rects[0].y + kEps < pl.rects[1].y);
    assert(pl.rects[1].y + kEps < pl.rects[2].y);
    std::cout << "testThreeBlockColumnShiftsByExactDeficit ok\n";
}

// Fixture (g): demand exceeding MAX -- same A|B|C row but with outline_w clamped tight (just
// enough to hold the un-widened row) and a demand far larger than the available slack. The
// relax must never push any coordinate past MAX, must still widen (result != un-widened input,
// i.e. partial relief), preserve order, and return true (design failure-mode 1 -- residual
// unsatisfied demand is a graded penalty elsewhere, not a hard failure here).
void testDemandExceedingMaxClampsToOutlineAndStaysLegal() {
    Problem p;
    p.blocks = {makeBlock("A", 100, 100), makeBlock("B", 100, 100), makeBlock("C", 100, 100)};
    p.block_index = {{"A", 0}, {"B", 1}, {"C", 2}};
    p.outline_w = 320;  // only 20 units of slack beyond the tight 300-wide row
    p.outline_h = 100;
    p.alpha = 1.0;

    Placement pl;
    pl.rects = {Rect{0, 0, 100, 100}, Rect{100, 0, 100, 100}, Rect{200, 0, 100, 100}};
    pl.aspect = {1.0, 1.0, 1.0};

    StripDemand d;
    d.left = 0;
    d.right = 1;
    d.need_w = 500.0;  // wildly exceeds available slack
    std::vector<StripDemand> demands = {d};

    bool changed = relaxToDemand(p, pl, demands);
    assert(changed);
    for (const auto& r : pl.rects) {
        assert(r.right() <= p.outline_w + kEps);
    }
    // Partial widening: B (and C) still moved right of their un-widened positions, just not by
    // the full 500.
    assert(pl.rects[1].x > 100.0 + kEps);
    assert(pl.rects[1].x <= p.outline_w - 100.0 + kEps);
    // Order-freeze holds even under clamping.
    assert(pl.rects[0].x + kEps < pl.rects[1].x);
    assert(pl.rects[1].x + kEps < pl.rects[2].x);
    std::cout << "testDemandExceedingMaxClampsToOutlineAndStaysLegal ok\n";
}

// Fixture (h): order-freeze fuzz -- several fixed, hand-varied demand sets (no RNG) over a
// 4-block row, each checked to never swap any pair's relative x-order. Mirrors the frozen-order
// invariant (design §2.2/§3.2): the relax only ever increases coordinates, so an x-predecessor
// can never become an x-successor.
void testOrderFreezeUnderVariedDemands() {
    auto makeRow = []() {
        Problem p;
        p.blocks = {makeBlock("A", 80, 100), makeBlock("B", 60, 100), makeBlock("C", 90, 100), makeBlock("D", 70, 100)};
        p.block_index = {{"A", 0}, {"B", 1}, {"C", 2}, {"D", 3}};
        p.outline_w = 2000;
        p.outline_h = 100;
        p.alpha = 1.0;
        Placement pl;
        pl.rects = {Rect{0, 0, 80, 100}, Rect{80, 0, 60, 100}, Rect{140, 0, 90, 100}, Rect{230, 0, 70, 100}};
        pl.aspect = {1.0, 1.0, 1.0, 1.0};
        return std::make_pair(p, pl);
    };

    std::vector<std::vector<StripDemand>> demand_sets;
    {
        StripDemand d1;
        d1.left = 0;
        d1.right = 1;
        d1.need_w = 15.0;
        StripDemand d2;
        d2.left = 2;
        d2.right = 3;
        d2.need_w = 200.0;
        demand_sets.push_back({d1, d2});
    }
    {
        StripDemand d1;
        d1.left = 1;
        d1.right = 2;
        d1.need_w = 3.0;
        demand_sets.push_back({d1});
    }
    {
        StripDemand d1;
        d1.left = 0;
        d1.right = 1;
        d1.need_w = 500.0;
        StripDemand d2;
        d2.left = 1;
        d2.right = 2;
        d2.need_w = 500.0;
        StripDemand d3;
        d3.left = 2;
        d3.right = 3;
        d3.need_w = 500.0;
        demand_sets.push_back({d1, d2, d3});
    }
    demand_sets.push_back({});  // empty demand set: pure no-op

    for (const auto& demands : demand_sets) {
        auto [p, pl0] = makeRow();
        Placement pl = pl0;
        bool changed = relaxToDemand(p, pl, demands);
        assert(changed);
        for (int i = 0; i + 1 < 4; ++i) {
            assert(pl.rects[i].x + kEps < pl.rects[i + 1].x);
        }
        for (const auto& r : pl.rects) {
            assert(r.right() <= p.outline_w + kEps);
        }
    }
    std::cout << "testOrderFreezeUnderVariedDemands ok\n";
}

// Carry-over (a): staggered-distractor regression for channelBounds' strip-membership filter.
// Two blocks share the SAME right edge (x0) that bounds the channel's left side: `Flank` fully
// spans the channel's y-range [y0,y1], while `Distractor` sits mostly BELOW y0 and only pokes a
// sliver (1 unit) into [y0,y1] via edge-coincident overlap. Before the fix, the tie-break used
// "greatest r.right()" which is degenerate when both candidates share the same edge (both equal
// x0), so iteration order (i.e. index) alone decided -- meaning a Distractor placed before Flank
// in the blocks array could win despite not actually flanking the channel body. The fix requires
// the DEEPEST cross-range overlap to win; Distractor's 1-unit sliver must lose to Flank's full
// 100-unit span regardless of index order.
void testStaggeredDistractorDoesNotWinChannelBound() {
    Problem p;
    // Distractor (index 0) is listed FIRST so a naive "first match wins" tie-break would pick it.
    p.blocks = {makeBlock("Distractor", 50, 21), makeBlock("Flank", 50, 100), makeBlock("Opposite", 50, 100)};
    p.block_index = {{"Distractor", 0}, {"Flank", 1}, {"Opposite", 2}};
    p.outline_w = 200;
    p.outline_h = 300;

    // Channel occupies x in [50,150], y in [100,200] (matches Flank's y-span exactly).
    Rect channel_rect{50, 100, 100, 100};

    Placement pl;
    // Distractor: right edge at x==50 (same as Flank), y-range [80,101] -- pokes only 1 unit into
    // the channel's [100,200] band (overlap depth = 1).
    // Flank: right edge at x==50, y-range [100,200] -- fully spans the channel (overlap depth = 100).
    // Opposite: left edge at x==150, y-range [100,200] -- the right-bounding block (uncontested).
    pl.rects = {Rect{0, 80, 50, 21}, Rect{0, 100, 50, 100}, Rect{150, 100, 50, 100}};
    pl.aspect = {1.0, 1.0, 1.0};

    ChannelBounds bounds = channelBounds(p, pl, channel_rect);
    assert(bounds.left == 1);   // Flank, not Distractor(0), despite Distractor's lower index
    assert(bounds.right == 2);  // Opposite
    std::cout << "testStaggeredDistractorDoesNotWinChannelBound ok\n";
}

// Carry-over (c): mixed-direction fixture -- one channel carrying BOTH horizontal and vertical
// usage (an L-shaped route composite), so channelDemands must populate need_w and need_h
// independently on the same StripDemand entry. Built as two bundles routed through the same
// single channel: A->B crosses vertically (pure dy), C->D crosses horizontally (pure dx), and
// both bundles' guide paths pass through the one gap between the four flanking blocks.
void testMixedDirectionChannelPopulatesBothNeeds() {
    Problem p;
    p.blocks = {makeBlock("A", 100, 100), makeBlock("B", 100, 100), makeBlock("C", 100, 100), makeBlock("D", 100, 100)};
    p.block_index = {{"A", 0}, {"B", 1}, {"C", 2}, {"D", 3}};
    p.outline_w = 300;
    p.outline_h = 300;
    p.alpha = 1.0;
    Bundle vertical_bun;
    vertical_bun.source = 0;
    vertical_bun.target = 1;
    vertical_bun.nets = 5;
    Bundle horizontal_bun;
    horizontal_bun.source = 2;
    horizontal_bun.target = 3;
    horizontal_bun.nets = 7;
    p.bundles = {vertical_bun, horizontal_bun};

    // A below, B above (vertical crossing through the shared channel's y-gap); C left, D right of
    // the SAME channel (horizontal crossing through its x-gap). All four blocks bound the single
    // central channel [100,200]x[100,200].
    Placement pl;
    pl.rects = {Rect{100, 0, 100, 100}, Rect{100, 200, 100, 100}, Rect{0, 100, 100, 100}, Rect{200, 100, 100, 100}};
    pl.aspect = {1.0, 1.0, 1.0, 1.0};

    RoutingResult routing = routeProblem(p, pl);
    assert(routing.open_routes == 0);
    // Find the shared central channel among the extracted channels.
    const Channel* central = nullptr;
    for (const auto& ch : routing.channels) {
        if (std::abs(ch.rect.x - 100) < kEps && std::abs(ch.rect.y - 100) < kEps &&
            std::abs(ch.rect.w - 100) < kEps && std::abs(ch.rect.h - 100) < kEps) {
            central = &ch;
        }
    }
    assert(central != nullptr);
    assert(central->horizontal_usage > kEps);
    assert(central->vertical_usage > kEps);

    std::vector<StripDemand> demands = channelDemands(p, pl, routing);
    bool found = false;
    for (const auto& d : demands) {
        if (d.left == 2 && d.right == 3 && d.below == 0 && d.above == 1) {
            found = true;
            assert(d.need_w > kEps);  // vertical_usage -> need_w on (left,right)
            assert(d.need_h > kEps);  // horizontal_usage -> need_h on (below,above)
        }
    }
    assert(found);
    std::cout << "testMixedDirectionChannelPopulatesBothNeeds ok\n";
}

}  // namespace

int main() {
    testVerticalUsageChargesNeedWOnLeftRight();
    testHorizontalUsageChargesNeedHOnBelowAbove();
    testWallBoundedChannelSideIsMinusOne();
    testZeroUsageChannelsProduceNoDemand();
    testThreeBlockRowShiftsByExactDeficit();
    testThreeBlockColumnShiftsByExactDeficit();
    testDemandExceedingMaxClampsToOutlineAndStaysLegal();
    testOrderFreezeUnderVariedDemands();
    testStaggeredDistractorDoesNotWinChannelBound();
    testMixedDirectionChannelPopulatesBothNeeds();
    std::cout << "P3 T1+T2 regressions passed\n";
    return 0;
}
