// B7 (capacity-profit interior-gap rungs, .superpowers/sdd/b7-brief.md): regression coverage for
// capacityGapCandidates' analytic profit ranking (determinism, count/slack-driven ordering with
// explicit tie-breaks, the structural O<=0/C<=0 early-outs, wall/edge-type "to" exclusion) and
// applyGapCandidate's "widen the EXISTING gap by delta" mechanics, plus one small end-to-end
// fixture that runs the REAL router (routeProblem) before and after applying the top-ranked
// candidate, demonstrating the mechanism actually grows channel capacity the way the profit key
// predicts. Real-case adoption evidence (case4, -5.99% aligned, reproducible across repeated
// runs) is reported in b7-report.md, not reproduced here -- a full case4 run takes ~2 minutes,
// impractical for this suite's own runtime budget (run_selfconsistency_regressions.sh is the
// existing precedent for where that cost belongs instead).
#include <cassert>
#include <cmath>
#include <iostream>
#include <vector>

#include "../src/model.hpp"
#include "../src/placement_engine.hpp"
#include "../src/respace.hpp"
#include "../src/routing.hpp"

namespace {
using namespace efgr;

constexpr double kTol = 1e-6;

Block makeBlock(const std::string& name, double w, double h) {
    Block b;
    b.name = name;
    b.width = w;
    b.height = h;
    b.area = w * h;
    b.type = BlockType::Macro;
    return b;
}

bool sameCandidates(const std::vector<CapacityGapCandidate>& a, const std::vector<CapacityGapCandidate>& b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (a[i].from != b[i].from || a[i].to != b[i].to || a[i].horizontal != b[i].horizontal ||
            std::abs(a[i].delta - b[i].delta) > kTol || std::abs(a[i].profit - b[i].profit) > kTol) {
            return false;
        }
    }
    return true;
}

// ---- structural early-outs -------------------------------------------------------------------

void testEmptyWhenNoOverflow() {
    Problem p;
    p.blocks = {makeBlock("A", 100, 100)};
    Placement pl;
    pl.rects = {Rect{0, 0, 100, 100}};
    pl.aspect = {1.0};
    pl.outline_w = 200;
    pl.outline_h = 200;

    RoutingResult routing;
    routing.channel_overflow_nets = 0.0;
    routing.channel_capacity_total = 1000.0;
    assert(capacityGapCandidates(p, pl, routing).empty());
    std::cout << "testEmptyWhenNoOverflow ok\n";
}

void testEmptyWhenNoCapacity() {
    Problem p;
    p.blocks = {makeBlock("A", 100, 100)};
    Placement pl;
    pl.rects = {Rect{0, 0, 100, 100}};
    pl.aspect = {1.0};
    pl.outline_w = 200;
    pl.outline_h = 200;

    RoutingResult routing;
    routing.channel_overflow_nets = 5.0;
    routing.channel_capacity_total = 0.0;
    assert(capacityGapCandidates(p, pl, routing).empty());
    std::cout << "testEmptyWhenNoCapacity ok\n";
}

// ---- enumeration exclusion (unrealizable "to" sides) -----------------------------------------

// Channel bounded left=L (core), right=EdgeR (Edge-type) -- an edge-type `to` is never a member
// of relaxToDemand's `core` node set, so this gap is structurally unrealizable via
// applyGapCandidate and must be excluded at enumeration time (respace.hpp's own doc comment).
void testExcludesEdgeTypeToSide() {
    Problem p;
    p.blocks = {makeBlock("L", 50, 100), makeBlock("EdgeR", 50, 100)};
    p.blocks[1].type = BlockType::Edge;
    Placement pl;
    pl.rects = {Rect{0, 0, 50, 100}, Rect{150, 0, 50, 100}};
    pl.aspect = {1.0, 1.0};
    pl.outline_w = 500;
    pl.outline_h = 500;

    RoutingResult routing;
    Channel ch;
    ch.rect = Rect{50, 0, 100, 100};  // left=L(idx0,core), right=EdgeR(idx1,edge)
    routing.channels = {ch};
    routing.channel_overflow_nets = 10.0;
    routing.channel_capacity_total = 1000.0;

    assert(capacityGapCandidates(p, pl, routing).empty());
    std::cout << "testExcludesEdgeTypeToSide ok\n";
}

