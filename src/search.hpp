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
    // B1 amendment (2026-07-24, research doc 2026-07-23 §2): finalizeFeedthrough simulates the
    // pre-B1 (legacy) mismatch-first paydown rule read-only, alongside its real value-gated commit,
    // so the legacy endpoint stays available as a seed/candidate generator (runHybridPlacementSearch's
    // third leg) without letting it back into selection directly. Always populated by
    // finalizeFeedthrough -- equals best_* verbatim when it early-returns (mismatch already <= kEps).
    Score legacy_paydown_score;
    Placement legacy_paydown_placement;
    RoutingResult legacy_paydown_routing;
};

// Exact-scoring callback shape used by the elite pass's exact_cache (search.cpp's `exactCached`
// lambda) and injected into outlineSweep below so the sweep shares the same cache/dedupe path.
// Third arg (Placement*, may be null) receives the EFFECTIVE placement exactScore actually
// scored/routed -- when exactScore's corrective grow fires it re-anchors edge blocks at a larger
// outline, so the returned routing describes different geometry than the input. Consumers
// (outlineSweep, respaceFixedPoint) store this effective placement, not the raw input, so the
// placement they hand back is mutually consistent with the routing they hand back. The cache stores
// the (score, routing, effective placement) triple keyed by the INPUT placementKey, so cache hits
// reproduce the same triple (dedupe semantics unchanged; the mapping input -> triple is deterministic
// because exactScore is deterministic).
using ExactCachedFn = std::function<Score(const Placement&, RoutingResult*, Placement*)>;

struct OutlineSweepResult {
    Score score;
    Placement placement;
    RoutingResult routing;
};

// Elite-pass outline sweep (design p1-design.md §1.4 elite path): for a fixed set of rects
// (`base`, its own outline_w/h ignored -- always re-shrink-wrapped), tries a deterministic ladder
// of declared-outline margins between the tight shrink-wrap and MAX, PLUS (B3, research doc
// 2026-07-23 §3a-b) a set of integer-µm capacity-denominator micro-pad rungs (W-only/H-only/both,
// added to the tight frontier) -- exact-scores each via the caller-supplied `exactCached` (so
// results share the caller's exact-eval cache), and returns the best variant: open_routes==0
// strictly preferred over fewer open routes, then lower score.value (mirrors compactTowardOrigin's
// fresh-restamp discipline -- see its comment above). Deterministic: fixed margin+pad ladder, no
// RNG. `include_pads` is a test-only escape hatch (default true = "pads on" at every production call
// site) so a test can compare pads-on vs pads-off on the same base placement without duplicating the
// sweep's own logic -- see test/regression_b3_20260724.cpp.
OutlineSweepResult outlineSweep(const Problem& problem, const Placement& base, const ExactCachedFn& exactCached,
                                bool include_pads = true);

std::string placementKey(const Placement& placement);
// Pushes non-edge blocks toward the origin against problem.outline_w/h (MAX), reclaiming slack
// left by e.g. a feedthrough-inflation re-decode. Resets the returned placement's stamped declared
// outline (outline_w/h = 0) since compaction moves core rects and invalidates any prior
// shrink-wrap stamp -- callers (exactScore via the unset-path) recompute a fresh one.
Placement compactTowardOrigin(const Problem& problem, Placement placement);
// finalizeFeedthrough commit rule (value-gated, research doc 2026-07-23 §2): legal (open_routes==0)
// and strictly lower score.value wins outright -- since beta T3, exactScore already prices
// feedthrough mismatch into value at the official kBetaFt rate, so paying mismatch down at the
// price of value would fight the objective it was meant to protect. Lower feedthrough_mismatch is
// only a tiebreak at an exact value tie (hidden-case insurance; costs nothing when values match).
bool acceptFeedthroughPaydown(const RoutingResult& candidate, double candidate_value,
                              const RoutingResult& best, double best_value);
// Result arbitration (hybrid legs), same value-gated rule as acceptFeedthroughPaydown above:
// fewer open routes wins outright, then lower total score.value, then lower feedthrough mismatch
// as an exact-value-tie breaker only.
bool paydownBetter(const RoutingResult& a, double a_value, const RoutingResult& b, double b_value);
SearchResult runPlacementSearch(const Problem& problem, const PlacementEngine& engine, const SearchOptions& options);
SearchResult runPlacementSearch(const Problem& problem, const PlacementEngine& engine, const SearchOptions& options,
                                const PlacementState& initial_state);
SearchResult runHybridPlacementSearch(const Problem& problem, const SearchOptions& options);
void finalizeFeedthrough(const Problem& problem, const PlacementEngine& engine, SearchResult& result);

}  // namespace efgr
