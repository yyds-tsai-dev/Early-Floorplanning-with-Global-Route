#include <cassert>
#include <cmath>
#include <iostream>
#include <memory>
#include <random>

#include "../src/eval.hpp"
#include "../src/io.hpp"
#include "../src/model.hpp"
#include "../src/placement_engine.hpp"
#include "../src/respace.hpp"
#include "../src/routing.hpp"
#include "../src/search.hpp"
#include "../src/validate.hpp"

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
// bounded left=L, right=R, below=A, above=B. The A->B bundle's guiding-point path crosses the
// channel vertically (dy>0, dx==0 at the guiding points -- verified empirically against
// route_geometry.hpp's guidingPoint/usage accrual before writing this fixture), producing pure
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

// --- P3-T2 review fixes (2 Critical): relaxAxis must relax over ALL constraint-graph
// predecessors, not a single selected one. ---

// TEST C2 (discarded-predecessor overlap; fixture verified legal + empirically reproduces the
// bug): idx0 "P" Rect{0,0,10,4}; idx1 "A2" Rect{10,0,1,4}; idx2 "A2P" Rect{27,4,2,4}; idx3 "B"
// Rect{29,0,5,8} (spans both y-bands: [0,4] where A2 sits and [4,8] where A2P sits). One demand,
// keyed (P,A2)=20, is the only correct edge into A2; A2's true predecessor is P, so A2 legitimately
// leaps to new_x = P.right()+20 = 30, landing at [30,31]. Under the single-predecessor bug, B's
// nearest-neighbor search ties between A2 (far=11) and A2P (far=29... via its own far edge) and
// picks whichever wins the tie-break, discarding the other -- so B can be left at its old x=29,
// which then overlaps A2's grown [30,31]. The fix (relax over ALL cross-overlapping predecessors)
// must have B consider both A2 and A2P, landing at x=31 (A2's new_right()), clearing A2 exactly.
void testTieLosingCandidateCannotOverlapDiscardingBlock() {
    Problem p;
    p.blocks = {makeBlock("P", 10, 4), makeBlock("A2", 1, 4), makeBlock("A2P", 2, 4), makeBlock("B", 5, 8)};
    p.block_index = {{"P", 0}, {"A2", 1}, {"A2P", 2}, {"B", 3}};
    p.outline_w = 100;
    p.outline_h = 100;
    p.alpha = 1.0;

    Placement pl;
    pl.rects = {Rect{0, 0, 10, 4}, Rect{10, 0, 1, 4}, Rect{27, 4, 2, 4}, Rect{29, 0, 5, 8}};
    pl.aspect = {1.0, 1.0, 1.0, 1.0};

    // Sanity: the starting placement is legal (no pairwise overlap) before relaxing -- all 6 pairs.
    for (std::size_t i = 0; i < pl.rects.size(); ++i) {
        for (std::size_t j = i + 1; j < pl.rects.size(); ++j) {
            assert(!overlaps(pl.rects[i], pl.rects[j]));
        }
    }

    StripDemand d;
    d.left = 0;   // P
    d.right = 1;  // A2
    d.need_w = 20.0;
    std::vector<StripDemand> demands = {d};

    bool changed = relaxToDemand(p, pl, demands);
    assert(changed);

    // EXPECTED with the fix: new x = {0, 30, 27, 31}.
    assert(std::abs(pl.rects[0].x - 0.0) < kEps);
    assert(std::abs(pl.rects[1].x - 30.0) < kEps);
    assert(std::abs(pl.rects[2].x - 27.0) < kEps);
    assert(std::abs(pl.rects[3].x - 31.0) < kEps);
    // B must clear A2's new position; A2 must clear P's new position.
    assert(pl.rects[3].x + kEps >= 31.0 - kEps);
    assert(pl.rects[1].x + kEps >= 30.0 - kEps);

    // No pair may overlap after the relax (the buggy code yields B.x==29 with A2 at [30,31),
    // which overlaps B -- that's the RED this test catches).
    for (std::size_t i = 0; i < pl.rects.size(); ++i) {
        for (std::size_t j = i + 1; j < pl.rects.size(); ++j) {
            assert(!overlaps(pl.rects[i], pl.rects[j]));
        }
    }
    std::cout << "testTieLosingCandidateCannotOverlapDiscardingBlock ok\n";
}