// Channel touching the outline's own right wall directly -- bounds.right==-1 (a "to"==wall demand
// is never read by relaxAxis's per-block loop, which only iterates CORE block targets), so this
// must also be excluded.
void testExcludesOutlineWallToSide() {
    Problem p;
    p.blocks = {makeBlock("L", 50, 100)};
    Placement pl;
    pl.rects = {Rect{0, 0, 50, 100}};
    pl.aspect = {1.0};
    pl.outline_w = 500;
    pl.outline_h = 500;

    RoutingResult routing;
    Channel ch;
    ch.rect = Rect{50, 0, 450, 100};  // left=L(idx0,core), right touches x=500 (outline wall)
    routing.channels = {ch};
    routing.channel_overflow_nets = 10.0;
    routing.channel_capacity_total = 1000.0;

    assert(capacityGapCandidates(p, pl, routing).empty());
    std::cout << "testExcludesOutlineWallToSide ok\n";
}

// ---- determinism + profit ordering (count-driven DeltaC, ample slack -> DeltaA==0) ------------

// Two hand-built channels both resolve (via channelBounds) to the SAME (left=L,right=R) pair --
// count=2 for that key -- plus one channel resolving to a DIFFERENT (below=P,above=Q) pair --
// count=1. Ample outline slack on both axes makes DeltaA==0 for every candidate, isolating
// DeltaC's count multiplier: profit == coeff_c*25*delta*count exactly, hand-verified below.
std::vector<CapacityGapCandidate> buildCountOrderingFixture(Problem& p, Placement& pl) {
    p.blocks = {makeBlock("L", 50, 300), makeBlock("R", 50, 300), makeBlock("P", 50, 100), makeBlock("Q", 50, 100)};
    p.outline_w = 1000;
    p.outline_h = 1000;
    p.alpha = 1.0;

    pl.rects = {Rect{0, 0, 50, 300}, Rect{300, 0, 50, 300}, Rect{500, 0, 50, 100}, Rect{500, 300, 50, 100}};
    pl.aspect = {1.0, 1.0, 1.0, 1.0};
    pl.outline_w = 1000;
    pl.outline_h = 1000;

    RoutingResult routing;
    Channel ch_lo, ch_hi, ch_pq;
    ch_lo.rect = Rect{50, 0, 250, 100};    // x=[50,300] y=[0,100]   -> left=L(0), right=R(1)
    ch_hi.rect = Rect{50, 150, 250, 100};  // x=[50,300] y=[150,250] -> left=L(0), right=R(1) (SAME pair)
    ch_pq.rect = Rect{500, 100, 50, 200};  // x=[500,550] y=[100,300] -> below=P(2), above=Q(3)
    routing.channels = {ch_lo, ch_hi, ch_pq};
    routing.channel_overflow_nets = 100.0;
    routing.channel_capacity_total = 100000.0;

    return capacityGapCandidates(p, pl, routing);
}

void testDeterministicAcrossInvocations() {
    Problem p1, p2;
    Placement pl1, pl2;
    std::vector<CapacityGapCandidate> a = buildCountOrderingFixture(p1, pl1);
    std::vector<CapacityGapCandidate> b = buildCountOrderingFixture(p2, pl2);
    assert(!a.empty());
    assert(sameCandidates(a, b));
    std::cout << "testDeterministicAcrossInvocations ok (n=" << a.size() << ")\n";
}

// Hand-derived expectation (see comment block below for the arithmetic): A=1e6, O=100, C=1e5,
// rate=1e-3, coeff_c=0.2, both axes' slack (450, 600) >> every delta so DeltaA==0 everywhere ->
// profit(count,delta) = 0.2*25*delta*count = 5*delta*count. (L,R,true) count=2 gives 10/20/40 for
// delta=1/2/4; (P,Q,false) count=1 gives 5/10/20. Two exact ties occur (20 and 10), each resolved
// by the "horizontal before vertical" tie-break -- both the values AND the tie resolution are
// asserted below.
void testProfitOrderingByCountWithTieBreaks() {
    Problem p;
    Placement pl;
    std::vector<CapacityGapCandidate> cands = buildCountOrderingFixture(p, pl);
    assert(cands.size() == 6);

    struct Expected {
        int from, to;
        bool horizontal;
        double delta, profit;
    };
    const Expected expected[6] = {
        {0, 1, true, 4.0, 40.0},   {0, 1, true, 2.0, 20.0},  {2, 3, false, 4.0, 20.0},
        {0, 1, true, 1.0, 10.0},   {2, 3, false, 2.0, 10.0}, {2, 3, false, 1.0, 5.0},
    };
    for (int i = 0; i < 6; ++i) {
        const CapacityGapCandidate& c = cands[static_cast<std::size_t>(i)];
        assert(c.from == expected[i].from);
        assert(c.to == expected[i].to);
        assert(c.horizontal == expected[i].horizontal);
        assert(std::abs(c.delta - expected[i].delta) < kTol);
        assert(std::abs(c.profit - expected[i].profit) < kTol);
    }
    std::cout << "testProfitOrderingByCountWithTieBreaks ok\n";
}

