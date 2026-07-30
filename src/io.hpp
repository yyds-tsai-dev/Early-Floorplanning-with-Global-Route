#pragma once

#include <string>

#include "model.hpp"

namespace efgr {

Problem parseProblem(const std::string& path);

// L6 (undirected bundle merge, docs/research/2026-07-30-final-levers-design.md §L6.1): pure function,
// no I/O -- merges each unordered {source,target} pair across `bundles` into a single Bundle with
// nets summed (m[A][B]+m[B][A]) and direction normalized to (min(source,target), max(source,target)).
// Output is sorted ascending by (source,target) (an std::map<pair<int,int>,int> iteration order),
// i.e. an upper-triangular row-major scan. Never called by parseProblem -- wired in by main.cpp's
// hidden --undirected-bundles flag AFTER parsing (Problem.undirected_bundles, model.hpp), so every
// existing caller of parseProblem is unaffected.
std::vector<Bundle> mergeUndirectedBundles(const std::vector<Bundle>& bundles);

void writeCfg(const std::string& path, const Problem& problem, const Placement& placement,
              const RoutingResult& routing);

}  // namespace efgr
