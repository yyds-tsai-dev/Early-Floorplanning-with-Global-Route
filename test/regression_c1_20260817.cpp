// C1a (fixed-topology WL-driven whitespace redistribution, .superpowers/sdd/c1-design-20260817.md):
// regression coverage for wlRedistributeIterates' pure sweep enumerator -- the four structural
// pre-gates (empty-when-no-bundles/unstamped/overlapping-input), determinism, per-iterate legality,
// edge-rect/outline invariance, the primary objective's monotone non-increase across the returned
// rung sequence, a hand-derived analytic 3-block gap-close, an obstacle-flush stop, integer-in/
// integer-out, and the "don't duplicate an already-captured fixpoint rung" dedupe rule. Links ONLY
// src/wl_redistribute.cpp + src/placement_engine.cpp (see test/run_c1_regressions.sh) -- no
// search.cpp, no routing, no exactCached: this suite exercises the enumerator in isolation, exactly
// as design §8's file plan scopes it.
#include <cassert>
#include <cmath>
#include <iostream>
#include <vector>

#include "../src/model.hpp"
#include "../src/wl_redistribute.hpp"

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

// Mirrors wl_redistribute.cpp's own file-local `placementLegalWithin` (respace.cpp:373-382
// semantics: containment in [0,w]x[0,h] + pairwise non-overlap over ALL rects) -- reproduced here
// since that helper is not exposed via wl_redistribute.hpp (this suite only sees the public
// enumerator, same as any other caller would).
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

// Design §1's primary objective, Sum_bundles nets*gapx(src,tgt) -- the same L1b boundary-gap
// formula eval.cpp's cheapScore uses under cheap_proxy_v2 (eval.cpp:314-316): zero when the pair
// already abuts, growing linearly with facing-edge separation otherwise.
double totalGapObjective(const Problem& p, const Placement& pl) {
    double total = 0.0;
    for (const auto& b : p.bundles) {
        const Rect& a = pl.rects[static_cast<std::size_t>(b.source)];
        const Rect& c = pl.rects[static_cast<std::size_t>(b.target)];
        double dx = std::max(0.0, std::max(a.x, c.x) - std::min(a.right(), c.right()));
        double dy = std::max(0.0, std::max(a.y, c.y) - std::min(a.top(), c.top()));
        total += (dx + dy) * b.nets;
    }
    return total;
}

// ---- shared fixtures ----------------------------------------------------------------------------

// L(edge, frozen) -- M(macro, movable) -- R(edge, frozen), all banded at y=[0,100]. M bundles
// equally (nets=1 each) to both L and R. Hand-derivable: the primary's weighted-median-interval for
// M's x is exactly [L.right(), R.x - M.w] = [50,250] here (a coincidence of this fixture's
// symmetric spacing, verified by direct computation of the deadzone breakpoints), and M's own
// window happens to match that same interval, so the secondary (wireMedianTarget-derived) tie-break
// alone decides where in that flat zone M lands: partner-center median = (25+325)/2 = midway, whose
// left-edge-converted target clamps UP to the window/interval's shared lower bound 50 -- M ends up
// exactly abutting L. See testKnownAnalyticThreeBlockGapClose's own comment for the full arithmetic.
void buildThreeBlockFixture(Problem& p, Placement& pl) {
    p.blocks = {makeBlock("L", 50, 100, BlockType::Edge), makeBlock("M", 50, 100, BlockType::Macro),
               makeBlock("R", 50, 100, BlockType::Edge)};
    p.bundles = {Bundle{0, 1, 1}, Bundle{1, 2, 1}};
    pl.rects = {Rect{0, 0, 50, 100}, Rect{100, 0, 50, 100}, Rect{300, 0, 50, 100}};
    pl.aspect = {1.0, 1.0, 1.0};
    pl.outline_w = 500;
    pl.outline_h = 100;
}

