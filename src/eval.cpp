#include "eval.hpp"

namespace efgr {

Score cheapScore(const Problem& problem, const Placement& placement) {
    Score score;
    score.outline_area = problem.outline_w * problem.outline_h;
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
    score.penalty = penalty;
    score.value = score.outline_area + problem.alpha * wire_proxy + penalty;
    return score;
}

Score exactScore(const Problem& problem, const Placement& placement, RoutingResult* routing_out) {
    RoutingResult routing = routeProblem(problem, placement);
    Score score = cheapScore(problem, placement);
    score.wirelength = routing.total_wirelength;
    score.penalty += routing.open_routes * 1e15;
    score.penalty += routing.channel_overflow * 1e6;
    score.penalty += routing.feedthrough_mismatch * 1e6;
    score.value = score.outline_area + problem.alpha * score.wirelength + score.penalty;
    if (routing_out) *routing_out = routing;
    return score;
}

}  // namespace efgr