// TEST C1 (dropped keyed demand): full-height L idx0 Rect{0,0,10,100}; partial wedge M idx1
// Rect{10,60,4,20}; R idx2 Rect{14,0,6,100}; outline 200x100. One demand keyed directly (L,R)
// with need_w=25 -- NOT (M,R) -- because the real constraint is on the L<->R gap, independent of
// the M wedge sitting between them. Under the single-predecessor bug, R's immediate x-predecessor
// is M (M's far edge, 14, meets R's near edge), so the lookup merged.find({M,R}) misses (the
// demand is keyed {L,R}) and the (L,R) demand is silently dropped; R never moves past its old
// x=14, leaving only a 4-unit gap to L, not the demanded 25. The fix must apply the (L,R)
// merged-demand-pair edge regardless of who wins the immediate-predecessor slot.
void testDemandKeyedPairAppliesEvenWhenAnotherBlockWinsPredecessorSlot() {
    Problem p;
    p.blocks = {makeBlock("L", 10, 100), makeBlock("M", 4, 20), makeBlock("R", 6, 100)};
    p.block_index = {{"L", 0}, {"M", 1}, {"R", 2}};
    p.outline_w = 200;
    p.outline_h = 100;
    p.alpha = 1.0;

    Placement pl;
    pl.rects = {Rect{0, 0, 10, 100}, Rect{10, 60, 4, 20}, Rect{14, 0, 6, 100}};
    pl.aspect = {1.0, 1.0, 1.0};

    for (std::size_t i = 0; i < pl.rects.size(); ++i) {
        for (std::size_t j = i + 1; j < pl.rects.size(); ++j) {
            assert(!overlaps(pl.rects[i], pl.rects[j]));
        }
    }

    StripDemand d;
    d.left = 0;   // L
    d.right = 2;  // R
    d.need_w = 25.0;
    std::vector<StripDemand> demands = {d};

    bool changed = relaxToDemand(p, pl, demands);
    assert(changed);

    for (std::size_t i = 0; i < pl.rects.size(); ++i) {
        for (std::size_t j = i + 1; j < pl.rects.size(); ++j) {
            assert(!overlaps(pl.rects[i], pl.rects[j]));
        }
    }
    // EXPECTED with fix: R.x - (L.x+10) >= 25 - 1e-6 (buggy code drops the (L,R) demand because
    // pred(R)=M -> RED).
    assert(pl.rects[2].x - (pl.rects[0].x + 10.0) >= 25.0 - 1e-6);
    std::cout << "testDemandKeyedPairAppliesEvenWhenAnotherBlockWinsPredecessorSlot ok\n";
}

// --- T3: respaceToDemand single integrated pass + legality gate (design T3, p3-design.md §2.1) ---

double sumStampedArea(double w, double h) { return w * h; }

