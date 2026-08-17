#include "wl_redistribute.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

#include "placement_engine.hpp"

namespace efgr {
namespace {

// C1a design §1: cross-axis overlap test -- true iff `a`/`b`'s CROSS-axis ranges (y for the H-pass,
// x for the V-pass) overlap by more than kEps. Mirrors respace.cpp's relaxAxis `crossOverlap`
// (respace.cpp:206-215) and search.cpp's compactTowardOrigin `axisOverlap` (search.cpp:16-18,41,51)
// -- same predicate, reproduced locally per the file-plan's "NEW module" scope (this file must link
// standalone with only placement_engine.cpp, per test/run_c1_regressions.sh).
bool crossOverlapOn(bool horizontal, const Rect& a, const Rect& b) {
    if (horizontal) return std::min(a.top(), b.top()) - std::max(a.y, b.y) > kEps;
    return std::min(a.right(), b.right()) - std::max(a.x, b.x) > kEps;
}

// C1a design §1 step (a): one movable block's feasible window along `horizontal`'s axis, against
// EVERY other rect (edge blocks included, design §3) that cross-overlaps it. `legal=false` signals
// the belt-and-braces detection of an already-overlapping pair (a rect that cross-overlaps `i` but
// is neither fully left nor fully right of it -- exactly `overlaps()`, model.hpp:282-285) -- the
// caller treats this as "something is already broken" and aborts the whole redistribution attempt,
// per wlRedistributeIterates' own "base illegal" empty-return contract (this should never actually
// fire given the driver's own upfront legality gate; it exists as defensive proof, not a normal
// control-flow path). A window with hi < lo (computed but geometrically infeasible -- two
// obstacles leave no room) is NOT `legal=false`: the caller skips moving that one block and leaves
// every other block's turn in this sweep unaffected (design §1 "If hi < lo-kEps: skip block").
struct AxisWindow {
    double lo = 0.0;
    double hi = 0.0;
    bool legal = true;
};

AxisWindow axisWindow(const Placement& placement, int i, bool horizontal, double outline_extent) {
    AxisWindow win;
    const Rect& ri = placement.rects[static_cast<std::size_t>(i)];
    double size_i = horizontal ? ri.w : ri.h;
    win.hi = outline_extent - size_i;
    for (std::size_t j = 0; j < placement.rects.size(); ++j) {
        if (static_cast<int>(j) == i) continue;
        const Rect& rj = placement.rects[j];
        if (!crossOverlapOn(horizontal, ri, rj)) continue;
        if (horizontal) {
            if (rj.right() <= ri.x + kEps) {
                win.lo = std::max(win.lo, rj.right());
            } else if (rj.x >= ri.right() - kEps) {
                win.hi = std::min(win.hi, rj.x - size_i);
            } else {
                win.legal = false;
                return win;
            }
        } else {
            if (rj.top() <= ri.y + kEps) {
                win.lo = std::max(win.lo, rj.top());
            } else if (rj.y >= ri.top() - kEps) {
                win.hi = std::min(win.hi, rj.y - size_i);
            } else {
                win.legal = false;
                return win;
            }
        }
    }
    return win;
}

// C1a design §1 "Key simplification": minimizes Sum_p weight_p * max(0, v_p - x, x - v_p) style
// deadzone terms via the weighted-median-interval of a breakpoint multiset -- here each bundle
// partner contributes TWO breakpoints (L_p, weight n_p) and (R_p, weight n_p), and the returned
// interval is exactly the minimizer SET of Sum_p n_p*max(0, L_p-x, x-R_p) (see the design doc's own
// derivation: starting slope -N, incrementing by weight at every breakpoint crossing, N =
// Sum of n_p -- so the flat/zero-slope zone is the classic weighted-median-interval of this combined
// multiset with target = totalWeight/2 = N). Empty `points` (block touches no bundle) returns
// (-infinity, +infinity) -- no constraint at all, matching that the primary objective is uniformly
// zero for such a block regardless of position. Deterministic; no RNG.
std::pair<double, double> weightedMedianInterval(std::vector<std::pair<double, int>> points) {
    if (points.empty()) {
        return {-std::numeric_limits<double>::infinity(), std::numeric_limits<double>::infinity()};
    }
    std::sort(points.begin(), points.end(),
              [](const std::pair<double, int>& a, const std::pair<double, int>& b) { return a.first < b.first; });
    double total = 0.0;
    for (const auto& p : points) total += p.second;
    double half = total / 2.0;

    double a_lo = points.front().first;
    double cum = 0.0;
    for (const auto& p : points) {
        cum += p.second;
        if (cum >= half - kEps) {
            a_lo = p.first;
            break;
        }
    }
    double a_hi = points.back().first;
    cum = total;
    for (auto it = points.rbegin(); it != points.rend(); ++it) {
        cum -= it->second;
        if (cum <= half + kEps) {
            a_hi = it->first;
            break;
        }
    }
    return {a_lo, a_hi};
}

// C1a design §1 step (b)-(d): the target coordinate for one movable block `i` on `horizontal`'s
// axis -- the primary weighted-median-interval breakpoints come from every bundle touching `i`
// (partner's CURRENT facing edges, design's L_p = x_p - w_i / R_p = x_p + w_p); the secondary
// tie-break is `wireMedianTarget`'s own weighted-median partner-CENTER target (design §1 "not a
// hack: the routed chain's jog still depends on overlap alignment"), converted from a center target
// to a left/bottom-edge target by subtracting half of `i`'s own size. `m` is clamped into
// [a_lo,a_hi] first (the primary's exact minimizer interval), THEN rounded, THEN clamped into the
// block's own feasible window [win.lo,win.hi] -- design's integrality note: since a_lo/a_hi are
// themselves exact breakpoint values (integers when the input geometry is integer, never an
// interpolated average), round() of a value already inside an integer-bounded interval cannot land
// outside it.
double axisTarget(const Problem& problem, const Placement& placement, int i, bool horizontal, double size_i,
                  const AxisWindow& win) {
    std::vector<std::pair<double, int>> breakpoints;
    for (const auto& b : problem.bundles) {
        int partner = -1;
        if (b.source == i) {
            partner = b.target;
        } else if (b.target == i) {
            partner = b.source;
        } else {
            continue;
        }
        const Rect& pr = placement.rects[static_cast<std::size_t>(partner)];
        double p_pos = horizontal ? pr.x : pr.y;
        double p_size = horizontal ? pr.w : pr.h;
        breakpoints.emplace_back(p_pos - size_i, b.nets);
        breakpoints.emplace_back(p_pos + p_size, b.nets);
    }
    auto interval = weightedMedianInterval(std::move(breakpoints));

    auto median = wireMedianTarget(problem, placement, i);
    double m = (horizontal ? median.first : median.second) - size_i / 2.0;

    double clamped = std::min(interval.second, std::max(interval.first, m));
    double rounded = std::round(clamped);
    return std::min(win.hi, std::max(win.lo, rounded));
}

// C1a design §1 "Iteration": one (H or V) sweep pass -- order = movable blocks by CURRENT position
// on this axis ascending (index tie-break), reversed on even `k` (alternating direction damps the
// group-move stall mode, design §1 preamble). Gauss-Seidel: each block's new coordinate is applied
// immediately, so later blocks in this same pass's order see earlier blocks' fresh positions.
// `legal=false` propagates axisWindow's belt-and-braces overlap detection up to the driver.
struct SweepOutcome {
    int moved_count = 0;
    bool legal = true;
};

SweepOutcome sweepAxis(const Problem& problem, Placement& placement, bool horizontal, int k) {
    SweepOutcome outcome;
    std::vector<int> order;
    for (std::size_t i = 0; i < problem.blocks.size(); ++i) {
        if (!isAnchoredEdge(problem, i)) order.push_back(static_cast<int>(i));
    }
    std::sort(order.begin(), order.end(), [&](int a, int b) {
        double pa = horizontal ? placement.rects[static_cast<std::size_t>(a)].x : placement.rects[static_cast<std::size_t>(a)].y;
        double pb = horizontal ? placement.rects[static_cast<std::size_t>(b)].x : placement.rects[static_cast<std::size_t>(b)].y;
        if (std::abs(pa - pb) > kEps) return pa < pb;
        return a < b;
    });
    if (k % 2 == 0) std::reverse(order.begin(), order.end());

    double outline_extent = horizontal ? placement.outline_w : placement.outline_h;

    for (int i : order) {
        AxisWindow win = axisWindow(placement, i, horizontal, outline_extent);
        if (!win.legal) {
            outcome.legal = false;
            return outcome;
        }
        if (win.hi < win.lo - kEps) continue;  // infeasible window -- skip this block only

        Rect& ri = placement.rects[static_cast<std::size_t>(i)];
        double size_i = horizontal ? ri.w : ri.h;
        double old_pos = horizontal ? ri.x : ri.y;
        double new_pos = axisTarget(problem, placement, i, horizontal, size_i, win);
        if (std::abs(new_pos - old_pos) > kEps) {
            if (horizontal) {
                ri.x = new_pos;
            } else {
                ri.y = new_pos;
            }
            ++outcome.moved_count;
        }
    }
    return outcome;
}

// C1a design §7 failure-mode-2 belt-and-braces gate: mirrors respace.cpp's `rectsLegalWithinOutline`
// (respace.cpp:373-382) geometric semantics (containment in [0,w]x[0,h] + pairwise non-overlap over
// ALL rects, edge or core) -- reproduced locally rather than exposed across files, same rationale
// respace.cpp's own doc comment gives for not widening placement_engine.cpp's public surface.
bool placementLegalWithin(const Placement& placement, double w, double h) {
    for (std::size_t i = 0; i < placement.rects.size(); ++i) {
        const Rect& r = placement.rects[i];
        if (r.x < -kEps || r.y < -kEps || r.right() > w + kEps || r.top() > h + kEps) return false;
        for (std::size_t j = i + 1; j < placement.rects.size(); ++j) {
            if (overlaps(r, placement.rects[j])) return false;
        }
    }
    return true;
}

}  // namespace

std::vector<Placement> wlRedistributeIterates(const Problem& problem, const Placement& base, int max_sweeps,
                                              WlRedistributeStats* stats) {
    WlRedistributeStats local_stats;
    if (stats == nullptr) stats = &local_stats;
    *stats = WlRedistributeStats{};

    if (base.outline_w <= kEps || base.outline_h <= kEps) return {};  // stamp unset
    if (problem.bundles.empty()) return {};  // nothing to align toward

    bool has_movable = false;
    for (std::size_t i = 0; i < problem.blocks.size(); ++i) {
        if (!isAnchoredEdge(problem, i)) {
            has_movable = true;
            break;
        }
    }
    if (!has_movable) return {};

    if (!placementLegalWithin(base, base.outline_w, base.outline_h)) return {};  // base illegal

    Placement current = base;
    Placement sweep1_result;
    Placement sweep2_result;
    bool have_sweep2 = false;
    int last_k = 0;

    for (int k = 1; k <= max_sweeps; ++k) {
        SweepOutcome h = sweepAxis(problem, current, /*horizontal=*/true, k);
        if (!h.legal) return {};
        // V-pass reads the POST-H-pass x-geometry (respace.cpp:316-323 rationale, design §1's own
        // "reads POST-H geometry" note) -- do not reorder or read a stale snapshot.
        SweepOutcome v = sweepAxis(problem, current, /*horizontal=*/false, k);
        if (!v.legal) return {};

        int moved = h.moved_count + v.moved_count;
        stats->sweeps_run = k;
        stats->blocks_moved += moved;
        last_k = k;

        if (k == 1) {
            if (moved == 0) return {};  // pre-gate: sweep 1 moved nothing, zero evals
            sweep1_result = current;
        } else if (k == 2) {
            sweep2_result = current;
            have_sweep2 = true;
        }

        if (moved == 0) break;  // fixpoint: further sweeps cannot do anything new
    }

    std::vector<Placement> iterates;
    iterates.push_back(sweep1_result);
    if (have_sweep2) iterates.push_back(sweep2_result);
    // Only append `current` as a distinct "final" rung when it is a genuinely later sweep than 1/2
    // -- when the loop stopped AT sweep 1 (max_sweeps==1) or AT sweep 2, `current` is byte-identical
    // to the entry already pushed above, and the caller's own placementKey dedup (design §2) would
    // just discard the duplicate anyway; skipping it here keeps this enumerator's own output free of
    // trivial dupes without depending on search.cpp's placementKey.
    if (last_k != 1 && last_k != 2) iterates.push_back(current);
    return iterates;
}

}  // namespace efgr
