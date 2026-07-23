#pragma once

#include "model.hpp"
#include "routing.hpp"

namespace efgr {

std::pair<double, double> usedOutline(const Problem& problem, const Placement& placement);
std::pair<double, double> usedOutline(const Problem& problem, const Placement& placement,
                                       const RoutingResult& routing);

Score cheapScore(const Problem& problem, const Placement& placement);
// Scores `placement` with real routing. When the tight declared outline is too congested to route
// (open_routes>0), exactScore may perform a one-step corrective grow that RE-ANCHORS edge blocks at
// a larger outline and re-routes -- so the score/routing it returns can describe a DIFFERENT
// placement than the one passed in. `placement_out`, when non-null, receives that effective
// placement (the geometry actually scored/routed -- grown or not), so callers that keep the returned
// score/routing for later emission/comparison can store a mutually-consistent (placement, routing)
// pair instead of pairing the original rects with channels/paths extracted from grown geometry. When
// the grow does not fire, *placement_out equals the input placement (byte-identical outputs).
Score exactScore(const Problem& problem, const Placement& placement, RoutingResult* routing_out,
                 Placement* placement_out = nullptr);

}  // namespace efgr