// ---- DeltaA (slack-exhausted) path --------------------------------------------------------

// Fully MAX-locked fixture (outline_w/h exactly equal the tight span on BOTH axes, slack==0) --
// forces DeltaA=delta (not 0) for every candidate, exercising the branch
// testProfitOrderingByCountWithTieBreaks never reaches. One channel resolves to BOTH a
// (left=L,right=R) and a (below=A,above=B) pair simultaneously (count=1 each); since slack==0 on
// both axes here, the two axes' profit formulas are numerically identical per delta, so this also
// re-confirms the tie-break (horizontal ranks first at an exact tie).
void testDeltaAKicksInWhenSlackExhausted() {
    Problem p;
    p.blocks = {makeBlock("A", 100, 100), makeBlock("B", 100, 100), makeBlock("L", 50, 300), makeBlock("R", 50, 300)};
    p.outline_w = 200;
    p.outline_h = 300;
    p.alpha = 1.0;

    Placement pl;
    pl.rects = {Rect{50, 0, 100, 100}, Rect{50, 200, 100, 100}, Rect{0, 0, 50, 300}, Rect{150, 0, 50, 300}};
    pl.aspect = {1.0, 1.0, 1.0, 1.0};
    pl.outline_w = 200;
    pl.outline_h = 300;

    RoutingResult routing;
    Channel ch;
    ch.rect = Rect{50, 100, 100, 100};  // x=[50,150] y=[100,200] -> left=L(2),right=R(3),below=A(0),above=B(1)
    routing.channels = {ch};
    routing.channel_overflow_nets = 50.0;
    routing.channel_capacity_total = 5000.0;

    std::vector<CapacityGapCandidate> cands = capacityGapCandidates(p, pl, routing);
    assert(cands.size() == 6);
    // coeff_c = 20*60000*0.01/5000 = 2.4, coeff_a = 1+20*0.01 = 1.2; DeltaA==delta (slack==0):
    // profit(delta) = 2.4*25*delta*1 - delta*1.2 = 58.8 (d=1) / 117.6 (d=2) / 235.2 (d=4).
    const double expected_profit[3] = {235.2, 117.6, 58.8};  // delta=4,2,1 in that (descending) order
    const double expected_delta[3] = {4.0, 2.0, 1.0};
    for (int rung = 0; rung < 3; ++rung) {
        // Each rung contributes a tied pair (horizontal, then vertical, per the tie-break).
        const CapacityGapCandidate& first = cands[static_cast<std::size_t>(rung * 2)];
        const CapacityGapCandidate& second = cands[static_cast<std::size_t>(rung * 2 + 1)];
        assert(std::abs(first.delta - expected_delta[rung]) < kTol);
        assert(std::abs(second.delta - expected_delta[rung]) < kTol);
        assert(std::abs(first.profit - expected_profit[rung]) < kTol);
        assert(std::abs(second.profit - expected_profit[rung]) < kTol);
        assert(first.horizontal && !second.horizontal);
        assert(first.from == 2 && first.to == 3);
        assert(second.from == 0 && second.to == 1);
    }
    std::cout << "testDeltaAKicksInWhenSlackExhausted ok\n";
}

// ---- applyGapCandidate: widen-by-exact-delta mechanics -----------------------------------------

void testApplyGapCandidateWidensExistingGapByExactDelta() {
    Problem p;
    p.blocks = {makeBlock("L", 50, 100), makeBlock("R", 50, 100)};
    p.outline_w = 300;  // ample slack (MAX -- applyGapCandidate's legality gate bounds against this,
    p.outline_h = 100;  // not the Placement's own outline_w/h stamp, which gets overwritten anyway)
    Placement pl;
    pl.rects = {Rect{0, 0, 50, 100}, Rect{100, 0, 50, 100}};  // gap = 50
    pl.aspect = {1.0, 1.0};
    pl.outline_w = 300;
    pl.outline_h = 100;

    CapacityGapCandidate gap{0, 1, true, 4.0, 0.0};
    Placement widened = pl;
    bool ok = applyGapCandidate(p, widened, gap);
    assert(ok);
    assert(std::abs(widened.rects[0].x - 0.0) < kTol);  // L (the "from" side) never moves
    assert(std::abs(widened.rects[1].x - 104.0) < kTol);  // R pushed by exactly delta (100+4)
    std::cout << "testApplyGapCandidateWidensExistingGapByExactDelta ok\n";
}

