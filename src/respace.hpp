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
std::vector<StripDemand> channelDemands(const Problem& problem, const Placement& placement,
                                         const RoutingResult& routing);

}  // namespace efgr