// (a) case1 bstar seed1: decode + exactScore (routes it), assert it is overflow-prone (as design
// §6 predicts for case1), then respaceToDemand must succeed, produce a placement that
// validateSolution accepts against ITS OWN freshly-stamped outline, whose stamped area is >= the
// pre-respace stamped area (widening-only, never shrinks), and whose core (non-edge) blocks moved
// only right/up (monotone widen) while preserving pairwise relative x/y order.
void testRespaceToDemandCase1BStarSeed1() {
    Problem problem = parseProblem("test/2026 ICCAD_case.case1.csv");
    std::unique_ptr<PlacementEngine> engine = makePlacementEngine("bstar");
    std::mt19937 rng(1);
    PlacementState state = engine->initialState(problem, rng);
    Placement placement = engine->decode(problem, state);

    RoutingResult routing;
    exactScore(problem, placement, &routing);
    // V5 re-baseline (2026-07-28, docs/research/2026-07-28-alpha-v5-cost-recalculation-analysis.md):
    // under single-charge demand this decode no longer overflows (was ~38875 pre-P3 under the old
    // per-edge model; channel_overflow is now exactly 0), so the old overflow-prone precondition went
    // vacuous. respaceToDemand's input primitive is channelDemands' strip list -- assert THAT is
    // non-empty so the fixture still provably exercises the widen path.
    assert(!channelDemands(problem, placement, routing).empty());

    double pre_area = sumStampedArea(placement.outline_w, placement.outline_h);
    std::vector<Rect> pre_core;
    std::vector<int> core_indices;
    for (std::size_t i = 0; i < problem.blocks.size(); ++i) {
        if (!isEdge(problem.blocks[i].type)) core_indices.push_back(static_cast<int>(i));
    }
    for (int i : core_indices) pre_core.push_back(placement.rects[i]);

    Placement respaced = placement;
    bool changed = respaceToDemand(problem, respaced, routing);
    assert(changed);

    // Stamped area must not shrink (widening-only per the monotone-widen design invariant).
    double post_area = sumStampedArea(respaced.outline_w, respaced.outline_h);
    assert(post_area + kEps >= pre_area);
    assert(respaced.outline_w <= problem.outline_w + kEps);
    assert(respaced.outline_h <= problem.outline_h + kEps);

    // Legality: re-route the respaced placement and validate against its own fresh stamp.
    Problem scoped = problem;
    scoped.outline_w = respaced.outline_w;
    scoped.outline_h = respaced.outline_h;
    RoutingResult post_routing = routeProblem(scoped, respaced);
    ValidationResult validation = validateSolution(scoped, respaced, post_routing);
    if (!validation.ok) {
        for (const auto& m : validation.messages) std::cout << "  validation: " << m << "\n";
    }
    assert(validation.ok);

    // Core rects moved only right/up (monotone widen) and never left/down; relative order (a few
    // spot-checked pairs by x and by y among core blocks) is preserved.
    for (std::size_t k = 0; k < core_indices.size(); ++k) {
        int idx = core_indices[k];
        assert(respaced.rects[idx].x + kEps >= pre_core[k].x);
        assert(respaced.rects[idx].y + kEps >= pre_core[k].y);
    }
    int pairs_checked = 0;
    for (std::size_t a = 0; a < core_indices.size() && pairs_checked < 20; ++a) {
        for (std::size_t b = a + 1; b < core_indices.size() && pairs_checked < 20; ++b) {
            int ia = core_indices[a];
            int ib = core_indices[b];
            if (pre_core[a].x + kEps < pre_core[b].x) {
                assert(respaced.rects[ia].x <= respaced.rects[ib].x + kEps);
            } else if (pre_core[b].x + kEps < pre_core[a].x) {
                assert(respaced.rects[ib].x <= respaced.rects[ia].x + kEps);
            }
            if (pre_core[a].y + kEps < pre_core[b].y) {
                assert(respaced.rects[ia].y <= respaced.rects[ib].y + kEps);
            } else if (pre_core[b].y + kEps < pre_core[a].y) {
                assert(respaced.rects[ib].y <= respaced.rects[ia].y + kEps);
            }
            ++pairs_checked;
        }
    }
    std::cout << "testRespaceToDemandCase1BStarSeed1 ok\n";
}

// (b) failure/no-op path: a placement whose routed result has zero channel_overflow and zero
// directional usage everywhere -> channelDemands is empty -> respaceToDemand must return false and
// leave the caller's placement byte-identical (design §3.5 degenerate no-op, task brief item (b)).
void testRespaceToDemandNoDemandIsNoOp() {
    Problem p;
    Block a;
    a.name = "A";
    a.width = 100;
    a.height = 100;
    a.area = 100 * 100;
    a.type = BlockType::Macro;
    Block b;
    b.name = "B";
    b.width = 100;
    b.height = 100;
    b.area = 100 * 100;
    b.type = BlockType::Macro;
    p.blocks = {a, b};
    p.block_index = {{"A", 0}, {"B", 1}};
    p.outline_w = 200;
    p.outline_h = 300;
    p.alpha = 1.0;
    // No bundles -- nothing routed, so every channel's usage stays at its zero default and
    // channel_overflow is zero.

    Placement placement;
    placement.rects = {Rect{50, 0, 100, 100}, Rect{50, 200, 100, 100}};
    placement.aspect = {1.0, 1.0};
    placement.outline_w = 200;
    placement.outline_h = 300;

    RoutingResult routing = routeProblem(p, placement);
    assert(routing.channel_overflow <= kEps);

    Placement before = placement;
    bool changed = respaceToDemand(p, placement, routing);
    assert(!changed);
    assert(placement.rects.size() == before.rects.size());
    for (std::size_t i = 0; i < placement.rects.size(); ++i) {
        assert(placement.rects[i].x == before.rects[i].x);
        assert(placement.rects[i].y == before.rects[i].y);
        assert(placement.rects[i].w == before.rects[i].w);
        assert(placement.rects[i].h == before.rects[i].h);
    }
    assert(placement.outline_w == before.outline_w);
    assert(placement.outline_h == before.outline_h);
    std::cout << "testRespaceToDemandNoDemandIsNoOp ok\n";
}

