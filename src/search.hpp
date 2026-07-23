#pragma once

#include <functional>
#include <string>
#include <utility>
#include <vector>

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
    // B6 (rank-fidelity instrumentation + adaptive interval, .superpowers/sdd/b6-brief.md;
    // research doc 2026-07-23 §2 lit, saACM-ES GECCO 2012): when true, runPlacementSearch
    // recomputes windowed Kendall tau (rank_stats.hpp) every kAdaptiveWindow recorded SA-loop
    // fidelity pairs and adapts the LIVE exact_interval (widen on high fidelity, narrow on low --
    // see search.cpp's kAdaptive* constants). DEFAULT OFF: off must reproduce today's
    // exact_interval-is-constant behavior byte-identically (hard acceptance gate, like B1's
    // original gate). An experiment hook for B5's sweep, not a production default -- flipping the
    // default is B5's budget-gate decision, not this one's.
    bool adaptive_interval = false;
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
    // B4.1 (route-pricing portfolio, b4-report.md): true iff the OFFICIAL router pricing (B4) won
    // runPricingPortfolio's fold; false iff the RETAINED pre-B4 pricing won. Telemetry only (never
    // read by scoring/caching logic) -- surfaced in main.cpp's stderr summary as
    // `pricing=official|legacy`. Defaults true (matches Problem::route_pricing_official's default)
    // for any SearchResult not produced by runPricingPortfolio, so existing direct callers of
    // runPlacementSearch/runHybridPlacementSearch are unaffected.
    bool pricing_official = true;
    // B6 (rank-fidelity instrumentation, .superpowers/sdd/b6-brief.md): observation-only -- never
    // READ by any decision in runPlacementSearch/finalizeFeedthrough/outlineSweep, purely reporting
    // data for main.cpp's stderr summary (tau=/tau_w= fields) and the flag-gated adaptive-interval
    // control (SearchOptions.adaptive_interval). One (cand_score.value, exact.value) pair recorded
    // at the SA loop's existing exact-eval trigger site (periodic interval OR cheap-beats-best), in
    // iteration order, for EVERY exactCached call there regardless of cache hit/miss (a cache hit is
    // still a valid, if duplicate, rank-fidelity observation of that placement). Like
    // legacy_paydown_*/pricing_official above, a hybrid/portfolio fold (runHybridPlacementSearch,
    // runPricingPortfolio) does a whole-struct copy of the PICKED leg only -- the other legs'
    // recorded pairs are simply discarded, never merged, so main.cpp's stderr line reports the
    // picked leg's rank fidelity only.
    std::vector<std::pair<double, double>> fidelity_pairs;
    // B6: elite-pool ranking snapshot, captured once at the start of the elite loop (before its own
    // sweep/compaction/respace adoptions can move any candidate's placement) -- one (cheap, exact)
    // pair per top-K cheap candidate, in POOL ORDER = CHEAP RANK ASCENDING (index 0 = best/lowest
    // cheap value, matching top_candidates' own sort in runPlacementSearch). Exact values come from
    // exactCached (memoized by placementKey), so a pool member the SA loop already exact-scored is a
    // cache hit here, not a new eval. Same fold behavior as fidelity_pairs above: picked leg's vector
    // only.
    std::vector<std::pair<double, double>> elite_pairs;
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
// B1.1 (Codex adversarial review, 2026-07-24, .superpowers/sdd/b1-report.md "Post-review
// amendment"): exact (bitwise ==) placement equality -- same rect count, every rect's x/y/w/h and
// the outline_w/h compare == with no tolerance. Deliberately stricter than `placementKey` equality
// (which rounds to 3 decimals via llround(x*1000)): BStarEngine::seedFromPlacement
// (placement_engine.cpp:622) sorts by RAW coordinates with a kEps=1e-6 tie-band, so two placements
// that round to the SAME placementKey can still differ enough (e.g. 1e-4) to sort differently and
// produce a different seed state. Bitwise equality of the seed placement, combined with an
// identically-constructed fresh mt19937(seed), provably implies identical seedFromPlacement output
// -- see runHybridPlacementSearch's bstar_b skip, the one caller this exists for.
bool placementsBitwiseEqual(const Placement& a, const Placement& b);
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
// B4.1 (route-pricing portfolio, b4-report.md): runs the FULL requested engine search ("sp",
// "bstar", or "hybrid" -- same dispatch main.cpp used to do inline) TWICE, on two `problem` copies
// differing ONLY in `route_pricing_official` (model.hpp) -- official (B4's per-edge marginal
// pricing) and legacy (byte-identical to pre-B4/HEAD 207d08c) -- then folds the two SearchResults
// with the existing value-gated `paydownBetter` (legality-first, then value). Restores the
// per-case <=-pre-B4 Pareto floor a changed search trajectory can otherwise miss, since the legacy
// leg reproduces pre-B4 behavior bit-exactly (B4 never touched search.cpp) -- the fold's floor is
// structural. `options` is shared by both legs (same seed); a wall-clock time_budget, if set, is
// split 50/50 across them. Existing direct callers of runPlacementSearch/runHybridPlacementSearch
// keep single-pricing semantics unchanged.
SearchResult runPricingPortfolio(const Problem& problem, const std::string& engine, const SearchOptions& options);

}  // namespace efgr
