// C3 (exact local-group LNS re-placement, .superpowers/sdd/c3-wlrec-briefs-20260817.md BRIEF A):
// regression coverage for the pure lns_polish.cpp module -- A2's candidate-grid completeness on a
// hand-derivable 3-blockage fixture, the pipeline's structural pre-gates (unstamped/illegal/
// movable<2), determinism, per-iterate legality, edge-rect/outline invariance, integer-in-integer-out
// (a fractional wireMedianTarget rounds to an exact integer breakpoint), a known-analytic 2-member
// group flush-close, incumbent-never-worse-than-status-quo under the proxy, and node-cap anytime
// legality. Links ONLY src/lns_polish.cpp + src/placement_engine.cpp (see
// test/run_lns_regressions.sh) -- no search.cpp, no routing, no exactCached: this suite exercises
// group selection and the B&B enumerator in isolation, exactly as BRIEF A's own file-plan scopes it.
#include <algorithm>
#include <cassert>
#include <cmath>
#include <iostream>
#include <vector>

#include "../src/lns_polish.hpp"
#include "../src/model.hpp"

namespace {
using namespace efgr;

constexpr double kTol = 1e-6;

Block makeBlock(const std::string& name, double w, double h, BlockType type = BlockType::Macro) {
    Block b;
    b.name = name;
    b.width = w;
    b.height = h;
    b.area = w * h;
    b.type = type;
    return b;
}

// Mirrors lns_polish.cpp's own file-local `placementLegalWithin` (respace.cpp:373-382 semantics) --
// reproduced here since it is not exposed via lns_polish.hpp, same precedent
// test/regression_c1_20260817.cpp's own local `legalWithin` already set for wl_redistribute.cpp.
bool legalWithin(const Placement& pl, double w, double h) {
    for (std::size_t i = 0; i < pl.rects.size(); ++i) {
        const Rect& r = pl.rects[i];
        if (r.x < -kTol || r.y < -kTol || r.right() > w + kTol || r.top() > h + kTol) return false;
        for (std::size_t j = i + 1; j < pl.rects.size(); ++j) {
            if (overlaps(r, pl.rects[j])) return false;
        }
    }
    return true;
}

bool placementsClose(const Placement& a, const Placement& b) {
    if (a.rects.size() != b.rects.size()) return false;
    for (std::size_t i = 0; i < a.rects.size(); ++i) {
        const Rect& ra = a.rects[i];
        const Rect& rb = b.rects[i];
        if (std::abs(ra.x - rb.x) > kTol || std::abs(ra.y - rb.y) > kTol || std::abs(ra.w - rb.w) > kTol ||
            std::abs(ra.h - rb.h) > kTol) {
            return false;
        }
    }
    return std::abs(a.outline_w - b.outline_w) <= kTol && std::abs(a.outline_h - b.outline_h) <= kTol;
}

// A3's primary term (bundle-weighted facing-edge gap, same L1b formula eval.cpp's cheapScore and
// wl_redistribute.cpp's own axisTarget use) restricted to bundles touching `group` -- a bundle with
// exactly one group endpoint is `own[i][c]`'s contribution, a bundle between two group members is
// `pairTerm`'s; summing gapx+gapy*nets over every group-touching bundle covers both without needing
// to reproduce the internal own/pairTerm split.
double groupGapObjective(const Problem& p, const Placement& pl, const std::vector<int>& group) {
    double total = 0.0;
    for (const auto& b : p.bundles) {
        if (b.source == b.target) continue;
        bool touches = std::find(group.begin(), group.end(), b.source) != group.end() ||
                      std::find(group.begin(), group.end(), b.target) != group.end();
        if (!touches) continue;
        const Rect& a = pl.rects[static_cast<std::size_t>(b.source)];
        const Rect& c = pl.rects[static_cast<std::size_t>(b.target)];
        double gapx = std::max(0.0, std::max(a.x - c.right(), c.x - a.right()));
        double gapy = std::max(0.0, std::max(a.y - c.top(), c.y - a.top()));
        total += (gapx + gapy) * b.nets;
    }
    return total;
}

// ---- shared fixtures ----------------------------------------------------------------------------

// Three fixed macro blockages spanning the full outline height (y=[0,200]) at x=[0,100], [400,500],
// [800,900], leaving three corridors ([100,400], [500,800], [900,1000]) -- M (bundle-less, so its own
// candidate grid is exercised in isolation via lnsCandidatePositions, not the full B&B) sits inside
// the middle corridor at x=[600,650]. Hand-derivable maximal empty rectangles and their expected
// clamp(median,R) representative points are documented in the completeness test itself.
void buildThreeBlockageFixture(Problem& p, Placement& pl) {
    p.blocks = {makeBlock("B1", 100, 200), makeBlock("B2", 100, 200), makeBlock("B3", 100, 200),
               makeBlock("M", 50, 200)};
    pl.rects = {Rect{0, 0, 100, 200}, Rect{400, 0, 100, 200}, Rect{800, 0, 100, 200}, Rect{600, 0, 50, 200}};
    pl.aspect = {1.0, 1.0, 1.0, 1.0};
    pl.outline_w = 1000;
    pl.outline_h = 200;
}

// L (edge, ODD width 101 @ x=0) -- R (edge, ODD width 103 @ x=897, so R spans exactly to the outline
// edge W=1000) -- M1 (macro 50x100 @ x=400, bundled ONLY to L) -- M2 (macro 50x100 @ x=500, bundled
// ONLY to R), NO bundle between M1/M2. Odd partner widths make wireMedianTarget's partner-center land
// on a half-integer (L center=(50.5,50), R center=(948.5,50)) -- since centering M1/M2 on a FIXED
// partner's center always overlaps that same partner (the whole point of the pull), this fractional
// raw candidate is always filtered out as illegal here (see testIntegerInIntegerOut's OWN dedicated
// fixture below for a case where a fractional candidate survives legally). The primary objective's
// unique global minimum (p=0) is M1 exactly flush against L (x=101=L.right()) and M2 exactly flush
// against R (x=847=R.x-M2.w), simultaneously feasible (no mutual overlap) -- a hand-derivable
// known-analytic close, like C1a's own 3-block fixture.
void buildTwoBlockGroupFixture(Problem& p, Placement& pl) {
    p.blocks = {makeBlock("L", 101, 100, BlockType::Edge), makeBlock("R", 103, 100, BlockType::Edge),
               makeBlock("M1", 50, 100), makeBlock("M2", 50, 100)};
    p.bundles = {Bundle{0, 2, 1}, Bundle{1, 3, 1}};
    pl.rects = {Rect{0, 0, 101, 100}, Rect{897, 0, 103, 100}, Rect{400, 0, 50, 100}, Rect{500, 0, 50, 100}};
    pl.aspect = {1.0, 1.0, 1.0, 1.0};
    pl.outline_w = 1000;
    pl.outline_h = 100;
}

// M1 (macro 50x100 @ x=100) bundled ONLY to M2 (macro, ODD width 51, @ x=700) -- BOTH in the SAME
// group, and no other blocks at all. A2's "FIXED rects" for M1's own candidate grid are `base.rects`
// EXCLUDING group members, so M2 (M1's only bundle partner) is excluded from that legality filter --
// unlike buildTwoBlockGroupFixture above (where the partner is a FIXED edge, and centering on it
// always self-overlaps), M1's median-derived candidate here is checked only against the (empty) fixed
// set + the outline bounds, so it survives. M2's center = 700+25.5 = 725.5 (odd width => half-integer
// center); M1's median target mx = 725.5 - 25 (M1's own half-width) = 700.5, a genuine round-half
// case -- llround (round-half-away-from-zero) must produce exactly 701, not 700 or a truncation.
void buildGroupBundleIntegerFixture(Problem& p, Placement& pl) {
    p.blocks = {makeBlock("M1", 50, 100), makeBlock("M2", 51, 100)};
    p.bundles = {Bundle{0, 1, 1}};
    pl.rects = {Rect{100, 0, 50, 100}, Rect{700, 0, 51, 100}};
    pl.aspect = {1.0, 1.0};
    pl.outline_w = 1000;
    pl.outline_h = 100;
}

// ---- 1: A2 candidate-grid completeness on the 3-blockage fixture ---------------------------------

// Corridor [100,400] (bounded by B1.right()=100, B2.x=400): valid x-range for a w=50 member is
// [100,350]; clamp(M's median=600 [M is bundle-less, so wireMedianTarget returns its OWN unchanged
// center 625, mx=625-25=600], [100,350]) = 350. Corridor [500,800]: valid range [500,750];
// clamp(600,[500,750])=600 (M's own current position, force-included regardless). Corridor
// [900,1000]: valid range [900,950]; clamp(600,[900,950])=900. Every one of those three
// representative points, PLUS every corridor's own two endpoints, must be in the returned set --
// and since B1 also contributes an ILLEGAL breakpoint (x=0, which overlaps B1 outright), the
// returned set is EXACTLY {100,350,500,600,750,900,950} (all at y=0, since h_i==H leaves no y
// freedom) -- a completeness AND exactness check in one.
void testCandidateGridCompleteness() {
    Problem p;
    Placement pl;
    buildThreeBlockageFixture(p, pl);
    std::vector<int> group = {3};  // just M
    std::vector<std::pair<double, double>> cand = lnsCandidatePositions(p, pl, group, 3);

    std::vector<double> xs;
    for (const auto& c : cand) {
        assert(std::abs(c.second - 0.0) < kTol);  // h_i==H: y is always 0
        xs.push_back(c.first);
    }
    std::sort(xs.begin(), xs.end());
    std::vector<double> expected = {100, 350, 500, 600, 750, 900, 950};
    assert(xs.size() == expected.size());
    for (std::size_t i = 0; i < expected.size(); ++i) assert(std::abs(xs[i] - expected[i]) < kTol);
    std::cout << "testCandidateGridCompleteness ok (n=" << xs.size() << ")\n";
}

// ---- 2-4: structural pre-gates --------------------------------------------------------------------

void testEmptyWhenUnstamped() {
    Problem p;
    Placement pl;
    buildTwoBlockGroupFixture(p, pl);
    pl.outline_w = 0;
    pl.outline_h = 0;
    assert(lnsPolishIterates(p, pl, {2, 3}).empty());
    std::cout << "testEmptyWhenUnstamped ok\n";
}

void testEmptyWhenIllegal() {
    Problem p;
    Placement pl;
    buildTwoBlockGroupFixture(p, pl);
    pl.rects[2] = Rect{0, 0, 50, 100};  // M1 now overlaps L
    assert(lnsPolishIterates(p, pl, {2, 3}).empty());
    std::cout << "testEmptyWhenIllegal ok\n";
}

void testEmptyWhenMovableUnderTwo() {
    // lnsSelectGroup: a Problem with a single movable, bundle-connected block never yields a group.
    Problem p;
    p.blocks = {makeBlock("L", 50, 100, BlockType::Edge), makeBlock("M", 50, 100)};
    p.bundles = {Bundle{0, 1, 1}};
    RoutingResult routing;
    assert(lnsSelectGroup(p, routing, 0).empty());

    // lnsPolishIterates: an explicit sub-2 group is rejected regardless of the placement's own
    // legality.
    Problem p2;
    Placement pl2;
    buildTwoBlockGroupFixture(p2, pl2);
    assert(lnsPolishIterates(p2, pl2, {}).empty());
    assert(lnsPolishIterates(p2, pl2, {2}).empty());
    std::cout << "testEmptyWhenMovableUnderTwo ok\n";
}

// ---- 5: determinism --------------------------------------------------------------------------------

void testDeterministic() {
    Problem p1, p2;
    Placement pl1, pl2;
    buildTwoBlockGroupFixture(p1, pl1);
    buildTwoBlockGroupFixture(p2, pl2);
    std::vector<Placement> a = lnsPolishIterates(p1, pl1, {2, 3});
    std::vector<Placement> b = lnsPolishIterates(p2, pl2, {2, 3});
    assert(!a.empty());
    assert(a.size() == b.size());
    for (std::size_t i = 0; i < a.size(); ++i) assert(placementsClose(a[i], b[i]));
    std::cout << "testDeterministic ok (n=" << a.size() << ")\n";
}

// ---- 6: every returned iterate is legal -------------------------------------------------------------

void testEveryIterateLegal() {
    Problem p;
    Placement pl;
    buildTwoBlockGroupFixture(p, pl);
    std::vector<Placement> iterates = lnsPolishIterates(p, pl, {2, 3});
    assert(!iterates.empty());
    for (const auto& it : iterates) assert(legalWithin(it, it.outline_w, it.outline_h));
    std::cout << "testEveryIterateLegal ok\n";
}

// ---- 7: edge rects never move, outline stamp never changes ------------------------------------------

void testEdgeRectsAndStampInvariant() {
    Problem p;
    Placement pl;
    buildTwoBlockGroupFixture(p, pl);
    std::vector<Placement> iterates = lnsPolishIterates(p, pl, {2, 3});
    assert(!iterates.empty());
    for (const auto& it : iterates) {
        assert(it.rects[0].x == pl.rects[0].x && it.rects[0].y == pl.rects[0].y);
        assert(it.rects[0].w == pl.rects[0].w && it.rects[0].h == pl.rects[0].h);
        assert(it.rects[1].x == pl.rects[1].x && it.rects[1].y == pl.rects[1].y);
        assert(it.rects[1].w == pl.rects[1].w && it.rects[1].h == pl.rects[1].h);
        assert(it.outline_w == pl.outline_w);
        assert(it.outline_h == pl.outline_h);
    }
    std::cout << "testEdgeRectsAndStampInvariant ok\n";
}

// ---- 8: known-analytic 2-member group flush-close ----------------------------------------------------

void testKnownAnalyticFlushClose() {
    Problem p;
    Placement pl;
    buildTwoBlockGroupFixture(p, pl);
    std::vector<Placement> iterates = lnsPolishIterates(p, pl, {2, 3});
    assert(!iterates.empty());
    const Placement& best = iterates.back();
    assert(std::abs(best.rects[2].x - 101.0) < kTol);  // M1 flush against L
    assert(std::abs(best.rects[2].y - 0.0) < kTol);
    assert(std::abs(best.rects[3].x - 847.0) < kTol);  // M2 flush against R
    assert(std::abs(best.rects[3].y - 0.0) < kTol);
    assert(std::abs(groupGapObjective(p, best, {2, 3}) - 0.0) < kTol);  // global primary minimum
    std::cout << "testKnownAnalyticFlushClose ok (M1.x=" << best.rects[2].x << " M2.x=" << best.rects[3].x
              << ")\n";
}

// ---- 9: incumbent never worse than status quo under the proxy ---------------------------------------

void testIncumbentNeverWorseThanStatusQuo() {
    Problem p;
    Placement pl;
    buildTwoBlockGroupFixture(p, pl);
    double status_quo_obj = groupGapObjective(p, pl, {2, 3});
    std::vector<Placement> iterates = lnsPolishIterates(p, pl, {2, 3});
    assert(!iterates.empty());
    for (const auto& it : iterates) assert(groupGapObjective(p, it, {2, 3}) <= status_quo_obj + kTol);
    // This fixture's flush-close is a genuine (strict) improvement, not just a tie -- confirm the
    // property is actually exercised.
    assert(groupGapObjective(p, iterates.back(), {2, 3}) < status_quo_obj - kTol);
    std::cout << "testIncumbentNeverWorseThanStatusQuo ok (status_quo=" << status_quo_obj
              << " best=" << groupGapObjective(p, iterates.back(), {2, 3}) << ")\n";
}

// ---- 10: integer input geometry produces integer output geometry (candidate grid) -------------------

// M1's own raw candidate grid (buildGroupBundleIntegerFixture) includes the fractional-before-round
// median breakpoint (mx=700.5 -> llround=701, a genuine round-half case that survives legality since
// its only bundle partner is excluded from the fixed-obstacle set) -- every returned x/y, from both
// the raw candidate grid and the full B&B pipeline's output, must still be an exact integer.
void testIntegerInIntegerOut() {
    Problem p;
    Placement pl;
    buildGroupBundleIntegerFixture(p, pl);
    std::vector<int> group = {0, 1};
    std::vector<std::pair<double, double>> cand = lnsCandidatePositions(p, pl, group, 0);
    assert(!cand.empty());
    bool saw_701 = false;
    for (const auto& c : cand) {
        assert(std::abs(c.first - std::round(c.first)) < kTol);
        assert(std::abs(c.second - std::round(c.second)) < kTol);
        if (std::abs(c.first - 701.0) < kTol) saw_701 = true;
    }
    assert(saw_701);  // confirms the fractional median (700.5) actually rounded, not just skipped

    std::vector<Placement> iterates = lnsPolishIterates(p, pl, group);
    assert(!iterates.empty());
    for (const auto& it : iterates) {
        for (const auto& r : it.rects) {
            assert(std::abs(r.x - std::round(r.x)) < kTol);
            assert(std::abs(r.y - std::round(r.y)) < kTol);
        }
    }
    std::cout << "testIntegerInIntegerOut ok\n";
}

// ---- 11: node-cap anytime legality -------------------------------------------------------------------

void testNodeCapAnytimeLegality() {
    Problem p;
    Placement pl;
    buildTwoBlockGroupFixture(p, pl);
    LnsPolishStats stats;
    std::vector<Placement> iterates = lnsPolishIterates(p, pl, {2, 3}, &stats, /*node_budget=*/3);
    assert(stats.nodes_expanded <= 4);  // budget + 1 (the trial that trips the guard is still counted)
    for (const auto& it : iterates) assert(legalWithin(it, it.outline_w, it.outline_h));
    std::cout << "testNodeCapAnytimeLegality ok (nodes=" << stats.nodes_expanded << " n=" << iterates.size()
              << ")\n";
}

}  // namespace

int main() {
    testCandidateGridCompleteness();
    testEmptyWhenUnstamped();
    testEmptyWhenIllegal();
    testEmptyWhenMovableUnderTwo();
    testDeterministic();
    testEveryIterateLegal();
    testEdgeRectsAndStampInvariant();
    testKnownAnalyticFlushClose();
    testIncumbentNeverWorseThanStatusQuo();
    testIntegerInIntegerOut();
    testNodeCapAnytimeLegality();
    std::cout << "lns (C3) regressions passed\n";
    return 0;
}