// --- T4: respaceFixedPoint (route<->respace fixed point, ≤3 iters, monotone) ---
// design decision 5, p3-design.md §2.6.

// (a) case1 bstar seed1: the same known-overflow fixture as T3's test. Wrap a plain
// (non-caching) exactCached lambda around exactScore (deliberately not the SA's cache -- this
// test exercises respaceFixedPoint's own contract, not runPlacementSearch's caching).
//
// MEASURED (2026-07-07): this fixture's core blocks already reach x==MAX (outline_w == problem's
// declared MAX exactly; x-slack == 0), while y has ~220 units of slack. channelDemands charges
// large demands on BOTH axes, but relaxToDemand's MAX-secant clamp scales the (infeasible) x-axis
// demands down to ~0 while the y-axis-only widening goes through; re-routing after that shift
// moves net traffic onto the still-saturated, x-locked channels. Empirically (5 cases x {sp,
// bstar} x seed 1) NOT ONE combination yields a strict raw channel_overflow reduction within 3
// iterations on this axis-locked layout -- the loop's own best-tracking correctly recognizes this
// and falls back to the untouched baseline (or, for sp/case1, adopts a candidate with marginally
// lower total Cost despite a tiny overflow uptick, which is exactly what the design's
// outlineSweepBetter-style preference is defined to optimize -- Cost, not raw overflow). See
// p3-task-4-report.md for the full 5x2 measurement table. The strategic fix for axis-locked
// layouts is P2 (cost-driven routing steering flow off the locked axis), not more spacing here --
// tracked for the phase-level docs, not this test.
//
// So this test asserts the CONTRACT respaceFixedPoint actually promises on an axis-locked
// fixture: never regress (open_routes==0 first, then value <= baseline value + kEps, matching the
// preference rule the loop itself uses to pick `best`), legality, and determinism. The genuine
// overflow-reduction proof lives in the synthetic two-axis-slack fixture below.
void testRespaceFixedPointCase1BStarSeed1NeverRegresses() {
    Problem problem = parseProblem("test/2026 ICCAD_case.case1.csv");
    std::unique_ptr<PlacementEngine> engine = makePlacementEngine("bstar");
    std::mt19937 rng(1);
    PlacementState state = engine->initialState(problem, rng);
    Placement placement = engine->decode(problem, state);

    // Plain (non-caching) exactCached lambda: respaceFixedPoint must work with any conforming
    // ExactCachedFn, not just the SA's memoizing one.
    ExactCachedFn exactCached = [&](const Placement& p, RoutingResult* routing_out, Placement* placement_out) {
        return exactScore(problem, p, routing_out, placement_out);
    };

    RoutingResult baseline_routing;
    Score baseline_score = exactScore(problem, placement, &baseline_routing);
    // V5 re-baseline (2026-07-28): the raw decode no longer overflows under single-charge demand, so
    // pin non-vacuousness on channelDemands (the primitive the respace loop acts on) instead.
    assert(!channelDemands(problem, placement, baseline_routing).empty());

    RespaceResult result = respaceFixedPoint(problem, placement, exactCached);

    // Non-regression per the design contract (same preference rule the loop's own best-tracking
    // uses): open_routes==0 is never worse than baseline, and value is never worse than baseline
    // (+kEps slack for float noise) -- the loop must never hand back something strictly worse than
    // doing nothing.
    assert(result.routing.open_routes == 0);
    assert(baseline_routing.open_routes == 0);
    assert(result.score.value <= baseline_score.value + kEps);

    Problem scoped = problem;
    scoped.outline_w = result.placement.outline_w;
    scoped.outline_h = result.placement.outline_h;
    RoutingResult post_routing = routeProblem(scoped, result.placement);
    ValidationResult validation = validateSolution(scoped, result.placement, post_routing);
    if (!validation.ok) {
        for (const auto& m : validation.messages) std::cout << "  validation: " << m << "\n";
    }
    assert(validation.ok);

    // Determinism: two independent calls from the same inputs give byte-identical placements.
    RespaceResult result2 = respaceFixedPoint(problem, placement, exactCached);
    assert(result2.placement.rects.size() == result.placement.rects.size());
    for (std::size_t i = 0; i < result.placement.rects.size(); ++i) {
        assert(result2.placement.rects[i].x == result.placement.rects[i].x);
        assert(result2.placement.rects[i].y == result.placement.rects[i].y);
        assert(result2.placement.rects[i].w == result.placement.rects[i].w);
        assert(result2.placement.rects[i].h == result.placement.rects[i].h);
    }
    assert(result2.placement.outline_w == result.placement.outline_w);
    assert(result2.placement.outline_h == result.placement.outline_h);
    std::cout << "testRespaceFixedPointCase1BStarSeed1NeverRegresses ok\n";
}

