#pragma once

#include "model.hpp"
#include "routing.hpp"

namespace efgr {

std::pair<double, double> usedOutline(const Problem& problem, const Placement& placement);
std::pair<double, double> usedOutline(const Problem& problem, const Placement& placement,
                                       const RoutingResult& routing);

Score cheapScore(const Problem& problem, const Placement& placement);
Score exactScore(const Problem& problem, const Placement& placement, RoutingResult* routing_out);

}  // namespace efgr
