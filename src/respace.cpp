#include "respace.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <utility>

#include "placement_engine.hpp"

namespace efgr {
namespace {

// Same open_routes-strictly-first-then-lower-value preference as search.cpp's file-local
// outlineSweepBetter (search.cpp:116-119). Replicated here rather than moved/exposed across
// files, per the task-4 brief: respaceFixedPoint lives in respace.cpp, and outlineSweepBetter is
// intentionally file-local to search.cpp.
bool respaceCandidateBetter(const RoutingResult& a, double a_value, const RoutingResult& b, double b_value) {
    if ((a.open_routes == 0) != (b.open_routes == 0)) return a.open_routes == 0;
    return a_value < b_value;
}

// True iff every rect's coordinates (x,y,w,h) match within kEps -- used to detect "respaceToDemand
// made no coordinate change" (design §2.6(3) early exit: further iterations cannot do anything new
// once a pass is a geometric no-op).
bool rectsUnchanged(const Placement& a, const Placement& b) {
    if (a.rects.size() != b.rects.size()) return false;
    for (std::size_t i = 0; i < a.rects.size(); ++i) {
        const Rect& ra = a.rects[i];
        const Rect& rb = b.rects[i];
        if (std::abs(ra.x - rb.x) > kEps || std::abs(ra.y - rb.y) > kEps || std::abs(ra.w - rb.w) > kEps ||
            std::abs(ra.h - rb.h) > kEps) {
            return false;
        }
    }
    return true;
}

bool positiveOverlap(double a0, double a1, double b0, double b1) {
    return std::min(a1, b1) - std::max(a0, b0) > kEps;
}

double overlapDepth(double a0, double a1, double b0, double b1) {
    return std::min(a1, b1) - std::max(a0, b0);
}

}  // namespace

ChannelBounds channelBounds(const Problem& problem, const Placement& placement, const Rect& channel_rect) {
    ChannelBounds bounds;
    double x0 = channel_rect.x;
    double x1 = channel_rect.right();
    double y0 = channel_rect.y;
    double y1 = channel_rect.top();

    // Strip-membership filter (T1 review carry-over): a candidate bounding block must actually
    // span the channel's cross-range, not merely clip a corner of it via a barely-positive
    // overlap. Among candidates sharing the same edge coincidence (staggered blocks at the same
    // x/y edge but different cross-extents), pick the one with the DEEPEST cross-range overlap
    // (the block that actually flanks the channel body), breaking ties by smallest index for
    // determinism. A block whose overlap is only a sliver at the channel's corner still passes
    // `positiveOverlap` but must lose to a block that truly abuts/spans the channel.
    double best_left_depth = -1.0;
    double best_right_depth = -1.0;
    double best_below_depth = -1.0;
    double best_above_depth = -1.0;

    for (std::size_t i = 0; i < problem.blocks.size(); ++i) {
        const Rect& r = placement.rects[i];
        // Left neighbor: block's right edge coincides with the channel's left edge, y-overlapping.
        if (std::abs(r.right() - x0) < kEps && positiveOverlap(r.y, r.top(), y0, y1)) {
            double depth = overlapDepth(r.y, r.top(), y0, y1);
            if (bounds.left == -1 || depth > best_left_depth + kEps) {
                bounds.left = static_cast<int>(i);
                best_left_depth = depth;
            }
        }
        // Right neighbor: block's left edge coincides with the channel's right edge, y-overlapping.
        if (std::abs(r.x - x1) < kEps && positiveOverlap(r.y, r.top(), y0, y1)) {
            double depth = overlapDepth(r.y, r.top(), y0, y1);
            if (bounds.right == -1 || depth > best_right_depth + kEps) {
                bounds.right = static_cast<int>(i);
                best_right_depth = depth;
            }
        }
        // Below neighbor: block's top edge coincides with the channel's bottom edge, x-overlapping.
        if (std::abs(r.top() - y0) < kEps && positiveOverlap(r.x, r.right(), x0, x1)) {
            double depth = overlapDepth(r.x, r.right(), x0, x1);
            if (bounds.below == -1 || depth > best_below_depth + kEps) {
                bounds.below = static_cast<int>(i);
                best_below_depth = depth;
            }
        }
        // Above neighbor: block's bottom edge coincides with the channel's top edge, x-overlapping.
        if (std::abs(r.y - y1) < kEps && positiveOverlap(r.x, r.right(), x0, x1)) {
            double depth = overlapDepth(r.x, r.right(), x0, x1);
            if (bounds.above == -1 || depth > best_above_depth + kEps) {
                bounds.above = static_cast<int>(i);
                best_above_depth = depth;
            }
        }
    }
    return bounds;
}

namespace {

// One node of the per-axis constraint graph: either a core block (index into problem.blocks /
// placement.rects) or the low-side outline wall (block_index == -1). Nodes are processed in
// ascending order of their CURRENT coordinate along the relaxed axis (frozen order, design
// p3-design.md §2.2); this is always a valid topological order because the immediate-predecessor
// adjacency is derived from that same current geometry.
struct AxisNode {
    int block_index = -1;  // -1 == low-side wall (LEFT for H-pass, BOTTOM for V-pass)
    double old_coord = 0.0;
    double size = 0.0;  // block width (H-pass) or height (V-pass); 0 for the wall
};

// Merges a demand list into a lookup keyed by ordered (from,to) node identity, taking the MAX
// across entries sharing a pair (design p3-design.md §1.2 "Multiple channels per strip"; also the
// respace.hpp channelDemands doc-comment carry-over). from/to use the same -1-for-wall convention
// as StripDemand's own fields.
std::map<std::pair<int, int>, double> mergeDemandsMax(const std::vector<StripDemand>& demands, bool horizontal) {
    std::map<std::pair<int, int>, double> merged;
    for (const auto& d : demands) {
        int from = horizontal ? d.left : d.below;
        int to = horizontal ? d.right : d.above;
        double need = horizontal ? d.need_w : d.need_h;
        if (need <= kEps) continue;
        // A demand with an unresolved (-1 on both sides would mean "whole outline", which
        // channelBounds never emits for a real bounded channel) still needs at least one real
        // endpoint to attach an edge to; if both sides are the wall there is nothing to charge a
        // pairwise edge with here (that degenerates to "grow the outline", §3.5, out of scope for
        // this pairwise pass) so skip it defensively.
        if (from == -1 && to == -1) continue;
        auto key = std::make_pair(from, to);
        auto it = merged.find(key);
        if (it == merged.end() || need > it->second) merged[key] = need;
    }
    return merged;
}

// Integer-quantizes one widening contribution before it is folded into a coordinate (beta-task-4
// post-packaging fix: contest output must be all-integer, QA A43). `need_w`/`need_h` originate as
// usage/25.0 (channelDemands) and are then further scaled below by the secant MAX-clamp fallback,
// so neither value alone is guaranteed integral -- this is the one point every widening delta
// passes through on its way into new_coord[b], and hence into placement.rects[i].x/y. Ceil, never
// round/floor: widening MORE only ever adds slack, so it can't turn a satisfied demand into an
// unsatisfied one. The -kEps guard absorbs float noise so an already-integral contribution (e.g.
// scale==1.0 applied to an exact need) is not bumped up an unnecessary extra unit.
double ceilWidening(double x) { return std::ceil(x - kEps); }

// Relaxes one axis (H: x using need_w/width; V: y using need_h/height) via longest-path over
// ALL constraint-graph predecessors (not a single selected one -- p3-task-2 review, 2 Critical
// findings), folding each node's OLD coordinate in as a virtual edge from the wall so the relax
// can only ever raise a coordinate (design p3-design.md §2.2, monotone increase; folding the old
// coordinate in during the same topological pass, rather than clamping post-hoc, is required for
// correctness on non-origin-compacted input and keeps every downstream min-gap edge satisfied).
// Returns the new coordinate per core block (indexed by problem.blocks index; edge blocks and
// unused entries are left at their input value).
//
// Per-block relax (frozen order, current-coord + index tie-break):
//   new_coord[b] = max( coord[b],                                                  // monotone
//                       max over EVERY earlier-in-frozen-order a with crossOverlap(a,b):
//                           new_coord[a] + size[a] + scale*demandOrZero(a,b),
//                       max over EVERY merged demand pair (a,b), a earlier in frozen order,
//                           regardless of crossOverlap(a,b):
//                           new_coord[a] + size[a] + scale*need(a,b) )
// A single selected predecessor is not enough: (Critical 1) a demand keyed (L,R) must still apply
// even when some other block M -- not L -- is R's nearest geometric neighbor (the demand pair
// itself is charged regardless of crossOverlap, the third max-term above); (Critical 2) a block
// that legitimately wins its OWN predecessor edge (and so is pushed forward by a real demand) must
// still be checked against by every OTHER block that geometrically cross-overlaps it, not just the
// one block that happens to win that other block's nearest-neighbor tie-break -- otherwise the
// tie-loser can be left behind and the tie-winner's stale position ends up overlapping the pushed
// block (the second max-term above, over ALL cross-overlapping a, not just the nearest one).
// O(n^2) per axis, which is fine at the contest's n<50.
//
// `scale` multiplies every DEMAND contribution (not the base non-overlap term); passing 1.0 is
// the unclamped relax, values < 1.0 implement the proportional partial-widening fallback when the
// unclamped result would exceed MAX (design failure-mode 1, §2.3/§3.1).
std::vector<double> relaxAxis(const Problem& problem, const Placement& placement, bool horizontal,
                               const std::map<std::pair<int, int>, double>& merged, double scale) {
    std::size_t n = problem.blocks.size();
    std::vector<double> coord(n, 0.0);
    std::vector<double> size(n, 0.0);
    std::vector<int> core;
    for (std::size_t i = 0; i < n; ++i) {
        const Rect& r = placement.rects[i];
        coord[i] = horizontal ? r.x : r.y;
        size[i] = horizontal ? r.w : r.h;
        if (!isEdge(problem.blocks[i].type)) core.push_back(static_cast<int>(i));
    }

    // Frozen topological order: ascending current coordinate, index tie-break (design §2.2/§2.7).
    std::sort(core.begin(), core.end(), [&](int a, int b) {
        if (std::abs(coord[a] - coord[b]) > kEps) return coord[a] < coord[b];
        return a < b;
    });
    // Position of each core block within the frozen order, so "earlier in frozen order" can be
    // tested in O(1) rather than by re-deriving it from coord/index each time.
    std::vector<int> order_rank(n, -1);
    for (std::size_t pos = 0; pos < core.size(); ++pos) order_rank[core[pos]] = static_cast<int>(pos);

    // Cross-range overlap, mirroring compactTowardOrigin's axisOverlap predicate (search.cpp:16-
    // 18,41,51): true iff a and b's CROSS axis ranges (y for the H-pass, x for the V-pass) overlap
    // by more than kEps. No longer used to pick a single nearest predecessor -- every crossing pair
    // earlier-in-frozen-order now contributes its own non-overlap edge (see block comment above).
    auto crossOverlap = [&](int a, int b) {
        const Rect& ra = placement.rects[a];
        const Rect& rb = placement.rects[b];
        if (horizontal) return std::min(ra.top(), rb.top()) - std::max(ra.y, rb.y) > kEps;
        return std::min(ra.right(), rb.right()) - std::max(ra.x, rb.x) > kEps;
    };

    std::vector<double> new_coord(n, 0.0);
    for (std::size_t i = 0; i < n; ++i) new_coord[i] = coord[i];

    for (int b : core) {
        // Monotone virtual edge: a block never moves left of where it already was.
        double best = coord[b];

        // Term 1: every earlier-in-frozen-order block that cross-overlaps b contributes its own
        // non-overlap (+ demand, if this exact pair is charged) edge -- not just the single
        // nearest one. This is what stops a tie-losing candidate's stale position (never
        // re-checked against the block that actually grew) from ending up overlapped (Critical 2).
        for (int a : core) {
            if (order_rank[a] >= order_rank[b]) continue;
            if (!crossOverlap(a, b)) continue;
            double demand = 0.0;
            auto it = merged.find(std::make_pair(a, b));
            if (it != merged.end()) demand = it->second;
            best = std::max(best, new_coord[a] + size[a] + ceilWidening(scale * demand));
        }

        // Term 2: every demand pair keyed EXACTLY (a,b) applies regardless of crossOverlap(a,b) --
        // a demand charged to a specific bounding pair must be honored even when some other block
        // won the immediate-neighbor slot between them (Critical 1). `a == -1` is the low-side
        // wall (size/new_coord both 0 by convention); demand pairs keyed (b, -1) (b -> the high-
        // side wall) are a "grow the outline" constraint, not a per-block coordinate raise, and are
        // deliberately left unread here, same as the prior single-predecessor code (out of scope
        // for this pairwise pass -- design p3-design.md §3.5).
        for (const auto& entry : merged) {
            int a = entry.first.first;
            int to = entry.first.second;
            if (to != b) continue;
            if (a != -1 && (order_rank[a] < 0 || order_rank[a] >= order_rank[b])) continue;
            double a_new = (a == -1) ? 0.0 : new_coord[a];
            double a_size = (a == -1) ? 0.0 : size[a];
            best = std::max(best, a_new + a_size + ceilWidening(scale * entry.second));
        }

        new_coord[b] = best;
    }
    return new_coord;
}

double axisSpan(const Problem& problem, const Placement& placement, bool horizontal, const std::vector<double>& coord) {
    double span = 0.0;
    for (std::size_t i = 0; i < problem.blocks.size(); ++i) {
        if (isEdge(problem.blocks[i].type)) continue;
        double size = horizontal ? placement.rects[i].w : placement.rects[i].h;
        span = std::max(span, coord[i] + size);
    }
    return span;
}

// One axis pass: unclamped relax, then -- if it would exceed MAX -- the secant proportional scale
// (design failure-mode 1, §2.3/§3.1; per task-2 review: the secant is the exact closed-form
// bound for a convex, piecewise-linear, non-decreasing span-vs-scale curve, so it is guaranteed
// never to exceed MAX even though it may leave a little slack when the binding path changes with
// scale). Writes the resulting coordinates back into `placement` for core blocks only.
void relaxOneAxis(const Problem& problem, Placement& placement, bool horizontal, const std::vector<StripDemand>& demands) {
    auto merged = mergeDemandsMax(demands, horizontal);
    if (merged.empty()) return;  // no-op fast path (design §3.5)

    double max_extent = horizontal ? problem.outline_w : problem.outline_h;

    std::vector<double> unclamped = relaxAxis(problem, placement, horizontal, merged, 1.0);
    double span1 = axisSpan(problem, placement, horizontal, unclamped);

    std::vector<double> result = unclamped;
    if (span1 > max_extent + kEps) {
        std::vector<double> base = relaxAxis(problem, placement, horizontal, merged, 0.0);
        double span0 = axisSpan(problem, placement, horizontal, base);
        double scale = 0.0;
        if (span1 - span0 > kEps) {
            scale = (max_extent - span0) / (span1 - span0);
            scale = std::max(0.0, std::min(1.0, scale));
        }
        result = (scale <= kEps) ? base : relaxAxis(problem, placement, horizontal, merged, scale);
        // Defensive hard clamp: the secant is a provable upper bound for a convex span curve, but
        // guard floating-point slop at the boundary rather than trust it silently. Never shrinks
        // a coordinate below what relaxAxis already folded in (coord[i]), so order/gaps hold.
        for (std::size_t i = 0; i < problem.blocks.size(); ++i) {
            if (isEdge(problem.blocks[i].type)) continue;
            double size = horizontal ? placement.rects[i].w : placement.rects[i].h;
            double cap = max_extent - size;
            if (result[i] > cap) result[i] = std::max(cap, horizontal ? placement.rects[i].x : placement.rects[i].y);
        }
    }

    for (std::size_t i = 0; i < problem.blocks.size(); ++i) {
        if (isEdge(problem.blocks[i].type)) continue;
        if (horizontal) {
            placement.rects[i].x = result[i];
        } else {
            placement.rects[i].y = result[i];
        }
    }
}

}  // namespace

bool relaxToDemand(const Problem& problem, Placement& placement, const std::vector<StripDemand>& demands) {
    // H before V is deliberate, not incidental (p3-task-2 review, Important finding): the V-pass's
    // crossOverlap predicate reads x-ranges from `placement`, so running H first means V collides
    // against the POST-H-PASS x-geometry -- i.e. where blocks actually sit after horizontal
    // widening, not their stale pre-relax x. That is the CORRECT semantics: real vertical collision
    // is governed by the blocks' actual (post-move) x-extents, not by geometry that no longer
    // exists once the H-pass has run. Do not reorder these two calls or make them read a shared
    // "original" snapshot instead of `placement`'s live state.
    relaxOneAxis(problem, placement, /*horizontal=*/true, demands);
    relaxOneAxis(problem, placement, /*horizontal=*/false, demands);
    // Always true: per design failure-mode 1, this call never hard-fails. With the all-predecessor
    // fix, demand pairs can no longer be silently DROPPED (every merged pair is applied regardless
    // of who wins a geometric neighbor slot) -- but a demand can still be left PARTIALLY unsatisfied
    // when MAX forces the secant scale-back (`scale < 1`, see relaxOneAxis). true therefore means
    // "relaxed within MAX," not "every demand fully satisfied"; residual unsatisfied demand after
    // scaling is a graded penalty evaluated elsewhere, not a failure this return value encodes.
    return true;
}

std::vector<StripDemand> channelDemands(const Problem& problem, const Placement& placement,
                                         const RoutingResult& routing) {
    std::vector<StripDemand> demands;
    for (const auto& ch : routing.channels) {
        bool has_h = ch.horizontal_usage > kEps;
        bool has_v = ch.vertical_usage > kEps;
        if (!has_h && !has_v) continue;

        ChannelBounds bounds = channelBounds(problem, placement, ch.rect);
        StripDemand demand;
        demand.left = bounds.left;
        demand.right = bounds.right;
        demand.below = bounds.below;
        demand.above = bounds.above;

        // AXIS-CROSSING INVARIANT (p3-design.md §1.2-1.3): vertical_usage is capacity-limited by
        // channel WIDTH (vcap = 25*w) -> relief widens the WIDTH -> charged to the (left,right)
        // pair as need_w. horizontal_usage is capacity-limited by channel HEIGHT (hcap = 25*h) ->
        // relief widens the HEIGHT -> charged to the (below,above) pair as need_h. Never read
        // ch.usage (max aggregate) here -- the two directional fields are read separately.
        if (has_v) demand.need_w = ch.vertical_usage / 25.0;
        if (has_h) demand.need_h = ch.horizontal_usage / 25.0;

        demands.push_back(demand);
    }
    return demands;
}

namespace {

// Explicit O(n^2) legality check mirroring placement_engine.cpp's file-local `placementLegalAt`
// geometric semantics (containment in [0,w]x[0,h] + pairwise non-overlap over ALL rects, edge or
// core). `placementLegalAt` itself is file-local to placement_engine.cpp and not exposed via
// placement_engine.hpp, so per the task brief's "your choice" this is reproduced locally rather
// than widening that file's public surface. Edge-block LOCATION legality (flush/thirds/corner)
// is NOT re-checked here -- `shrinkWrapOutline`'s own commit path already gates edge rects on the
// full `placementLegalAt` (containment + overlap + edge-location) before this function ever sees
// them, so re-deriving that here would be redundant, not a gap.
bool rectsLegalWithinOutline(const Placement& placement, double w, double h) {
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

bool respaceToDemand(const Problem& problem, Placement& placement, const RoutingResult& routing) {
    std::vector<StripDemand> demands = channelDemands(problem, placement, routing);
    if (demands.empty()) return false;  // nothing to relieve (design §3.5 degenerate no-op)

    // Work on a local copy throughout; the caller's `placement` is only ever touched on success.
    Placement candidate = placement;

    relaxToDemand(problem, candidate, demands);

    // Reset outline stamps to 0 before shrink-wrapping so shrinkWrapOutline's lower-bound
    // arithmetic (lowerBoundNeeds) reads the freshly-relaxed core geometry, not a stale stamp from
    // before the relax -- it re-anchors edge blocks and stamps a fresh outline for the widened
    // core (task brief step 3).
    candidate.outline_w = 0.0;
    candidate.outline_h = 0.0;
    OutlineFit fit = shrinkWrapOutline(problem, candidate);
    // Edge-location legality is guaranteed by commitOutlineAt's contract (placement_engine.cpp):
    // fit.legal=true implies edgeRectLegal passed on the stamped rects.
    if (!fit.legal) return false;  // caller's placement untouched

    // Legality gate (task brief step 4): shrinkWrapOutline's own commit path already verified
    // containment + pairwise overlap + edge-location legality via placementLegalAt internally
    // (fit.legal == true means that gate passed at commit time) -- but re-verify pairwise overlap
    // + containment explicitly here as well, since respaceToDemand is a public entry point that
    // must not trust an internal callee's bookkeeping alone for its own legality contract.
    if (!rectsLegalWithinOutline(candidate, candidate.outline_w, candidate.outline_h)) return false;
    if (candidate.outline_w > problem.outline_w + kEps || candidate.outline_h > problem.outline_h + kEps) {
        return false;
    }

    placement = candidate;
    return true;
}

RespaceResult respaceFixedPoint(const Problem& problem, const Placement& base, const ExactCachedFn& exactCached) {
    RespaceResult best;
    best.placement = base;
    best.score = exactCached(base, &best.routing);
    best.improved = false;

    // Early exit (design §3.5): already overflow-free and legal -- nothing for the loop to do.
    if (best.routing.channel_overflow <= kEps && best.routing.open_routes == 0) return best;

    Placement current = base;
    RoutingResult current_routing = best.routing;

    constexpr int kMaxIters = 3;
    for (int iter = 0; iter < kMaxIters; ++iter) {
        Placement candidate = current;
        bool changed = respaceToDemand(problem, candidate, current_routing);
        if (!changed) break;  // no demand left, or infeasible/illegal -- stop (monotone-widen halt)
        if (rectsUnchanged(candidate, current)) break;  // geometric no-op -- further iters can't help

        RoutingResult candidate_routing;
        Score candidate_score = exactCached(candidate, &candidate_routing);

        if (respaceCandidateBetter(candidate_routing, candidate_score.value, best.routing, best.score.value)) {
            best.placement = candidate;
            best.routing = candidate_routing;
            best.score = candidate_score;
            best.improved = true;
        }

        // Advance the loop's own "current" regardless of whether this iteration became the new
        // best -- each pass must relax from the freshly re-routed geometry (monotone-widen keeps
        // this safe: coordinates only ever grow), so the next iteration's demand reflects the
        // latest routing, not a stale one from a worse-scoring earlier candidate.
        current = candidate;
        current_routing = candidate_routing;

        if (current_routing.channel_overflow <= kEps && current_routing.open_routes == 0) break;
    }

    return best;
}

}  // namespace efgr
