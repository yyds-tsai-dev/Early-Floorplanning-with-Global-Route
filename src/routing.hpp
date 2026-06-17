#pragma once

#include "model.hpp"

namespace efgr {

std::vector<Channel> extractChannels(const Problem& problem, const Placement& placement);
RoutingResult routeProblem(const Problem& problem, const Placement& placement);

}  // namespace efgr
