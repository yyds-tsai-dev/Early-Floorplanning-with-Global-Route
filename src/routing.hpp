#pragma once

#include "model.hpp"

namespace efgr {

std::vector<Channel> extractChannels(const Problem& problem, const Placement& placement);

// P2 Task 2 (design p2-design.md §Q1-Q2 core; plan Global Constraint 7 comparison gate): `cost_driven`
// defaults to false so every existing caller (eval.cpp, search.cpp) is byte-identical to pre-P2
// behavior. When true, routeChunk runs a single unified Dijkstra pass per chunk with T1's
// cost-unit edge weights (route_cost.hpp) instead of the legacy two-pass heuristic; see routing.cpp
// for the edge-pricing details.
RoutingResult routeProblem(const Problem& problem, const Placement& placement, bool cost_driven = false);

}  // namespace efgr
