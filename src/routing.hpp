#pragma once

#include "model.hpp"

namespace efgr {

std::vector<Channel> extractChannels(const Problem& problem, const Placement& placement);

// P2 Task 6 (design p2-design.md §9 T6; plan Global Constraint 11 default-on rollout): `cost_driven`
// now defaults to true, so every existing production caller (eval.cpp, search.cpp) picks up the
// cost-driven + negotiated + adaptive router (routing.cpp) via its comparison gate against the
// legacy two-pass result -- never worse on the (open_routes, then alpha*WL + kOverflowAreaWeight*
// overflow + kFtMismatchWeight*mismatch) lexicographic key (routing.cpp's `runLegacyPass` +
// `roundCost` gate, landed T5/T5.1). The legacy path stays reachable via an explicit
// `cost_driven=false` argument (used by tests and, internally, by routing.cpp's own
// `force_legacy` comparison-gate run) -- no signature change, so no caller needs updating.
RoutingResult routeProblem(const Problem& problem, const Placement& placement, bool cost_driven = true);

}  // namespace efgr