void testApplyGapCandidateVerticalAxisWidensByExactDelta() {
    Problem p;
    p.blocks = {makeBlock("Below", 100, 50), makeBlock("Above", 100, 50)};
    p.outline_w = 100;  // MAX -- see the horizontal-axis test above for why this must be set here too
    p.outline_h = 400;  // (ample slack)
    Placement pl;
    pl.rects = {Rect{0, 0, 100, 50}, Rect{0, 100, 100, 50}};  // gap = 50
    pl.aspect = {1.0, 1.0};
    pl.outline_w = 100;
    pl.outline_h = 400;

    CapacityGapCandidate gap{0, 1, false, 2.0, 0.0};
    Placement widened = pl;
    bool ok = applyGapCandidate(p, widened, gap);
    assert(ok);
    assert(std::abs(widened.rects[0].y - 0.0) < kTol);
    assert(std::abs(widened.rects[1].y - 102.0) < kTol);  // Above pushed by exactly delta (100+2)
    std::cout << "testApplyGapCandidateVerticalAxisWidensByExactDelta ok\n";
}

// ---- end-to-end: real routeProblem before/after -------------------------------------------------

// A below, B above, L/R flank the A-B gap in x (same fixture shape as
// test/regression_p3_20260707.cpp's fixture (a), generous outline this time so DeltaA==0
// everywhere). Real routeProblem supplies the channel geometry (not hand-mocked); a synthetic
// overflow is layered on top (a 5-net bundle through a 100x100 channel -- cap=25*100=2500 nets --
// never naturally overflows at contest scale, so this isolates "does the mechanism correctly rank
// and apply given REAL geometry" from "can this tiny fixture organically overflow", which is not
// what this test checks). Real-case evidence that a genuine, organically-overflowing case adopts
// a rung is in b7-report.md (case4, -5.99%).
void testEndToEndRealRoutingRanksAndAppliesProfitableGap() {
    Problem p;
    p.blocks = {makeBlock("A", 100, 100), makeBlock("B", 100, 100), makeBlock("L", 50, 300), makeBlock("R", 50, 300)};
    p.block_index = {{"A", 0}, {"B", 1}, {"L", 2}, {"R", 3}};
    p.outline_w = 400;
    p.outline_h = 400;
    p.alpha = 1.0;
    Bundle bun;
    bun.source = 0;
    bun.target = 1;
    bun.nets = 5;
    p.bundles = {bun};

    Placement pl;
    pl.rects = {Rect{50, 0, 100, 100}, Rect{50, 200, 100, 100}, Rect{0, 0, 50, 300}, Rect{150, 0, 50, 300}};
    pl.aspect = {1.0, 1.0, 1.0, 1.0};
    pl.outline_w = 400;
    pl.outline_h = 400;

    RoutingResult routing = routeProblem(p, pl);
    assert(routing.open_routes == 0);
    assert(!routing.channels.empty());
    const double real_capacity = routing.channel_capacity_total;
    assert(real_capacity > 0.0);
    routing.channel_overflow_nets = 500.0;  // synthetic overflow layered on real geometry

    std::vector<CapacityGapCandidate> cands = capacityGapCandidates(p, pl, routing);
    assert(!cands.empty());
    const CapacityGapCandidate top = cands.front();
    assert(top.profit > 0.0);
    const bool is_lr = top.horizontal && top.from == 2 && top.to == 3;
    const bool is_ab = !top.horizontal && top.from == 0 && top.to == 1;
    assert(is_lr || is_ab);

    Placement widened = pl;
    bool applied = applyGapCandidate(p, widened, top);
    assert(applied);

    RoutingResult widened_routing = routeProblem(p, widened);
    assert(widened_routing.open_routes == 0);
    assert(widened_routing.channel_capacity_total > real_capacity + kTol);
    std::cout << "testEndToEndRealRoutingRanksAndAppliesProfitableGap ok (delta_capacity="
              << (widened_routing.channel_capacity_total - real_capacity) << ")\n";
}

}  // namespace

int main() {
    testEmptyWhenNoOverflow();
    testEmptyWhenNoCapacity();
    testExcludesEdgeTypeToSide();
    testExcludesOutlineWallToSide();
    testDeterministicAcrossInvocations();
    testProfitOrderingByCountWithTieBreaks();
    testDeltaAKicksInWhenSlackExhausted();
    testApplyGapCandidateWidensExistingGapByExactDelta();
    testApplyGapCandidateVerticalAxisWidensByExactDelta();
    testEndToEndRealRoutingRanksAndAppliesProfitableGap();
    std::cout << "b7 regressions passed\n";
    return 0;
}
