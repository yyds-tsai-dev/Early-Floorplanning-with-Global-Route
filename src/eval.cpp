#include "eval.hpp"

#include <tuple>

#include "ft.hpp"

namespace efgr {

std::pair<double, double> usedOutline(const Problem& problem, const Placement& placement) {
    double w = 0.0;
    double h = 0.0;
    for (const auto& r : placement.rects) {
        w = std::max(w, r.right());
        h = std::max(h, r.top());
    }
    w = std::max(1.0, std::min(problem.outline_w, w));
    h = std::max(1.0, std::min(problem.outline_h, h));
    return {w, h};
}

std::pair<double, double> usedOutline(const Problem& problem, const Placement& placement,
                                       const RoutingResult& routing) {
    double w = 0.0;
    double h = 0.0;
    for (const auto& r : placement.rects) {
        w = std::max(w, r.right());
        h = std::max(h, r.top());
    }
    for (const auto& ch : routing.channels) {
        w = std::max(w, ch.rect.right());
        h = std::max(h, ch.rect.top());
    }
    w = std::max(1.0, std::min(problem.outline_w, w));
    h = std::max(1.0, std::min(problem.outline_h, h));
    return {w, h};
}

namespace {

bool intersectsOpen(const Rect& a, const Rect& b) {
    return a.x + kEps < b.right() && b.x + kEps < a.right() &&
           a.y + kEps < b.top() && b.y + kEps < a.top();
}

double routeAwareProxyPenalty(const Problem& problem, const Placement& placement) {
    double penalty = 0.0;
    for (const auto& bundle : problem.bundles) {
        const auto& a = placement.rects[bundle.source];
        const auto& b = placement.rects[bundle.target];
        double dx = std::abs(a.cx() - b.cx());
        double dy = std::abs(a.cy() - b.cy());
        double narrow = 0.0;
        if (dx >= dy) {
            narrow = std::max(1.0, std::min(a.h, b.h));
        } else {
            narrow = std::max(1.0, std::min(a.w, b.w));
        }
        double capacity = narrow * 25.0;
        double overflow = std::max(0.0, static_cast<double>(bundle.nets) - capacity);
        penalty += overflow * overflow * 100.0;

        Rect bbox{std::min(a.cx(), b.cx()), std::min(a.cy(), b.cy()),
                  std::max(1.0, std::abs(a.cx() - b.cx())), std::max(1.0, std::abs(a.cy() - b.cy()))};
        for (std::size_t i = 0; i < problem.blocks.size(); ++i) {
            if (static_cast<int>(i) == bundle.source || static_cast<int>(i) == bundle.target) continue;
            if (!isSoft(problem.blocks[i].type)) continue;
            if (!intersectsOpen(bbox, placement.rects[i])) continue;
            double base = problem.blocks[i].ft_base_area > 0.0 ? problem.blocks[i].ft_base_area : problem.blocks[i].area;
            double required_area = ftRequiredArea(base, ftExtend(bundle.nets, ftTierRate(problem.blocks[i], bundle.nets)));
            // Proxy intentionally skips round2: cheap search gradient, not the scored quantity (exact path in routing.cpp rounds).
            double actual_area = placement.rects[i].w * placement.rects[i].h;
            penalty += std::max(0.0, required_area - actual_area) * 1000.0;
        }
    }
    return penalty;
}

// Crude v1 whitespace/channel-demand proxy (design p1-design.md §1.3): for each bundle, reserve
// area proportional to net count and the *aligned* axis distance between source/target centers
// (min(dx,dy) -- zero when the pair is already adjacent on one axis, so no margin is charged).
// Deliberately crude: biases cheapScore's SA gradient away from packings so tight they would
// overflow, not a routing prediction. Clamped so the shrink-wrapped outline_area (W*H) plus this
// margin never exceeds the MAX outline area -- the containment penalty (vs MAX) stays the hard
// legality gate; this term only shapes the gradient between the shrunk area and MAX.
double channelMarginProxy(const Problem& problem, const Placement& placement, double w, double h) {
    double margin = 0.0;
    for (const auto& bundle : problem.bundles) {
        const auto& a = placement.rects[bundle.source];
        const auto& b = placement.rects[bundle.target];
        double dx = std::abs(a.cx() - b.cx());
        double dy = std::abs(a.cy() - b.cy());
        margin += (static_cast<double>(bundle.nets) / 25.0) * std::min(dx, dy);
    }
    double max_area = problem.outline_w * problem.outline_h;
    double headroom = std::max(0.0, max_area - w * h);
    return std::min(margin, headroom);
}

}  // namespace

Score cheapScore(const Problem& problem, const Placement& placement) {
    Score score;
    double w = placement.outline_w;
    double h = placement.outline_h;
    if (w <= 0.0 || h <= 0.0) std::tie(w, h) = usedOutline(problem, placement);
    score.outline_area = w * h + channelMarginProxy(problem, placement, w, h);
    double penalty = 0.0;
    for (std::size_t i = 0; i < placement.rects.size(); ++i) {
        const auto& r = placement.rects[i];
        if (r.x < 0.0 || r.y < 0.0 || r.right() > problem.outline_w || r.top() > problem.outline_h) penalty += 1e12;
        for (std::size_t j = i + 1; j < placement.rects.size(); ++j) {
            penalty += overlapArea(r, placement.rects[j]) * 1e9;
        }
    }
    double wire_proxy = 0.0;
    for (const auto& b : problem.bundles) {
        const auto& a = placement.rects[b.source];
        const auto& c = placement.rects[b.target];
        wire_proxy += manhattan(a.cx(), a.cy(), c.cx(), c.cy()) * b.nets;
    }
    score.wirelength = wire_proxy;
    score.penalty = penalty + routeAwareProxyPenalty(problem, placement);
    score.value = score.outline_area + problem.alpha * wire_proxy + score.penalty;
    return score;
}

Score exactScore(const Problem& problem, const Placement& placement, RoutingResult* routing_out) {
    auto [used_w, used_h] = usedOutline(problem, placement);
    Problem scoped = problem;
    scoped.outline_w = used_w;
    scoped.outline_h = used_h;
    RoutingResult routing = routeProblem(scoped, placement);
    Score score = cheapScore(scoped, placement);
    score.wirelength = routing.total_wirelength;
    score.penalty += routing.open_routes * 1e15;
    score.penalty += routing.channel_overflow * 1e6;
    score.penalty += routing.feedthrough_mismatch * 1e6;
    score.value = score.outline_area + problem.alpha * score.wirelength + score.penalty;
    if (routing_out) *routing_out = routing;
    return score;
}

}  // namespace efgr
