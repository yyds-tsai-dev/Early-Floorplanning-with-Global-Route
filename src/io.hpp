#pragma once

#include <string>

#include "model.hpp"

namespace efgr {

Problem parseProblem(const std::string& path);
void writeCfg(const std::string& path, const Problem& problem, const Placement& placement,
              const RoutingResult& routing);

}  // namespace efgr
