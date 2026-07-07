#include "respace.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <utility>

namespace efgr {
namespace {

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

// Relaxes one axis (H: x using need_w/width; V: y using need_h/height) via longest-path over the
// frozen immediate-neighbor adjacency, folding each node's OLD coordinate in as a virtual edge
// from the wall so the relax can only ever raise a coordinate (design p3-design.md §2.2, monotone
// increase; see p3-task-2 review: folding x_old in during the same topological pass, rather than
// clamping post-hoc, is required for correctness on non-origin-compacted input and keeps every
// downstream min-gap edge satisfied). Returns the new coordinate per core block (indexed by
// problem.blocks index; edge blocks and unused entries are left at their input value).
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

    // Immediate-predecessor lookup, mirroring compactTowardOrigin's axisOverlap-based adjacency
    // (search.cpp:16-18,41,51): among all core blocks whose far edge coincides with (or sits at
    // or before) `b`'s near edge and whose cross-range overlaps, keep only the one with the
    // greatest far edge (the true immediate neighbor).
    auto crossOverlap = [&](int a, int b) {
        const Rect& ra = placement.rects[a];
        const Rect& rb = placement.rects[b];
        if (horizontal) return std::min(ra.top(), rb.top()) - std::max(ra.y, rb.y) > kEps;
        return std::min(ra.right(), rb.right()) - std::max(ra.x, rb.x) > kEps;
    };

    std::vector<double> new_coord(n, 0.0);
    for (std::size_t i = 0; i < n; ++i) new_coord[i] = coord[i];

    for (int b : core) {
        double far_b = coord[b];  // near edge of b along this axis
        int pred = -1;            // -1 == wall
        double pred_far = 0.0;    // predecessor's far edge (wall's far edge is 0)
        for (int a : core) {
            if (a == b) continue;
            double far_a = coord[a] + size[a];
            if (far_a <= far_b + kEps && crossOverlap(a, b)) {
                if (pred == -1 || far_a > pred_far) {
                    pred = a;
                    pred_far = far_a;
                }
            }
        }

        double base_weight = (pred == -1) ? 0.0 : size[pred];
        double demand = 0.0;
        auto it = merged.find(std::make_pair(pred, b));
        if (it != merged.end()) demand = it->second;

        double pred_new = (pred == -1) ? 0.0 : new_coord[pred];
        double via_edge = pred_new + base_weight + scale * demand;
        // Fold the old coordinate in as a virtual wall edge in the SAME pass so raises propagate
        // to successors immediately (task-2 review point 1): a block never moves left of where it
        // already was, and gap-safety holds because raising `b` only ever raises what its own
        // successors compute from it.
        new_coord[b] = std::max(coord[b], via_edge);
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
    relaxOneAxis(problem, placement, /*horizontal=*/true, demands);
    relaxOneAxis(problem, placement, /*horizontal=*/false, demands);
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

}  // namespace efgr
