#include "validate.hpp"

namespace efgr {
namespace {

bool near(double a, double b) {
    return std::abs(a - b) < kEps;
}

bool inRange(double lo, double value, double hi) {
    return value + kEps >= lo && value <= hi + kEps;
}

bool satisfiesEdgeLocation(const Rect& r, const Problem& problem, const std::string& loc) {
    double third_w = problem.outline_w / 3.0;
    double third_h = problem.outline_h / 3.0;
    if (loc == "TL") return near(r.top(), problem.outline_h) && inRange(0.0, r.x, third_w) && r.right() <= third_w + kEps;
    if (loc == "TM") return near(r.top(), problem.outline_h) && inRange(third_w, r.x, 2.0 * third_w) && r.right() <= 2.0 * third_w + kEps;
    if (loc == "TR") return near(r.top(), problem.outline_h) && r.x + kEps >= 2.0 * third_w && near(r.right(), problem.outline_w);
    if (loc == "BL") return near(r.y, 0.0) && inRange(0.0, r.x, third_w) && r.right() <= third_w + kEps;
    if (loc == "BM") return near(r.y, 0.0) && inRange(third_w, r.x, 2.0 * third_w) && r.right() <= 2.0 * third_w + kEps;
    if (loc == "BR") return near(r.y, 0.0) && r.x + kEps >= 2.0 * third_w && near(r.right(), problem.outline_w);
    if (loc == "LT") return near(r.x, 0.0) && r.y + kEps >= 2.0 * third_h && near(r.top(), problem.outline_h);
    if (loc == "LM") return near(r.x, 0.0) && inRange(third_h, r.y, 2.0 * third_h) && r.top() <= 2.0 * third_h + kEps;
    if (loc == "LB") return near(r.x, 0.0) && inRange(0.0, r.y, third_h) && r.top() <= third_h + kEps;
    if (loc == "RT") return near(r.right(), problem.outline_w) && r.y + kEps >= 2.0 * third_h && near(r.top(), problem.outline_h);
    if (loc == "RM") return near(r.right(), problem.outline_w) && inRange(third_h, r.y, 2.0 * third_h) && r.top() <= 2.0 * third_h + kEps;
    if (loc == "RB") return near(r.right(), problem.outline_w) && inRange(0.0, r.y, third_h) && r.top() <= third_h + kEps;
    return true;
}

bool satisfiesEdgeBoundary(const Rect& r, const Problem& problem, const std::string& loc) {
    if (loc.empty()) return true;
    if (loc[0] == 'T') return near(r.top(), problem.outline_h);
    if (loc[0] == 'B') return near(r.y, 0.0);
    if (loc[0] == 'L') return near(r.x, 0.0);
    if (loc[0] == 'R') return near(r.right(), problem.outline_w);
    return true;
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
            for (const auto& loc : problem.blocks[i].locations) {
                bool ok = problem.blocks[i].locations.size() > 1
                              ? satisfiesEdgeBoundary(r, problem, loc)
                              : satisfiesEdgeLocation(r, problem, loc);
                if (!ok) {
                    fail("edge location violation: " + problem.blocks[i].name + " " + loc);
                }
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
                if (source.port_edge > 0 && route.steps.front().edge != source.port_edge) {
                    fail("source port edge violation: " + source.name);
                }
            }
            if (route.target >= 0 && route.target < static_cast<int>(problem.blocks.size())) {
                const auto& target = problem.blocks[route.target];
                if (target.port_edge > 0 && route.steps.back().edge != target.port_edge) {
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