// (b) synthetic two-axis-slack fixture -- the real RED->GREEN proof that the machinery CAN reduce
// overflow when the layout actually has genuine room to grow on the axis the demand needs.
//
// Geometry (transposed fixture (b) from the top of this file): Left/Right sit side by side
// (narrow 10-unit vertical gap between their own top edge and T's bottom edge); Bo spans the full
// width below both, T spans the full width above both -- so the channel between Left and Right is
// bounded below=Bo, above=T, with NEITHER Bo nor T sandwiched between Left/Right on the x-axis
// (they bound the channel via a DIFFERENT axis entirely, so relaxToDemand's V-pass can move T
// directly -- unlike a same-axis sandwich, e.g. a block sitting IN the very gap a demand charges,
// which this file's case1 test comment documents as NOT relievable by this design).
// Left->Right's bundle crosses the gap horizontally (dx), producing horizontal_usage on that
// channel -- capacity-limited by channel HEIGHT (hcap = 25*height) per the axis-crossing
// invariant, so relief needs need_h charged to the (below,above) = (Bo,T) pair, exactly what
// channelDemands derives. nets=3000 against a starting 10-unit gap (hcap=25*115=2875) forces real
// overflow (measured 125 below) that MAX (outline_h=500, ~380 units of slack beyond the tight
// ~220 packing) has ample room to relieve.
void testRespaceFixedPointSyntheticTwoAxisSlackReducesOverflow() {
    Problem p;
    p.blocks = {makeBlock("Left", 100, 100), makeBlock("Right", 100, 100), makeBlock("T", 300, 50),
                makeBlock("Bo", 300, 50)};
    p.block_index = {{"Left", 0}, {"Right", 1}, {"T", 2}, {"Bo", 3}};
    p.outline_w = 600;
    p.outline_h = 500;
    p.alpha = 1.0;
    Bundle bun;
    bun.source = 0;  // Left
    bun.target = 1;  // Right
    bun.nets = 3000;
    p.bundles = {bun};

    Placement pl;
    pl.rects = {Rect{0, 55, 100, 100}, Rect{200, 55, 100, 100}, Rect{0, 165, 300, 50}, Rect{0, 0, 300, 50}};
    pl.aspect = {1.0, 1.0, 1.0, 1.0};

    ExactCachedFn exactCached = [&](const Placement& cand, RoutingResult* routing_out, Placement* placement_out) {
        return exactScore(p, cand, routing_out, placement_out);
    };

    RoutingResult baseline_routing;
    Score baseline_score = exactScore(p, pl, &baseline_routing);
    assert(baseline_routing.channel_overflow > kEps);

    // Sanity: channelDemands actually derives a feasible need_h on the (below,above) pair from
    // this fixture, confirming the axis-crossing invariant applies as expected before asserting
    // the fixed point exploits it.
    std::vector<StripDemand> demands = channelDemands(p, pl, baseline_routing);
    bool found_feasible_need_h = false;
    for (const auto& d : demands) {
        if (d.need_h > kEps) found_feasible_need_h = true;
    }
    assert(found_feasible_need_h);

    RespaceResult result = respaceFixedPoint(p, pl, exactCached);

    assert(result.improved);
    assert(result.routing.channel_overflow < baseline_routing.channel_overflow - kEps);
    assert(result.routing.open_routes == 0);
    assert(result.score.value < baseline_score.value);

    Problem scoped = p;
    scoped.outline_w = result.placement.outline_w;
    scoped.outline_h = result.placement.outline_h;
    RoutingResult post_routing = routeProblem(scoped, result.placement);
    ValidationResult validation = validateSolution(scoped, result.placement, post_routing);
    if (!validation.ok) {
        for (const auto& m : validation.messages) std::cout << "  validation: " << m << "\n";
    }
    assert(validation.ok);

    // Determinism.
    RespaceResult result2 = respaceFixedPoint(p, pl, exactCached);
    assert(result2.placement.rects.size() == result.placement.rects.size());
    for (std::size_t i = 0; i < result.placement.rects.size(); ++i) {
        assert(result2.placement.rects[i].x == result.placement.rects[i].x);
        assert(result2.placement.rects[i].y == result.placement.rects[i].y);
        assert(result2.placement.rects[i].w == result.placement.rects[i].w);
        assert(result2.placement.rects[i].h == result.placement.rects[i].h);
    }
    assert(result2.routing.channel_overflow == result.routing.channel_overflow);
    std::cout << "testRespaceFixedPointSyntheticTwoAxisSlackReducesOverflow ok\n";
}

