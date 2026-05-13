#include "validate.hpp"

namespace efgr {

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
        for (std::size_t j = i + 1; j < placement.rects.size(); ++j) {
            if (overlaps(r, placement.rects[j])) fail("block overlap: " + problem.blocks[i].name + " " + problem.blocks[j].name);
        }
    }
    for (const auto& route : routing.routes) {
        if (route.open) fail("open route");
        for (const auto& step : route.steps) {
            if (step.edge < 1 || step.edge > 4) fail("bad edge id: " + step.object);
        }
    }
    return vr;
}

}  // namespace efgr