// P(edge, frozen, far left) -- O(edge, frozen obstacle at x=[20,70], NOT a bundle partner) --
// M(macro, movable, starts at x=200) bundled ONLY to P (nets=1). M's wire-median pull wants it as
// far left as possible (toward P), but O physically blocks the path -- M's feasible window's own
// lower bound stops at O.right()=70, so M ends up flush against O, never overlapping it and never
// reaching all the way to P.
void buildObstacleFixture(Problem& p, Placement& pl) {
    p.blocks = {makeBlock("P", 10, 100, BlockType::Edge), makeBlock("O", 50, 100, BlockType::Edge),
               makeBlock("M", 50, 100, BlockType::Macro)};
    p.bundles = {Bundle{0, 2, 1}};
    pl.rects = {Rect{0, 0, 10, 100}, Rect{20, 0, 50, 100}, Rect{200, 0, 50, 100}};
    pl.aspect = {1.0, 1.0, 1.0};
    pl.outline_w = 1000;
    pl.outline_h = 100;
}

// P(edge, ODD width 101, banded at y=[200,300] -- deliberately OUTSIDE M's own y-band so it never
// physically obstructs M's window, isolating the round() step from any window-clamp) -- M(macro,
// movable, y=[0,100]) bundled to P. P's odd width makes wireMedianTarget's partner-center land on a
// half-integer (cx=50.5), so the un-rounded secondary target is itself fractional -- exercising the
// "round() of a genuinely non-integer intermediate stays an exact integer" property on real,
// non-degenerate arithmetic (not just a value already pinned to an integer window/interval bound).
void buildIntegerFixture(Problem& p, Placement& pl) {
    p.blocks = {makeBlock("P", 101, 100, BlockType::Edge), makeBlock("M", 50, 100, BlockType::Macro)};
    p.bundles = {Bundle{0, 1, 1}};
    pl.rects = {Rect{0, 200, 101, 100}, Rect{300, 0, 50, 100}};
    pl.aspect = {1.0, 1.0};
    pl.outline_w = 1000;
    pl.outline_h = 300;
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

// ---- 1-3: structural pre-gates ------------------------------------------------------------------

void testEmptyWhenNoBundles() {
    Problem p;
    p.blocks = {makeBlock("A", 100, 100), makeBlock("B", 100, 100)};
    Placement pl;
    pl.rects = {Rect{0, 0, 100, 100}, Rect{200, 0, 100, 100}};
    pl.aspect = {1.0, 1.0};
    pl.outline_w = 500;
    pl.outline_h = 100;
    assert(wlRedistributeIterates(p, pl).empty());
    std::cout << "testEmptyWhenNoBundles ok\n";
}

void testEmptyWhenUnstamped() {
    Problem p;
    p.blocks = {makeBlock("A", 100, 100), makeBlock("B", 100, 100)};
    p.bundles = {Bundle{0, 1, 1}};
    Placement pl;
    pl.rects = {Rect{0, 0, 100, 100}, Rect{200, 0, 100, 100}};
    pl.aspect = {1.0, 1.0};
    pl.outline_w = 0;  // unstamped
    pl.outline_h = 0;
    assert(wlRedistributeIterates(p, pl).empty());
    std::cout << "testEmptyWhenUnstamped ok\n";
}

void testEmptyWhenOverlappingInput() {
    Problem p;
    p.blocks = {makeBlock("A", 100, 100), makeBlock("B", 100, 100)};
    p.bundles = {Bundle{0, 1, 1}};
    Placement pl;
    pl.rects = {Rect{0, 0, 100, 100}, Rect{50, 50, 100, 100}};  // overlapping
    pl.aspect = {1.0, 1.0};
    pl.outline_w = 300;
    pl.outline_h = 300;
    assert(wlRedistributeIterates(p, pl).empty());
    std::cout << "testEmptyWhenOverlappingInput ok\n";
}

// ---- 4: determinism ------------------------------------------------------------------------------

void testDeterministic() {
    Problem p1, p2;
    Placement pl1, pl2;
    buildThreeBlockFixture(p1, pl1);
    buildThreeBlockFixture(p2, pl2);
    std::vector<Placement> a = wlRedistributeIterates(p1, pl1);
    std::vector<Placement> b = wlRedistributeIterates(p2, pl2);
    assert(!a.empty());
    assert(a.size() == b.size());
    for (std::size_t i = 0; i < a.size(); ++i) assert(placementsClose(a[i], b[i]));
    std::cout << "testDeterministic ok (n=" << a.size() << ")\n";
}

// ---- 5: every returned iterate is legal -----------------------------------------------------------

void testEveryIterateLegal() {
    Problem p;
    Placement pl;
    buildThreeBlockFixture(p, pl);
    std::vector<Placement> iterates = wlRedistributeIterates(p, pl);
    assert(!iterates.empty());
    for (const auto& it : iterates) assert(legalWithin(it, it.outline_w, it.outline_h));
    std::cout << "testEveryIterateLegal ok\n";
}

// ---- 6: edge rects never move ----------------------------------------------------------------------

void testEdgeRectsBitIdentical() {
    Problem p;
    Placement pl;
    buildThreeBlockFixture(p, pl);
    std::vector<Placement> iterates = wlRedistributeIterates(p, pl);
    assert(!iterates.empty());
    for (const auto& it : iterates) {
        assert(it.rects[0].x == pl.rects[0].x && it.rects[0].y == pl.rects[0].y);
        assert(it.rects[0].w == pl.rects[0].w && it.rects[0].h == pl.rects[0].h);
        assert(it.rects[2].x == pl.rects[2].x && it.rects[2].y == pl.rects[2].y);
        assert(it.rects[2].w == pl.rects[2].w && it.rects[2].h == pl.rects[2].h);
    }
    std::cout << "testEdgeRectsBitIdentical ok\n";
}

// ---- 7: outline stamp never changes ---------------------------------------------------------------

void testOutlineNeverChanged() {
    Problem p;
    Placement pl;
    buildThreeBlockFixture(p, pl);
    std::vector<Placement> iterates = wlRedistributeIterates(p, pl);
    assert(!iterates.empty());
    for (const auto& it : iterates) {
        assert(it.outline_w == pl.outline_w);
        assert(it.outline_h == pl.outline_h);
    }
    std::cout << "testOutlineNeverChanged ok\n";
}

// ---- 8: primary objective is non-increasing across the returned rung sequence ---------------------

void testObjectiveMonotoneNonIncreasing() {
    Problem p;
    Placement pl;
    buildObstacleFixture(p, pl);
    double base_obj = totalGapObjective(p, pl);
    std::vector<Placement> iterates = wlRedistributeIterates(p, pl);
    assert(!iterates.empty());
    double prev = base_obj;
    for (const auto& it : iterates) {
        double obj = totalGapObjective(p, it);
        assert(obj <= prev + kTol);
        prev = obj;
    }
    // This fixture's obstacle forces a genuine (strict) improvement, not just a tie -- see
    // buildObstacleFixture's own comment; confirm the property is actually exercised, not vacuous.
    assert(totalGapObjective(p, iterates.front()) < base_obj - kTol);
    std::cout << "testObjectiveMonotoneNonIncreasing ok (base=" << base_obj
              << " final=" << totalGapObjective(p, iterates.back()) << ")\n";
}

// ---- 9: known-analytic 3-block gap close -----------------------------------------------------------

// Hand derivation (see buildThreeBlockFixture's comment for the setup): M's primary breakpoints are
// {L_L=-50:1, R_L=50:1} (from partner L) and {L_R=250:1, R_R=350:1} (from partner R) -- the
// weighted-median-interval of that 4-point equal-weight multiset is exactly [50,250] (forward scan
// reaches half=2 at the SECOND point value 50; backward scan reaches half=2 at the THIRD point value
// 250). M's own physical window is ALSO [50,250] (L fully left with L.right()=50, R fully right with
// R.x-M.w=250) -- the same numbers by this fixture's deliberate symmetry. The secondary tie-break
// (wireMedianTarget's partner-center median, (25+325)/2 => median x=25, m=25-25=0) falls OUTSIDE
// both bounds and clamps up to the shared floor 50 -- M ends up exactly abutting L (gapx(M,L)=0).
void testKnownAnalyticThreeBlockGapClose() {
    Problem p;
    Placement pl;
    buildThreeBlockFixture(p, pl);
    std::vector<Placement> iterates = wlRedistributeIterates(p, pl);
    assert(!iterates.empty());
    assert(std::abs(iterates.front().rects[1].x - 50.0) < kTol);
    assert(std::abs(iterates.front().rects[1].y - 0.0) < kTol);
    std::cout << "testKnownAnalyticThreeBlockGapClose ok (M.x=" << iterates.front().rects[1].x << ")\n";
}

// ---- 10: an obstacle stops the pull flush, never overlapping ---------------------------------------

void testObstacleStopsFlush() {
    Problem p;
    Placement pl;
    buildObstacleFixture(p, pl);
    std::vector<Placement> iterates = wlRedistributeIterates(p, pl);
    assert(!iterates.empty());
    const Rect& m = iterates.front().rects[2];
    const Rect& o = pl.rects[1];
    assert(std::abs(m.x - o.right()) < kTol);  // exactly flush against O's right edge
    assert(!overlaps(m, o));
    std::cout << "testObstacleStopsFlush ok (M.x=" << m.x << ")\n";
}

// ---- 11: integer input geometry produces integer output geometry -----------------------------------

void testIntegerInIntegerOut() {
    Problem p;
    Placement pl;
    buildIntegerFixture(p, pl);
    std::vector<Placement> iterates = wlRedistributeIterates(p, pl);
    assert(!iterates.empty());
    for (const auto& it : iterates) {
        for (const auto& r : it.rects) {
            assert(std::abs(r.x - std::round(r.x)) < kTol);
            assert(std::abs(r.y - std::round(r.y)) < kTol);
        }
    }
    std::cout << "testIntegerInIntegerOut ok\n";
}

// ---- 12: fixpoint dedupe -- a rung identical to sweep1/sweep2 is not re-appended as "final" --------

// buildThreeBlockFixture converges at sweep 2 (sweep 2 moves nothing -- M is already at its fixpoint
// after sweep 1, see testKnownAnalyticThreeBlockGapClose's derivation): the driver must return
// EXACTLY [sweep1, sweep2] (size 2), never appending a third "final" entry that would just duplicate
// sweep2's own content (design §2's "deduped by placementKey" caller-side dedup is a DIFFERENT,
// content-based mechanism -- this is the enumerator's own index-based avoidance of the trivial case
// where "final" literally IS sweep1 or sweep2).
void testFixpointDedupe() {
    Problem p;
    Placement pl;
    buildThreeBlockFixture(p, pl);
    std::vector<Placement> iterates = wlRedistributeIterates(p, pl);
    assert(iterates.size() == 2);
    assert(placementsClose(iterates[0], iterates[1]));
    std::cout << "testFixpointDedupe ok (n=" << iterates.size() << ")\n";
}

}  // namespace

int main() {
    testEmptyWhenNoBundles();
    testEmptyWhenUnstamped();
    testEmptyWhenOverlappingInput();
    testDeterministic();
    testEveryIterateLegal();
    testEdgeRectsBitIdentical();
    testOutlineNeverChanged();
    testObjectiveMonotoneNonIncreasing();
    testKnownAnalyticThreeBlockGapClose();
    testObstacleStopsFlush();
    testIntegerInIntegerOut();
    testFixpointDedupe();
    std::cout << "c1 (C1a) regressions passed\n";
    return 0;
}