// T5 (design p3-design.md §5, plan Task 5): the elite pass's per-candidate respace hook
// (`maybeAdoptRespace`, file-local to search.cpp, mirroring outlineSweepBetter's own
// file-local precedent -- see p1-design's outlineSweep tests, which exercise it only through
// runPlacementSearch/outlineSweep rather than a standalone unit hook). case1/bstar/seed1 is the
// SAME axis-locked fixture T4's never-regresses test already pins (raw re-decode has no slack on
// the locked axis, so `respaceFixedPoint` legitimately returns improved=false there -- see
// respaceFixedPointCase1BStarSeed1NeverRegresses above and p3-task-4-report.md). Asserting a
// strictly-lower post-hook channel_overflow here would therefore be a FALSE requirement on this
// fixture; the honest, non-fragile contract this test pins is: (1) the end-to-end search still
// reaches open_routes==0 and validateSolution().ok with the hook wired in (never-worse, since
// maybeAdoptRespace only adopts a strictly-better candidate per outlineSweepBetter's own rule),
// and (2) determinism is preserved (byte-identical output across two identical runs) despite the
// added respaceFixedPoint calls in the elite loop. Cost-movement evidence (the cases where
// respacing genuinely lowers overflow) lands in T7's full sweep, not here.
void testEliteRespaceHookEndToEndCase1BStarSeed1() {
    Problem problem = parseProblem("test/2026 ICCAD_case.case1.csv");
    std::unique_ptr<PlacementEngine> engine = makePlacementEngine("bstar");
    SearchOptions options;
    options.iters = 500;
    options.seed = 1;
    options.exact_interval = 50;

    SearchResult result = runPlacementSearch(problem, *engine, options);
    assert(result.best_routing.open_routes == 0);

    Problem scoped = problem;
    scoped.outline_w = result.best_placement.outline_w;
    scoped.outline_h = result.best_placement.outline_h;
    RoutingResult post_routing = routeProblem(scoped, result.best_placement);
    ValidationResult validation = validateSolution(scoped, result.best_placement, post_routing);
    if (!validation.ok) {
        for (const auto& m : validation.messages) std::cout << "  validation: " << m << "\n";
    }
    assert(validation.ok);

    // Determinism: the elite loop's added respaceFixedPoint calls are pure/deterministic
    // (no RNG, per respace.hpp's contract), so a second identical run must reproduce the exact
    // same best placement byte-for-byte.
    SearchResult result2 = runPlacementSearch(problem, *engine, options);
    assert(result2.best_placement.rects.size() == result.best_placement.rects.size());
    for (std::size_t i = 0; i < result.best_placement.rects.size(); ++i) {
        assert(result2.best_placement.rects[i].x == result.best_placement.rects[i].x);
        assert(result2.best_placement.rects[i].y == result.best_placement.rects[i].y);
        assert(result2.best_placement.rects[i].w == result.best_placement.rects[i].w);
        assert(result2.best_placement.rects[i].h == result.best_placement.rects[i].h);
    }
    assert(result2.best_placement.outline_w == result.best_placement.outline_w);
    assert(result2.best_placement.outline_h == result.best_placement.outline_h);
    assert(result2.best_score.value == result.best_score.value);
    std::cout << "testEliteRespaceHookEndToEndCase1BStarSeed1 ok\n";
}

