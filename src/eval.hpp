#pragma once

#include "model.hpp"
#include "routing.hpp"

namespace efgr {

Score cheapScore(const Problem& problem, const Placement& placement);
Score exactScore(const Problem& problem, const Placement& placement, RoutingResult* routing_out);

}  // namespace efgr
