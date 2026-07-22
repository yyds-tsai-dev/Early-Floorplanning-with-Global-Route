#pragma once

#include <functional>
#include <string>

#include "model.hpp"
#include "placement_engine.hpp"

namespace efgr {

struct SearchOptions {
    int iters = 1000;
    int seed = 1;
    int exact_interval = 50;
    double initial_temperature = 1000000.0;
    double cooling = 0.995;
    double time_budget_sec = 0.0;  // 0 = disabled; wall-clock stop, checked every 64 iters
};

struct SearchResult {
    std::string engine_name;
    int iterations = 0;
    int exact_evaluations = 0;
    Score best_score;
    Placement best_placement;
    RoutingResult best_routing;
};

// Exact-scoring callback shape used by the elite pass's exact_cache (search.cpp's `exactCached`
// lambda) and injected into outlineSweep below so the sweep shares the same cache/dedupe path.
using ExactCachedFn = std::function<Score(const Placement&, RoutingResult*)>;

struct OutlineSweepResult {
    Score score;
    Placement placement;
    RoutingResult routing;
};

// Elite-pass outline sweep (design p1-design.md §1.4 elite path): for a fixed set of rects
// (`base`, its own outline_w/h ignored -- always re-shrink-wrapped), tries a deterministic ladder
// of declared-outline margins between the tight shrink-wrap and MAX, exact-scores each via the
// caller-supplied `exactCached` (so results share the caller's exact-eval cache), and returns the
// best variant: open_routes==0 strictly preferred over fewer open routes, then lower score.value
// (mirrors compactTowardOrigin's fresh-restamp discipline -- see its comment above). Deterministic:
// fixed margin ladder, no RNG.
OutlineSweepResult outlineSweep(const Problem& problem, const Placement& base, const ExactCachedFn& exactCached);

std::string placementKey(const Placement& placement);
// Pushes non-edge blocks toward the origin against problem.outline_w/h (MAX), reclaiming slack
// left by e.g. a feedthrough-inflation re-decode. Resets the returned placement's stamped declared
// outline (outline_w/h = 0) since compaction moves core rects and invalidates any prior
// shrink-wrap stamp -- callers (exactScore via the unset-path) recompute a fresh one.
Placement compactTowardOrigin(const Problem& problem, Placement placement);
// finalizeFeedthrough commit rule: pay the FT-mismatch deduction down first (QA A1.2),
// independent of total score; between equal-mismatch candidates prefer the lower total.
bool acceptFeedthroughPaydown(const RoutingResult& candidate, double candidate_value,
                              const RoutingResult& best, double best_value);
// Result arbitration (hybrid legs) under the same paydown-first ordering:
// fewer open routes, then lower feedthrough mismatch, then lower total score.
bool paydownBetter(const RoutingResult& a, double a_value, const RoutingResult& b, double b_value);
SearchResult runPlacementSearch(const Problem& problem, const PlacementEngine& engine, const SearchOptions& options);
SearchResult runPlacementSearch(const Problem& problem, const PlacementEngine& engine, const SearchOptions& options,
                                const PlacementState& initial_state);
SearchResult runHybridPlacementSearch(const Problem& problem, const SearchOptions& options);
void finalizeFeedthrough(const Problem& problem, const PlacementEngine& engine, SearchResult& result);

}  // namespace efgr