// T6 (design p3-design.md §3.4/§5, plan Task 6): a CONGESTED variant of the P0 FT-forced fixture
// family (`forcedFtProblem`/`rowPlacement`/`testFinalizeFeedthroughDirect`,
// test/regression_p0_20260706.cpp:47-81,235-270) -- same row (BLK01 macro | BLK02 soft | BLK03
// macro) with the same top wall (BLK04) forcing BLK01<->BLK03 through feedthrough (nonzero
// feedthrough_mismatch precondition), PLUS an added macro BLK05 to the right of BLK03 with a real
// x-gap (BLK03 right edge=500, BLK05 at x=[540,600]) and a SHORT shared y-band (both start at
// y=0, height 60) so extractChannels carves a genuine channel (CH4, width=40) between them. A
// heavy BLK03->BLK05 bundle (3200 nets) routes straight through CH4: the guide-point hop has both
// a dx and dy component (BLK05 is short relative to BLK03), so BOTH horizontal_usage and
// vertical_usage land on CH4 at 3200 -- vcap = 25*40 = 1000, so vertical_usage alone overflows by
// 2200 (verified empirically against routing.cpp's usage accrual before writing this fixture).
// This gives a real, nonzero precondition on BOTH axes finalizeFeedthrough must handle at once:
// feedthrough_mismatch > 0 (FT under-provisioned) AND channel_overflow > 0 (congested channel).
Problem congestedFtProblem() {
    Problem p;
    auto addBlock = [&](const std::string& name, double area, double w, double h, BlockType type) {
        Block b;
        b.name = name;
        b.area = area;
        b.width = w;
        b.height = h;
        b.type = type;
        b.ar_min = 0.25;
        b.ar_max = 4.0;
        b.ft_rates = {0.2, 0.4, 0.8, 1.0};
        p.block_index[name] = static_cast<int>(p.blocks.size());
        p.blocks.push_back(b);
    };
    addBlock("BLK01", 30000.0, 100.0, 300.0, BlockType::Macro);
    addBlock("BLK02", 90000.0, 300.0, 300.0, BlockType::Soft);
    addBlock("BLK03", 30000.0, 100.0, 300.0, BlockType::Macro);
    addBlock("BLK04", 620.0 * 40.0, 620.0, 40.0, BlockType::Macro);   // top wall: forces FT
    addBlock("BLK05", 60.0 * 60.0, 60.0, 60.0, BlockType::Macro);     // congestion target
    p.outline_w = 620.0;
    p.outline_h = 700.0;  // headroom for BStarEngine's re-stack on inflated re-decode
    p.alpha = 1.0;
    Bundle ft_bundle;
    ft_bundle.source = 0;
    ft_bundle.target = 2;
    ft_bundle.nets = 900;
    Bundle congestion_bundle;
    congestion_bundle.source = 2;
    congestion_bundle.target = 4;
    congestion_bundle.nets = 3200;
    p.bundles = {ft_bundle, congestion_bundle};
    return p;
}

Placement congestedFtPlacement() {
    Placement pl;
    pl.rects = {Rect{0.0, 0.0, 100.0, 300.0}, Rect{100.0, 0.0, 300.0, 300.0}, Rect{400.0, 0.0, 100.0, 300.0},
                Rect{0.0, 300.0, 620.0, 40.0}, Rect{540.0, 0.0, 60.0, 60.0}};
    pl.aspect = {1.0, 1.0, 1.0, 1.0, 1.0};
    return pl;
}

