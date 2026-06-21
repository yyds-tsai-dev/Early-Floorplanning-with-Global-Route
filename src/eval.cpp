#include "eval.hpp"

namespace efgr {
namespace {

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

double ftRateForProxy(const Block& block, double nets) {
    if (block.ft_rates.empty()) return 0.0;
    if (nets <= 3000.0) return block.ft_rates[0];
    if (nets <= 6000.0) return block.ft_rates.size() > 1 ? block.ft_rates[1] : block.ft_rates.back();
    if (nets <= 9000.0) return block.ft_rates.size() > 2 ? block.ft_rates[2] : block.ft_rates.back();
    return block.ft_rates.size() > 3 ? block.ft_rates[3] : block.ft_rates.back();
}

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
            double delta = (bundle.nets / 25.0) * ftRateForProxy(problem.blocks[i], bundle.nets) * 0.5;
            double side = std::sqrt(std::max(1.0, problem.blocks[i].area)) + delta;
            double required_area = side * side;
            double actual_area = placement.rects[i].w * placement.rects[i].h;
            penalty += std::max(0.0, required_area - actual_area) * 1000.0;
        }
    }
    return penalty;
}

}  // namespace

Score cheapScore(const Problem& problem, const Placement& placement) {
    Score score;
    auto [used_w, used_h] = usedOutline(problem, placement);
    score.outline_area = used_w * used_h;
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
    score.value = score.outline_area + problem.alpha * wire_proxy + penalty;
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
