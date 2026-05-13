#pragma once

#include <string>
#include <vector>

#include "model.hpp"

namespace efgr {

struct ValidationResult {
    bool ok = true;
    std::vector<std::string> messages;
};

ValidationResult validateSolution(const Problem& problem, const Placement& placement, const RoutingResult& routing);

}  // namespace efgr
