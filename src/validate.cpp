#include "validate.hpp"

namespace efgr {
namespace {

bool near(double a, double b) {
    return std::abs(a - b) < kEps;
}

char locSide(const std::string& loc) {
    return loc.empty() ? '\0' : loc[0];
}

bool positiveOverlap(double a0, double a1, double b0, double b1) {
    return std::min(a1, b1) - std::max(a0, b0) > kEps;
}

std::pair<double, double> locSegment(const Problem& problem, const std::string& loc) {
    double third_w = problem.outline_w / 3.0;
    double third_h = problem.outline_h / 3.0;
    if (loc == "TL" || loc == "BL") return {0.0, third_w};
    if (loc == "TM" || loc == "BM") return {third_w, 2.0 * third_w};
    if (loc == "TR" || loc == "BR") return {2.0 * third_w, problem.outline_w};
    if (loc == "LB" || loc == "RB") return {0.0, third_h};
    if (loc == "LM" || loc == "RM") return {third_h, 2.0 * third_h};
    if (loc == "LT" || loc == "RT") return {2.0 * third_h, problem.outline_h};
    return {0.0, 0.0};
}

bool satisfiesEdgeLocation(const Rect& r, const Problem& problem, const std::string& loc) {
    char side = locSide(loc);
    auto [lo, hi] = locSegment(problem, loc);
    if (side == 'T') return near(r.top(), problem.outline_h) && positiveOverlap(r.x, r.right(), lo, hi);
    if (side == 'B') return near(r.y, 0.0) && positiveOverlap(r.x, r.right(), lo, hi);
    if (side == 'L') return near(r.x, 0.0) && positiveOverlap(r.y, r.top(), lo, hi);
    if (side == 'R') return near(r.right(), problem.outline_w) && positiveOverlap(r.y, r.top(), lo, hi);
    return true;
}

bool satisfiesEdgeLocations(const Rect& r, const Problem& problem, const std::vector<std::string>& locs) {
    if (locs.empty()) return true;
    std::vector<char> sides;
    for (const auto& loc : locs) {
        char side = locSide(loc);
        if (std::find(sides.begin(), sides.end(), side) == sides.end()) sides.push_back(side);
    }
    if (sides.size() == 1) {
        for (const auto& loc : locs) {
            if (satisfiesEdgeLocation(r, problem, loc)) return true;
        }
        return false;
    }
    for (const auto& loc : locs) {
        if (!satisfiesEdgeLocation(r, problem, loc)) return false;
    }
    return true;
}

bool portAllows(const Block& block, int edge) {
    if (!block.port_edges.empty()) {
        return std::find(block.port_edges.begin(), block.port_edges.end(), edge) != block.port_edges.end();
    }
    return block.port_edge <= 0 || block.port_edge == edge;
}

}  // namespace

ValidationResult validateSolution(const Problem& problem, const Placement& placement, const RoutingResult& routing) {
    ValidationResult vr;
    auto fail = [&](const std::string& msg) {
        vr.ok = false;
        vr.messages.push_back(msg);
    };
    for (std::size_t i = 0; i < placement.rects.size(); ++i) {
        const auto& r = placement.rects[i];
        if (r.x < -kEps || r.y < -kEps || r.right() > problem.outline_w + kEps || r.top() > problem.outline_h + kEps) {
            fail("outline violation: " + problem.blocks[i].name);
        }
        if (!isSoft(problem.blocks[i].type)) {
            if (!near(r.w, problem.blocks[i].width) || !near(r.h, problem.blocks[i].height)) {
                fail("fixed block dimension violation: " + problem.blocks[i].name);
            }
        }
        if (isEdge(problem.blocks[i].type)) {
            if (!satisfiesEdgeLocations(r, problem, problem.blocks[i].locations)) {
                fail("edge location violation: " + problem.blocks[i].name);
            }
        }
        for (std::size_t j = i + 1; j < placement.rects.size(); ++j) {
            if (overlaps(r, placement.rects[j])) fail("block overlap: " + problem.blocks[i].name + " " + problem.blocks[j].name);
        }
    }
    for (const auto& route : routing.routes) {
        if (route.open) fail("open route");
        if (!route.open && !route.steps.empty()) {
            if (route.source >= 0 && route.source < static_cast<int>(problem.blocks.size())) {
                const auto& source = problem.blocks[route.source];
                if (!portAllows(source, route.steps.front().edge)) {
                    fail("source port edge violation: " + source.name);
                }
            }
            if (route.target >= 0 && route.target < static_cast<int>(problem.blocks.size())) {
                const auto& target = problem.blocks[route.target];
                if (!portAllows(target, route.steps.back().edge)) {
                    fail("target port edge violation: " + target.name);
                }
            }
        }
        for (const auto& step : route.steps) {
            if (step.edge < 1 || step.edge > 4) fail("bad edge id: " + step.object);
        }
    }
    return vr;
}

}  // namespace efgr
