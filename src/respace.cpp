#include "respace.hpp"

#include <algorithm>
#include <cmath>

namespace efgr {
namespace {

bool positiveOverlap(double a0, double a1, double b0, double b1) {
    return std::min(a1, b1) - std::max(a0, b0) > kEps;
}

}  // namespace

ChannelBounds channelBounds(const Problem& problem, const Placement& placement, const Rect& channel_rect) {
    ChannelBounds bounds;
    double x0 = channel_rect.x;
    double x1 = channel_rect.right();
    double y0 = channel_rect.y;
    double y1 = channel_rect.top();

    double best_left = -1.0;
    double best_right = -1.0;
    double best_below = -1.0;
    double best_above = -1.0;

    for (std::size_t i = 0; i < problem.blocks.size(); ++i) {
        const Rect& r = placement.rects[i];
        // Left neighbor: block's right edge coincides with the channel's left edge, y-overlapping.
        if (std::abs(r.right() - x0) < kEps && positiveOverlap(r.y, r.top(), y0, y1)) {
            if (bounds.left == -1 || r.right() > best_left) {
                bounds.left = static_cast<int>(i);
                best_left = r.right();
            }
        }
        // Right neighbor: block's left edge coincides with the channel's right edge, y-overlapping.
        if (std::abs(r.x - x1) < kEps && positiveOverlap(r.y, r.top(), y0, y1)) {
            if (bounds.right == -1 || r.x < best_right) {
                bounds.right = static_cast<int>(i);
                best_right = r.x;
            }
        }
        // Below neighbor: block's top edge coincides with the channel's bottom edge, x-overlapping.
        if (std::abs(r.top() - y0) < kEps && positiveOverlap(r.x, r.right(), x0, x1)) {
            if (bounds.below == -1 || r.top() > best_below) {
                bounds.below = static_cast<int>(i);
                best_below = r.top();
            }
        }
        // Above neighbor: block's bottom edge coincides with the channel's top edge, x-overlapping.
        if (std::abs(r.y - y1) < kEps && positiveOverlap(r.x, r.right(), x0, x1)) {
            if (bounds.above == -1 || r.y < best_above) {
                bounds.above = static_cast<int>(i);
                best_above = r.y;
            }
        }
    }
    return bounds;
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