// B1 amendment (2026-07-24, docs/research/2026-07-23-research-refresh-and-strategy.md §2 +
// .superpowers/sdd/b1-brief-amended.md): repurposed from asserting finalizeFeedthrough's REAL commit
// always reaches mismatch 0 (the pre-B1 objective, no longer correct once mismatch is priced into
// value and paydown is value-gated) to asserting the new invariant directly -- best_score.value never
// regresses past its own pre-finalize value -- while PRESERVING the fixture's original proof, now
// aimed at the read-only LEGACY SIMULATION (legacy_paydown_*): it must still pay this synthetic's
// mismatch all the way to 0, exactly as the pre-B1 rule did, since tryLegacyCommit replicates that
// rule verbatim (see finalizeFeedthrough).
void testFinalizeFeedthroughRespaceCongestedVariant() {
    Problem p = congestedFtProblem();
    Placement pl = congestedFtPlacement();

    SearchResult result;
    result.best_placement = pl;
    result.best_score = exactScore(p, pl, &result.best_routing);
    // Precondition: FT under-provisioning is real before finalize. Beta Task 3: with per-edge channel
    // demand the cost-driven router now diverts this bundle through BLK02's feedthrough rather than
    // overflowing the channel, so channel_overflow is 0 here (was >0 under the single-charge model) --
    // the fixture now exercises finalize's FT paydown alone, which still drives the LEGACY mismatch to
    // 0 below (and the real, value-gated best to at least as good as its own pre-finalize value).
    assert(result.best_routing.feedthrough_mismatch > kEps);
    const double pre_finalize_value = result.best_score.value;

    std::unique_ptr<PlacementEngine> engine = makePlacementEngine("bstar");
    finalizeFeedthrough(p, *engine, result);

    // NEW invariant (B1): the real, value-gated commit never regresses value past pre-finalize --
    // finalizeFeedthrough's monotone accept rule guarantees this by construction; this pins it.
    assert(result.best_score.value <= pre_finalize_value + kEps);
    assert(result.best_routing.open_routes == 0);
    auto validation = validateSolution(p, result.best_placement, result.best_routing);
    if (!validation.ok) {
        for (const auto& m : validation.messages) std::cout << "  validation: " << m << "\n";
    }
    assert(validation.ok);

    // Original fixture's proof, preserved: the LEGACY SIMULATION (verbatim replica of the pre-B1
    // mismatch-first rule) still pays this synthetic's mismatch all the way down to 0 and remains a
    // legal, validated candidate -- confirming tryLegacyCommit reproduces the old rule faithfully.
    assert(result.legacy_paydown_routing.feedthrough_mismatch <= kEps);
    assert(result.legacy_paydown_routing.open_routes == 0);
    auto legacy_validation = validateSolution(p, result.legacy_paydown_placement, result.legacy_paydown_routing);
    if (!legacy_validation.ok) {
        for (const auto& m : legacy_validation.messages) std::cout << "  legacy validation: " << m << "\n";
    }
    assert(legacy_validation.ok);

    // Determinism: finalizeFeedthrough (and the respaceFixedPoint calls now inside it) use a
    // fixed RNG seed and no other nondeterminism -- two independent runs from the same
    // precondition must commit byte-identical results, for both the real best and the legacy sim.
    SearchResult result2;
    result2.best_placement = pl;
    result2.best_score = exactScore(p, pl, &result2.best_routing);
    finalizeFeedthrough(p, *engine, result2);
    assert(result2.best_placement.rects.size() == result.best_placement.rects.size());
    for (std::size_t i = 0; i < result.best_placement.rects.size(); ++i) {
        assert(result2.best_placement.rects[i].x == result.best_placement.rects[i].x);
        assert(result2.best_placement.rects[i].y == result.best_placement.rects[i].y);
        assert(result2.best_placement.rects[i].w == result.best_placement.rects[i].w);
        assert(result2.best_placement.rects[i].h == result.best_placement.rects[i].h);
    }
    assert(result2.best_score.value == result.best_score.value);
    assert(result2.legacy_paydown_score.value == result.legacy_paydown_score.value);
    std::cout << "testFinalizeFeedthroughRespaceCongestedVariant ok\n";
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
    testTieLosingCandidateCannotOverlapDiscardingBlock();
    testDemandKeyedPairAppliesEvenWhenAnotherBlockWinsPredecessorSlot();
    testRespaceToDemandCase1BStarSeed1();
    testRespaceToDemandNoDemandIsNoOp();
    testRespaceFixedPointCase1BStarSeed1NeverRegresses();
    testRespaceFixedPointSyntheticTwoAxisSlackReducesOverflow();
    testEliteRespaceHookEndToEndCase1BStarSeed1();
    testFinalizeFeedthroughRespaceCongestedVariant();
    std::cout << "P3 T1+T2+T3+T4+T5+T6 regressions passed\n";
    return 0;
}
