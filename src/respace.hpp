#pragma once

#include <vector>

#include "model.hpp"

namespace efgr {

// Bounding core-block indices (or -1 for an outline wall) for a single channel, geometrically
// re-derived from the final placement rects + the channel's own rect. Pure; no mutation.
struct ChannelBounds {
    int left = -1;
    int right = -1;
    int below = -1;
    int above = -1;
};

// Per-block-pair gap demand, addressed by directional need (design p3-design.md §1.2-1.3,
// the axis-crossing invariant): need_w charges the (left,right) horizontal-gap pair, need_h
// charges the (below,above) vertical-gap pair. Indices are -1 for an outline wall.
struct StripDemand {
    int left = -1;
    int right = -1;
    int below = -1;
    int above = -1;
    double need_w = 0.0;
    double need_h = 0.0;
};

// Re-derives, for one channel, the block indices (or -1 for a wall) bounding it on each side:
// left/right via x-edge match + y-overlap, below/above via y-edge match + x-overlap. Pure;
// reads only `problem`/`placement`; does not touch RoutingResult/Channel structs.
ChannelBounds channelBounds(const Problem& problem, const Placement& placement, const Rect& channel_rect);

// For each channel with positive directional usage, derives its bounding block pair(s) and the
// absolute gap each one demands (design p3-design.md §1.2): vertical_usage -> need_w on the
// (left,right) pair; horizontal_usage -> need_h on the (below,above) pair. Channels with zero
// usage in both directions produce no entry. Pure; no mutation of problem/placement/routing.
// NOTE: entries are one-per-channel and NOT pre-merged -- when multiple channels stack on the
// same (left,right) or (below,above) pair (design p3-design.md §1.2 "Multiple channels per
// strip"), callers MUST max-merge need_w/need_h across all entries sharing that pair before
// using them as a single edge weight; the graph works in absolute per-pair gaps, so summing or
// using an arbitrary single entry would under- or mis-size the gap.
std::vector<StripDemand> channelDemands(const Problem& problem, const Placement& placement,
                                         const RoutingResult& routing);

// Relaxes CORE (non-edge) block coordinates to satisfy the strip demands via two independent
// longest-path constraint-graph passes (design p3-design.md §1-2, decision 4): an H-pass over
// x-coordinates using need_w on (left,right) pairs, then a V-pass over y-coordinates using
// need_h on (below,above) pairs. Node order per axis is frozen from `placement`'s CURRENT
// coordinates (immediate-neighbor adjacency, like `compactTowardOrigin`); the relax only ever
// increases a core block's coordinate (monotone widen), so relative order can never flip.
// Edge-type blocks are not graph nodes and are left untouched (re-anchored elsewhere). If the
// unclamped relax would push an axis's extent past `problem.outline_w`/`outline_h` (MAX), the
// added demand slack is proportionally scaled back (never the base non-overlap terms) so the
// result never exceeds MAX; this is a partial-relief case, not a failure. Mutates `placement`
// in place and always returns true (per design failure-mode 1, residual unsatisfied demand is a
// graded penalty elsewhere, not a hard failure here).
bool relaxToDemand(const Problem& problem, Placement& placement, const std::vector<StripDemand>& demands);

}  // namespace efgr
