#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <utility>
#include <vector>

#include "model.hpp"

namespace efgr {

// Beta Task 1 (byte-identical refactor): shared route-geometry/accounting helpers, moved verbatim
// out of routing.cpp so a future .cfg re-scorer can reuse the solver's exact wirelength/usage
// accounting instead of re-deriving it. Header-only, no I/O; the only mutation is to the Channel
// objects the caller points `accumulatePathUsage`'s `node_channels` at.

// Edge numbering (QA A22): 1 = left, 2 = top, 3 = right, 4 = bottom (clockwise from left).
// Moved verbatim from routing.cpp:24.
inline int edgeFromTo(const Rect& a, const Rect& b) {
    if (std::abs(a.x - b.right()) < kEps) return 1;
    if (std::abs(a.top() - b.y) < kEps) return 2;
    if (std::abs(a.right() - b.x) < kEps) return 3;
    return 4;
}

// Moved verbatim from routing.cpp:31.
inline std::pair<double, double> guidePoint(const Rect& a, const Rect& b) {
    if (std::abs(a.right() - b.x) < kEps || std::abs(b.right() - a.x) < kEps) {
        double x = std::abs(a.right() - b.x) < kEps ? a.right() : b.right();
        double lo = std::max(a.y, b.y);
        double hi = std::min(a.top(), b.top());
        return {x, (lo + hi) * 0.5};
    }
    double y = std::abs(a.top() - b.y) < kEps ? a.top() : b.top();
    double lo = std::max(a.x, b.x);
    double hi = std::min(a.right(), b.right());
    return {(lo + hi) * 0.5, y};
}

// One routed rect-sequence's contribution to wirelength and per-channel directional usage.
// `node_rects[i]` is the rect of the i-th node on the path; `node_channels[i]` is the Channel* the
// i-th node owns if it is a channel node, else nullptr. Implements exactly the accumulation loop
// formerly inlined in routing.cpp:316-345: guides = midpoints of consecutive shared borders (QA
// A41), per-segment dx>kEps charges horizontal_usage, dy>kEps charges vertical_usage on the channel
// that owns the segment (and ch.usage = max(horizontal_usage, vertical_usage) is refreshed on every
// charge), and WL = nets * Sum manhattan(consecutive guides). Verbatim transplant: same iteration
// order, same kEps comparisons, same ch.usage update. Soft-block feedthrough-net accrual is NOT part
// of this helper (routing.cpp keeps that loop) and steps emission is NOT part of this helper
// (routing.cpp keeps that loop too) -- only WL + per-channel usage accumulation moved here.
inline double accumulatePathUsage(const std::vector<Rect>& node_rects,
                                   const std::vector<Channel*>& node_channels,
                                   double nets) {
    std::vector<std::pair<double, double>> guides;
    for (std::size_t i = 0; i + 1 < node_rects.size(); ++i) {
        guides.push_back(guidePoint(node_rects[i], node_rects[i + 1]));
    }
    double wirelength = 0.0;
    for (std::size_t i = 0; i + 1 < guides.size(); ++i) {
        wirelength += manhattan(guides[i].first, guides[i].second, guides[i + 1].first, guides[i + 1].second) * nets;
    }

    for (std::size_t i = 1; i + 1 < node_rects.size(); ++i) {
        if (node_channels[i] == nullptr || !(i - 1 < guides.size() && i < guides.size())) continue;
        Channel* ch = node_channels[i];
        double dx = std::abs(guides[i].first - guides[i - 1].first);
        double dy = std::abs(guides[i].second - guides[i - 1].second);
        if (dx > kEps) ch->horizontal_usage += nets;
        if (dy > kEps) ch->vertical_usage += nets;
        ch->usage = std::max(ch->horizontal_usage, ch->vertical_usage);
    }
    return wirelength;
}

}  // namespace efgr
