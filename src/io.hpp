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
// hidden --undirected-bundles flag OR (P0b, docs/research/2026-08-14-final-levers-verdicts-and-0811-
// intel.md §3.3/§5 item 2) the bundlesValueSymmetric auto-rule below, AFTER parsing
// (Problem.undirected_bundles, model.hpp), so every existing caller of parseProblem is unaffected.
std::vector<Bundle> mergeUndirectedBundles(const std::vector<Bundle>& bundles);

// P0b (L6 value-symmetric auto-merge rule, docs/research/2026-08-14-final-levers-verdicts-and-0811-
// intel.md §3.3/§5 item 2, .superpowers/sdd/p0b-brief.md): pure function, no I/O -- true iff `bundles`
// is fully value-symmetric, i.e. for every unordered pair {s,t} (s!=t) the summed directed nets s->t
// equal the summed directed nets t->s (self-pairs s==t are trivially symmetric and skipped; a mirror
// entry entirely absent counts as 0, so a one-sided pair is NOT symmetric; empty input is vacuously
// true). This is the L6 adoption criterion measured to fire exactly on the winners (case1/case5) and
// exclude every loser (case0/2/3/4/6/7) -- blanket default-on was rejected (§3.3). Never called by
// parseProblem -- wired in by main.cpp's forced-off > flag-on > auto precedence AFTER parsing
// (Problem.undirected_bundles, model.hpp): with neither --no-undirected-bundles nor
// --undirected-bundles given, main.cpp sets undirected_bundles to this function's result on the
// parsed bundles before the mergeUndirectedBundles call above.
bool bundlesValueSymmetric(const std::vector<Bundle>& bundles);

void writeCfg(const std::string& path, const Problem& problem, const Placement& placement,
              const RoutingResult& routing);

}  // namespace efgr
