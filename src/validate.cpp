#include "validate.hpp"

#include <map>
#include <utility>

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
    // Validate containment/thirds/flush against the placement's declared (shrink-wrapped)
    // outline when stamped, falling back to the problem's outline (MAX) when unset (0) --
    // design p1-design.md §1.6. The declared outline is always <= MAX, so this scoped Problem
    // is strictly tighter than (or equal to) `problem`, never looser.
    Problem scoped = problem;
    if (placement.outline_w > 0.0) scoped.outline_w = placement.outline_w;
    if (placement.outline_h > 0.0) scoped.outline_h = placement.outline_h;

    for (std::size_t i = 0; i < placement.rects.size(); ++i) {
        const auto& r = placement.rects[i];
        if (r.x < -kEps || r.y < -kEps || r.right() > scoped.outline_w + kEps || r.top() > scoped.outline_h + kEps) {
            fail("outline violation: " + problem.blocks[i].name);
        }
        if (!isSoft(problem.blocks[i].type)) {
            if (!near(r.w, problem.blocks[i].width) || !near(r.h, problem.blocks[i].height)) {
                fail("fixed block dimension violation: " + problem.blocks[i].name);
            }
        }
        if (isEdge(problem.blocks[i].type)) {
            if (!satisfiesEdgeLocations(r, scoped, problem.blocks[i].locations)) {
                // G4 (edge-LOCATION soft-anchor exploratory lever, docs/research/2026-07-28-alpha-v5-
                // cost-recalculation-analysis.md): officially a PENALTY (gamma*distance, eval.cpp/
                // route_cost.hpp's edgeLocationViolationDistance), not a legality FAIL -- downgrade to
                // a warning when the search was explicitly allowed to explore off-anchor placements
                // (problem.edge_soft_anchor, main.cpp's hidden --edge-soft-anchor flag); unchanged
                // hard FAIL otherwise. The flag defaults false everywhere, so this branch is dead code
                // (and main.cpp's exit-code gate on vr.ok unaffected) on every default run.
                if (problem.edge_soft_anchor) {
                    vr.messages.push_back("warning: edge location violation: " + problem.blocks[i].name);
                } else {
                    fail("edge location violation: " + problem.blocks[i].name);
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

    // L6.3 (undirected bundle merge, docs/research/2026-07-30-final-levers-design.md): CONN coverage
    // check, flag-INDEPENDENT -- both sides aggregate by unordered pair, so the same check covers
    // problem.bundles in either its parsed directed-per-matrix-entry form or its merged
    // undirected_bundles form identically (n1+n2 vs (n1+n2), the same total). The official checker's
    // CONN_MISMATCH rule compares PATH(A->B)+PATH(B->A) against m[A][B]+m[B][A] (reference.html:
    // 459-460, 641-643); writeCfg (io.cpp) emits every route's nets under a single direction with no
    // metadata to recover which, so this is the finest granularity we can check -- and it is exactly
    // what the official checker itself compares.
    auto pairKey = [](int a, int b) { return a < b ? std::make_pair(a, b) : std::make_pair(b, a); };
    auto blockName = [&](int idx) -> std::string {
        return (idx >= 0 && idx < static_cast<int>(problem.blocks.size())) ? problem.blocks[idx].name
                                                                            : ("#" + std::to_string(idx));
    };
    std::map<std::pair<int, int>, long long> conn_want, conn_got;
    for (const auto& b : problem.bundles) conn_want[pairKey(b.source, b.target)] += b.nets;
    for (const auto& route : routing.routes) {
        if (!route.open) conn_got[pairKey(route.source, route.target)] += route.nets;
    }
    for (const auto& [key, want_nets] : conn_want) {
        auto it = conn_got.find(key);
        long long got_nets = (it != conn_got.end()) ? it->second : 0;
        if (want_nets != got_nets) {
            fail("conn mismatch: " + blockName(key.first) + " " + blockName(key.second) +
                 " want=" + std::to_string(want_nets) + " got=" + std::to_string(got_nets));
        }
    }
    for (const auto& [key, got_nets] : conn_got) {
        if (conn_want.find(key) == conn_want.end()) {
            fail("conn mismatch: " + blockName(key.first) + " " + blockName(key.second) +
                 " want=0 got=" + std::to_string(got_nets));
        }
    }

    return vr;
}

}  // namespace efgr
