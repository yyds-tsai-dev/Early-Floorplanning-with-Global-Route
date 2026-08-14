#pragma once

#include <functional>
#include <random>
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
    // B5a.1 (elite-admission variant portfolio, coordinator adjudication after B5a's own STOP --
    // .superpowers/sdd/b5a-report.md): when true, runPlacementSearch's elite pass uses the NEW (v2)
    // admission mechanism -- widened K_cheap=48 cheap prefilter, exact-value archive, insurance
    // reservoir, rung-0 tight-exact re-rank (buildEliteFullSet, b5-design.md §2). DEFAULT FALSE:
    // false must reproduce HEAD 408a297 (pre-B5a) BYTE-IDENTICALLY -- kTopCheapCandidates reverts to
    // 16, no archive/insurance machinery runs at all (not even constructed), and the elite loop
    // iterates top_candidates directly, the exact pre-B5a code path (hard acceptance gate, same
    // discipline as adaptive_interval's own byte-identity requirement above). B5a's own end-to-end
    // measurement found v2 alone regresses case1/case3 at default flags (a shared-search-path
    // interaction with runPricingPortfolio's legacy-leg floor and the hybrid bstar-seed cascade --
    // see b5a-report.md) while winning case2/case4 substantially, so v2 is never turned on directly
    // by ordinary callers/CLI -- runPricingPortfolio below runs BOTH v1 and v2 (crossed with
    // route_pricing_official) as 4 separate legs and folds by value, giving v2's wins for free while
    // the v1 legs hold the floor.
    bool elite_admission_v2 = false;
    // B5c (move portfolio, .superpowers/sdd/b5-design.md §4): solver-internal knob selecting which
    // move-proposal table each PlacementEngine::mutate uses -- false (default) is the pre-B5c table
    // (structural swap/soft-aspect/edge-choice only), BYTE-IDENTICAL to history (hard acceptance
    // gate: every new random draw the moves add is gated behind this flag, so the main mutate/accept
    // rng stream is untouched when off). true adds the wire-median / abutment-targeted / greedy-
    // reposition moves (design §4.2-§4.5) at 0.45 combined proposal probability.
    // THREADING NOTE (least-churn choice, documented per this task's own instruction): `mutate`'s
    // signature is fixed at (problem, state, rng) across the whole codebase (every override, every
    // call site, including test-defined PlacementEngine subclasses) -- adding a 4th parameter would
    // touch the abstract interface and every one of those sites. Instead this flag is baked into the
    // CONSTRUCTED engine instance (placement_engine.hpp's makePlacementEngine(name,
    // move_portfolio_v2) second argument, defaulted false) -- SequencePairEngine/BStarEngine each
    // store it as a private member `mutate()` reads directly, so runPlacementSearch/
    // finalizeFeedthrough (which only ever call `engine.mutate(...)` on an already-built engine
    // reference) need NO changes at all. This field is therefore NOT read anywhere in search.cpp
    // directly -- it exists purely so runPricingPortfolio's runLeg can read it off the winning
    // SearchVariant and construct the leg's engine(s) with the matching boolean (see runLeg/
    // runHybridPlacementSearch below). CAVEAT: setting this field alone on a SearchOptions passed to
    // a manually-constructed engine has NO effect unless that SAME boolean was also passed to
    // makePlacementEngine at construction time -- runLeg is the one production call site that keeps
    // the two in sync; a direct caller of runPlacementSearch bypassing runLeg must do the same.
    bool move_portfolio_v2 = false;
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
    // B5a.1 (elite-admission variant portfolio, .superpowers/sdd/b5a-report.md): true iff the NEW
    // (v2) tight-exact-reranked elite admission (b5-design.md §2) won runPricingPortfolio's 4-leg
    // fold; false iff the ORIGINAL (v1, pre-B5a/HEAD 408a297) top-16-by-cheap-rank admission won.
    // Telemetry only (never read by scoring/caching logic) -- surfaced in main.cpp's stderr summary
    // as `admission=v1|v2`. Defaults false (matches SearchOptions.elite_admission_v2's default) for
    // any SearchResult not produced by runPricingPortfolio's own explicit fold-tracking -- exactly
    // the same precedent pricing_official above already set (runPricingPortfolio tracks which leg
    // won with its OWN local bookkeeping and stamps this field fresh at the end; it is never read
    // from any individual leg's own copy of the field).
    bool elite_admission_v2 = false;
    // B5a.2 (variant-registry portfolio + default-first budget mode, coordinator adjudication after
    // B5a.1's sweep report -- .superpowers/sdd/b5a-report.md "B5a.2 addendum") + B2 (seed-sweep
    // phase-2, .superpowers/sdd/b2-brief.md item 1, REPLACES B5a.2's original continued-SA phase 2):
    // true iff runPricingPortfolio's phase 2 actually executed at least one leg for this result --
    // i.e. remaining budget after phase 1 cleared the 1.5x-margin floor (PortfolioFold::admit /
    // shouldRunPhase2Leg below) for at least the cheapest phase-1-measured leg. False whenever
    // time_budget_sec is 0 (budget off) OR the requested budget could not clear even one leg's own
    // floor (the run degenerates to the phase-1/default result). Phase 2 is now a SEED-SWEEP
    // (independent restarts at options.seed+1000*round, run to full completion each) rather than
    // the pre-B2 continued-SA (same seed, each leg's OWN SA loop truncated early by a shortened
    // time_budget_sec) -- see runPricingPortfolio's own doc comment for the full B2 rationale
    // (T5/Cicirello: independent restarts >= continued chains). Telemetry only -- surfaced in
    // main.cpp's stderr summary as `budget_phase2=yes|no`. Defaults false for any SearchResult not
    // produced by runPricingPortfolio.
    bool budget_phase2 = false;
    // B5b (abutment-credit registry variant, .superpowers/sdd/b5-design.md §3, coordinator
    // adjudication applying B5a's own flag-gated-variant lesson): true iff the WINNING leg of
    // runPricingPortfolio's fold carried Problem.abutment_credit=true (model.hpp). Telemetry only
    // (never read by scoring/caching logic) -- surfaced in main.cpp's stderr summary as
    // `credit=on|off`, immediately after `admission=`. Defaults false (matches
    // SearchVariant.abutment_credit's floor-leg default) for any SearchResult not produced by
    // runPricingPortfolio's own explicit fold-tracking -- same precedent pricing_official/
    // elite_admission_v2/budget_phase2 above already established.
    bool abutment_credit = false;
    // B5c (move-portfolio registry variant, .superpowers/sdd/b5-design.md §4, coordinator
    // adjudication applying B5a's own flag-gated-variant lesson): true iff the WINNING leg of
    // runPricingPortfolio's fold carried SearchOptions.move_portfolio_v2=true (i.e. its engine(s)
    // were constructed with the wire-median/abutment/greedy-reposition moves enabled). Telemetry
    // only (never read by scoring/caching logic) -- surfaced in main.cpp's stderr summary as
    // `moves=v2|v1`, immediately after `credit=`. Defaults false (matches
    // SearchVariant.move_portfolio_v2's floor-leg default) for any SearchResult not produced by
    // runPricingPortfolio's own explicit fold-tracking -- same precedent pricing_official/
    // elite_admission_v2/abutment_credit above already established.
    bool move_portfolio_v2 = false;
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
    // B5a FIELD-SEMANTICS CHANGE (.superpowers/sdd/b5-design.md §2.5 point 4): `top_candidates`
    // widened from K=16 to K=48 (kTopCheapCandidates, search.cpp), so this vector -- and therefore
    // main.cpp's pool_tau/exact_best_cheap_rank stderr fields -- now describe the 48-member CHEAP
    // prefilter's own rank fidelity, not literally "the pool admitted to full elite treatment"
    // anymore: admission to full treatment is a SEPARATE downstream re-rank (buildEliteFullSet below)
    // by tight-exact value, not by this vector's cheap order. elite_pairs/pool_tau/
    // exact_best_cheap_rank remain a diagnostic on the cheap proxy's own fidelity at pool scale; they
    // do not describe which candidates actually received the expensive sweep/compact/respace
    // treatment (see buildEliteFullSet's own doc comment for that set).
    std::vector<std::pair<double, double>> elite_pairs;
    // B2 (seed-sweep budget phase, .superpowers/sdd/b2-brief.md item 5): the SearchOptions.seed
    // value actually used by the leg that won runPricingPortfolio's fold -- `options.seed` itself
    // for a phase-1 leg, or `options.seed + 1000*round` for a phase-2 seed-sweep leg (item 1,
    // PortfolioFold::admit below). Telemetry only (never read by scoring/caching logic) --
    // surfaced in main.cpp's stderr summary as `seed=<s>`, immediately after `budget_phase2=`.
    // Defaults to 0 for any SearchResult not produced by runPricingPortfolio's own explicit
    // fold-tracking -- same precedent pricing_official/budget_phase2/etc. above already
    // established (a raw runPlacementSearch/runHybridPlacementSearch call never stamps this field).
    int picked_seed = 0;
    // B2 (seed-sweep budget phase, .superpowers/sdd/b2-brief.md item 5): total number of portfolio
    // legs actually executed and folded -- `defaultVariants().size()` (10) for phase 1 alone (budget
    // off, or budget on but phase 2 never clears even its cheapest leg's floor), plus however many
    // additional seed-sweep legs phase 2 ran (item 1) when it does fire. Telemetry only --
    // surfaced in main.cpp's stderr summary as `legs_run=<n>`, immediately after `seed=`. Defaults
    // to 1 for any SearchResult not produced by runPricingPortfolio (a raw single-engine call is
    // exactly one "leg" of search by this field's own definition).
    int legs_run = 1;
    // P0a (phase-1 budget-bounded leg launching, docs/research/2026-08-14-final-levers-verdicts-and-
    // 0811-intel.md §3.2/§5 item 1): number of defaultVariants() registry legs actually LAUNCHED
    // during runPricingPortfolio's phase 1 alone (out of defaultVariants().size()) -- always the full
    // registry size when the budget is off, or when it's on but phase 1 finishes within it; less than
    // that when phase 1's own per-leg wall-clock gate (kPhase2LegMargin, search.cpp) stops launching
    // further legs before the registry completes (case5's 828.8s-vs-900s overshoot, §3.2). Leg 0 always
    // runs. When this is less than defaultVariants().size(), phase 2 never runs (gated on
    // phase1_complete, search.cpp) regardless of remaining budget. Telemetry only -- surfaced in
    // main.cpp's stderr summary as `phase1_legs=<n>/<total>`, immediately after `legs_run=`. Defaults
    // to 0 for any SearchResult not produced by runPricingPortfolio -- same "not produced by
    // runPricingPortfolio" precedent legs_run/picked_seed/etc. above already established, except this
    // field's default is 0 (not 1) since "phase 1 of a registry" isn't a meaningful concept for a raw
    // single-engine call.
    int phase1_legs_run = 0;
    // L3c (deep mode, case5-design.md §4-L3c step 1, .superpowers/sdd/case5-l3bc-brief.md): wall
    // seconds the SA loop itself consumed (mutate/decode/cheapScore/periodic-exact), EXCLUDING the
    // seed exact eval before it and all post-SA machinery (elite sweeps, finalize, B7 rungs) after
    // it. Stamped unconditionally by runPlacementSearch (budget on or off -- a clock read is
    // invisible in every output byte) and SUMMED across sub-runs by runHybridPlacementSearch (like
    // iterations), so a portfolio leg's value covers all its sub-runs' SA loops. Telemetry + deep-
    // mode input: runPricingPortfolio reads leg 0's value as S0 to derive the machinery share
    // M0 = T0 - S0 that sizes deep legs. Defaults 0 for any result not produced by those runners.
    double sa_wall_time_sec = 0.0;
    // L3c (deep mode, case5-design.md §4-L3c step 3, .superpowers/sdd/case5-l3bc-brief.md): true iff
    // runPricingPortfolio's deep-mode trigger fired -- budget on AND leg 0's measured wall proves the
    // full registry cannot fit the budget (variants.size() * T0 > time_budget_sec) -- so phase 1
    // stopped after leg 0 and the freed budget went to deep_legs_run deepVariants() legs instead
    // (each iters=2e6, per-leg time_budget_sec sized from M0, overflow_gated_pads forced). Both
    // telemetry only -- surfaced in main.cpp's stderr summary as `deep=<0/1> deep_legs=<n>` after
    // `phase1_legs=`; never read by scoring/caching logic. Budget-off runs can never set these (the
    // trigger's first operand short-circuits), which is exactly what the L3bc regression suite pins.
    bool deep_mode = false;
    int deep_legs_run = 0;
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

// B5a (elite-pool rework, .superpowers/sdd/b5-design.md §2): a candidate placement considered for
// the elite pass's cheap-value prefilter (runPlacementSearch's `top_candidates`) or exact-value
// archive (`exact_archive`) -- `value` is whichever score the owning pool ranks by (cheap for the
// prefilter, exact for the archive), `key` is its `placementKey` (dedup identity). Hoisted here (was
// local to runPlacementSearch) so `rememberBounded`/`buildEliteFullSet` below, and their unit tests
// (test/regression_b5_20260724.cpp), can share the shape -- the design doc itself calls the archive
// "CheapCandidate-shaped".
struct CheapCandidate {
    double value;
    Placement placement;
    std::string key;
};

// B5a: inserts `placement` (scored at `value`) into `pool` unless a placementKey-identical entry is
// already present, then keeps `pool` sorted ascending by `.value` and capped at `cap` entries. The
// exact dedup+sort+resize discipline the cheap-value prefilter used before B5a, now shared with the
// exact-value archive B5a adds -- both are "keep the best `cap` distinct candidates seen so far"
// pools, differing only in which value they rank by. A key already present short-circuits the whole
// insert (first-seen value for that key wins; matches the pre-B5a `rememberCandidate` semantics this
// generalizes).
void rememberBounded(std::vector<CheapCandidate>& pool, const Placement& placement, double value,
                     std::size_t cap);

// B5a (elite-pool rework, b5-design.md §2.2 "Insurance picks"): one step of classic reservoir
// sampling (Algorithm R) over a stream of decoded candidates. `seen` is the 1-based index of
// `placement` in that stream (the caller increments its own counter once per call); `insurance`
// grows to `cap` entries then randomly replaces a uniformly-chosen existing entry with diminishing
// probability `cap/seen`, giving every stream element equal probability `cap/n` of surviving in the
// final `cap`-sized sample after `n` calls. Takes `rng` as an explicit parameter (never a
// captured/global generator) so a caller can pass a DEDICATED generator and structurally guarantee
// this can never read the SA loop's own main mutate/accept rng stream.
void reservoirSample(std::vector<Placement>& insurance, const Placement& placement, int seen,
                     std::size_t cap, std::mt19937& rng);

// B5a (elite-pool rework, b5-design.md §2): one placement admitted to the elite pass's expensive
// full treatment (outlineSweep raw + compacted + maybeAdoptRespace) -- `tight_value`/`open_routes`
// are its rung-0 tight-outline exact score, set by buildEliteFullSet below.
struct EliteCandidate {
    Placement placement;
    std::string key;
    double tight_value = 0.0;
    int open_routes = 0;
};

// B5a (elite-pool rework, b5-design.md §2.1): builds the admission set for the elite pass's
// expensive full treatment, replacing plain "top-K by cheap value" admission (B6 measured cheap
// rank to be a weak-to-inverted predictor of the post-sweep exact winner -- pool_tau negative on
// case1/case3, exact_best_cheap_rank 16/16 on case3 default). Unions the cheap-value prefilter
// (`cheap_pool`), the exact-value archive (`exact_archive`), the SA loop's running best
// (`best_placement`, always FORCE-admitted), and the insurance reservoir (`insurance`, likewise
// always force-admitted) -- deduped by placementKey. Re-ranks the deduped union by a RUNG-0
// TIGHT-EXACT score (one real `exactCached` call per candidate; reuses whatever memoized cache the
// caller's `exactCached` closure already carries, so a candidate already exact-scored by the
// caller -- e.g. every `cheap_pool` member, via the caller's own elite-pool snapshot loop -- costs
// nothing here), legality-first (open_routes==0 strictly preferred) then ascending tight value.
// Returns the top `full_size` re-ranked candidates UNIONED with the force-admitted best+insurance,
// so the returned vector can hold up to `full_size + 1 + insurance.size()` entries after dedup, not
// exactly `full_size` -- the caller applies the identical elite treatment to every returned member
// regardless of which admission path brought it in.
std::vector<EliteCandidate> buildEliteFullSet(const std::vector<CheapCandidate>& cheap_pool,
                                              const std::vector<CheapCandidate>& exact_archive,
                                              const Placement& best_placement,
                                              const std::vector<Placement>& insurance,
                                              std::size_t full_size, const ExactCachedFn& exactCached);

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
// L4b (shape-aware shelf seed, case5-design.md §4-L4b): optional `seed_placement` -- when non-null,
// the sp sub-run starts from SequencePairEngine::seedFromPlacement(*seed_placement) with the seed's
// soft aspects stamped via stampSoftAspectFromPlacement (placement_engine.hpp) instead of the
// random initial state. nullptr (the default, every pre-L4b caller) is byte-identical to the
// historical two-arg behavior. Only deep-mode shelf legs pass a seed today.
SearchResult runHybridPlacementSearch(const Problem& problem, const SearchOptions& options,
                                      const Placement* seed_placement = nullptr);
// L3b (cost-aware repair budget, case5-design.md §4-L3b, .superpowers/sdd/case5-l3bc-brief.md):
// `repair_attempts` bounds the inflated-topology jiggle loop (was the local constexpr
// kFinalizeRepairAttempts = 32). The default IS that historical constant, so every pre-L3b caller
// (tests included) and every budget-off call is byte-identical to before the parameter existed;
// only runPlacementSearch's budget-on path passes a governed value (clamped [4,32]).
void finalizeFeedthrough(const Problem& problem, const PlacementEngine& engine, SearchResult& result,
                         int repair_attempts = 32);
// B5a.2 (variant-registry portfolio, coordinator adjudication after B5a.1's sweep report --
// .superpowers/sdd/b5a-report.md "B5a.2 addendum"): one point in the portfolio's search space --
// which router pricing (B4.1) and which elite-admission mechanism (B5a.1) a leg runs under. Room to
// grow: a future dimension (e.g. a seed variant for B2's multi-restart portfolio) is another field
// here, not a new hardcoded leg.
struct SearchVariant {
    bool pricing_official;
    bool elite_admission_v2;
    // B5b (abutment-credit cheap-score upgrade, .superpowers/sdd/b5-design.md §3, coordinator
    // adjudication applying B5a's own flag-gated-variant lesson -- .superpowers/sdd/b5a-report.md):
    // whether this leg's Problem copy carries abutment_credit=true (model.hpp). The pre-existing 4
    // legs (defaultVariants()) keep this false -- their behavior must stay bit-exact with pre-B5b
    // history; the credit is exercised only by the 2 legs B5b appends.
    bool abutment_credit;
    // B5c (move-portfolio registry variant, .superpowers/sdd/b5-design.md §4, same flag-gated-
    // variant lesson): whether this leg's engine(s) are constructed with
    // makePlacementEngine(name, /*move_portfolio_v2=*/true) -- i.e. whether SearchOptions.
    // move_portfolio_v2 is threaded through for this leg (see runLeg below). The pre-existing 6 legs
    // (defaultVariants()) keep this false -- bit-exact with pre-B5c history; the new moves are
    // exercised only by the 2 legs B5c appends.
    bool move_portfolio_v2;
    // F-FT (docs/research/2026-07-28-alpha-v5-cost-recalculation-analysis.md §6, Codex finding #1,
    // same flag-gated-variant lesson B5b/B5c applied): whether this leg's Problem copy carries
    // ft_pricing_aggressive=true (model.hpp) -- i.e. whether routing.cpp's official-pricing branch
    // prices FT hops at the exact marginal of the V5 AGGREGATE penalty instead of the per-block
    // mismatch delta. The pre-existing 8 legs (defaultVariants()) keep this false -- bit-exact with
    // pre-rung history; only exercised by the 2 legs this rung appends. Conservative (false) pricing
    // is retained as the floor per the Q4' outline_after_ft ambiguity (qa-draft Q4') -- the
    // aggregate's exact semantics are not fully pinned, so a leg that never turns this on stays
    // available as the safety net.
    bool ft_pricing_aggressive;
};

// The CURRENT portfolio's 10 legs, in the CURRENT leg order (B5a.1/B5a.2: (official,v1,credit-off),
// (legacy,v1,credit-off) -- the floor pair -- then (official,v2,credit-off), (legacy,v2,credit-off);
// B5b appends (official,v2,credit-on), (official,v1,credit-on); B5c appends
// (official,v2,credit-on,moves-on), (official,v1,credit-on,moves-on); F-FT (docs/research/2026-07-28-
// alpha-v5-cost-recalculation-analysis.md §6, Codex finding #1) appends
// (official,v2,credit-on,moves-on,ftagg-on), (official,v1,credit-on,moves-on,ftagg-on)). Extracted
// from runPricingPortfolio's former hardcoded 4-variable body so both the default (budget-off) and
// phase-1/phase-2 (budget-on) paths below iterate the SAME registry -- adding a variant later means
// editing this one function, not every call site that used to enumerate legs by hand. B5b's 2 legs,
// B5c's 2 legs, and F-FT's 2 legs are all OFFICIAL pricing only -- neither the credit, the move
// portfolio, nor the FT pricing mode is a router-selection change, so crossing any of them with the
// legacy-pricing leg too would double the registry again for no evidenced benefit; the existing
// (official,*)/(legacy,*) floor legs already hold the per-case Pareto floor every later dimension is
// folded against. B5c's 2 legs mirror B5b's own admission split (v2-admission first, then
// v1-admission), both credit-on (moves are designed to be rewarded by the abutment credit ramp,
// design §3.3/§8 sequencing rationale -- B5b before B5c). F-FT's 2 legs keep that SAME
// admission-v2-then-v1 order and stack on top of B5c's moves-on legs -- conservative (ftagg-off)
// pricing is retained as the floor across every other leg per the Q4' outline_after_ft ambiguity
// (qa-draft Q4').
std::vector<SearchVariant> defaultVariants();

// L3c (deep mode, case5-design.md §4-L3c step 4, .superpowers/sdd/case5-l3bc-brief.md): the SEPARATE
// 3-leg list deep mode draws from when the full registry provably cannot fit the budget --
// (official,v1,credit-off,moves-off,ftagg-ON), (official,v2,credit-off,moves-off,ftagg-ON),
// (official,v2,credit-off,moves-on,ftagg-off). The ftagg-ON entries are the mitigation for P0a's
// phase-1 gate cutting registry legs 8/9 (the only default ftagg legs) first on an expensive
// instance (design §3.3 item 2); credit stays off on all three (under --cheap-proxy-v2 the L1b
// boundary-gap WL proxy makes the credit inert/double-counting, and deep legs are exactly the legs
// a case5-shaped run would pair with that flag). NOT a reorder of defaultVariants() -- that registry
// is untouched (case5-l3bc-brief.md "out of scope").
std::vector<SearchVariant> deepVariants();

// L3c deep-mode arithmetic (case5-design.md §4-L3c steps 3-4), pure helpers hoisted out of
// runPricingPortfolio so they are directly unit-testable with case5-shaped numbers (same precedent
// as shouldRunPhase2Leg above). All deterministic functions of their arguments.
// Trigger: budget on AND leg 0's measured wall T0 proves the registry cannot fit TWICE within the
// budget (kDeepTriggerFitFactor = 2.0, search.cpp -- L3c.1's M3-measured loosening of the design's
// original fits-once rule; see the constant's own doc comment for the measured middle-regime
// pathology it removes).
bool deepModeTrigger(double time_budget_sec, std::size_t registry_size, double t0);
// N_deep = clamp(floor((budget - T0) / (kPhase2LegMargin * M0)), 1, 3); M0 <= 0 (machinery
// unmeasurably small) takes the optimistic cap 3 -- the per-leg slice recheck still guards.
int deepLegCount(double time_budget_sec, double t0, double m0);
// Per-leg slice: (remaining budget) / (deep legs still to run) - kPhase2LegMargin * M0, floored at
// 0 -- arithmetically the design's (budget - elapsed)/N_deep - margin for the first leg, and
// self-correcting for later legs when an earlier one under/overran its slice.
double deepLegSlice(double time_budget_sec, double elapsed_sec, int legs_left, double m0);

// B2 (seed-sweep budget phase + anytime output, .superpowers/sdd/b2-brief.md items 1-2): callback
// invoked by PortfolioFold::admit below whenever a newly folded-in leg becomes the running
// fold-best -- runPricingPortfolio's caller (main.cpp) passes one that writes the current best cfg
// to the output path (atomic: temp+rename, io.cpp's writeCfg), so a killed/overrun run always
// leaves a valid best-so-far cfg on disk (2023-D pattern, A42 aggregate-2h safety -- the contest's
// "8 cases / 2h TOTAL" can kill a run regardless of whether that invocation's own --time-budget was
// set). Default-constructed (empty/null `std::function`) means "no anytime writing"; every existing
// direct caller of runPricingPortfolio that does not pass one sees no behavior change at all.
using AnytimeWriteFn = std::function<void(const Problem&, const Placement&, const RoutingResult&)>;

// B2: incremental value-gated fold over a stream of portfolio legs, extracted out of
// runPricingPortfolio's body so the "write on improvement" logic is directly unit-testable
// (test/regression_b2_20260725.cpp) without running a real 8-leg search -- same "hoist the
// primitive" precedent as paydownBetter/outlineSweepBetter/rememberBounded/buildEliteFullSet above.
// `admit` folds ONE leg's result in via the SAME value-gated `paydownBetter` runPricingPortfolio
// used pre-B2 (legality-first, then value; a transitive total preorder, so streaming admission one
// leg at a time yields the identical winner a "fold the whole vector at once" pass would), updates
// the running iteration/exact-eval totals and `legs_run`, and returns true iff this leg became the
// new running best -- in which case, when `on_improved` is non-null, it is invoked with the new
// best (problem, placement, routing) BEFORE returning. The very first `admit` call always returns
// true (empty fold has no incumbent to lose to), so the first leg of phase 1 always triggers the
// first anytime write -- exactly the brief's "first write after the first leg result, no separate
// pre-phase-1 write" rule, with no special-casing needed.
struct PortfolioFold {
    SearchResult result;
    SearchVariant winner{};
    int picked_seed = 0;
    int legs_run = 0;
    int total_iterations = 0;
    int total_exact_evaluations = 0;
    // L3c (case5-l3bc-brief.md M1-completion): summed sa_wall_time_sec across ALL admitted legs
    // (accumulated like total_iterations above), stamped into the portfolio result at the end so
    // main.cpp's `sa_wall=` field reports the whole run's SA-vs-machinery split, not just the
    // picked leg's.
    double total_sa_wall_sec = 0.0;
    bool have_result = false;

    bool admit(const Problem& problem, const SearchResult& leg_result, const SearchVariant& variant, int leg_seed,
               const AnytimeWriteFn& on_improved);
};

// B2 (seed-sweep budget phase, .superpowers/sdd/b2-brief.md item 1): true iff a phase-2 seed-sweep
// leg whose PHASE-1 measurement (same variant, options.seed) took `leg_phase1_wall_time_sec` should
// run, given `remaining_sec` wall-clock seconds left in the overall budget. The brief's own rule:
// skip when `remaining < 1.5 * leg_phase1_wall_time_sec` -- a fresh seed's actual runtime is not
// identical to phase 1's measurement of the SAME leg (different SA trajectory -> different elite-
// pool/sweep/respace costs), so the 1.5x margin is safety headroom against that variance, not a
// tight bound. Hoisted as a free, pure function (not inlined into runPricingPortfolio's loop) so
// the skip arithmetic itself is directly unit-testable. Equality (`remaining == 1.5*leg_time`)
// runs the leg (only strictly-less skips), matching the brief's own "<" wording.
bool shouldRunPhase2Leg(double remaining_sec, double leg_phase1_wall_time_sec);

// B4.1 (route-pricing portfolio, b4-report.md) + B5a.1 (elite-admission dimension,
// b5a-report.md) + B5a.2 (variant registry + default-first budget mode, b5a-report.md "B5a.2
// addendum") + B5b (abutment-credit registry variant, b5-design.md §3) + B5c (move-portfolio
// registry variant, b5-design.md §4) + B2 (seed-sweep phase 2 + anytime output,
// .superpowers/sdd/b2-brief.md): runs the FULL requested engine search ("sp", "bstar", or
// "hybrid" -- same dispatch main.cpp used to do inline) over `defaultVariants()` (currently 10 legs --
// see defaultVariants()'s own doc comment for the exact leg list/order), folding every executed leg
// with the existing value-gated `paydownBetter` (legality-first, then value; a transitive total
// preorder, so fold order never changes the winner) via PortfolioFold::admit above, which also
// drives the optional `on_improved` anytime-write callback (B2 item 2).
//
// BUDGET-OFF (`options.time_budget_sec == 0`): runs the registry ONCE at `options.iters`,
// time-budget off -- byte-identical to pre-B2 HEAD (hard gate, re-verified: md5-identical .cfg on
// all 5 default cases -- B2 only ADDS the optional anytime-write side effect and per-leg timing
// instrumentation around the SAME phase-1 loop, changing no comparison/selection logic).
//
// BUDGET-ON (`options.time_budget_sec > 0`) stays DEFAULT-FIRST (B5a.2's structural gate closure,
// unchanged): **phase 1** runs the ENTIRE registry at `options.iters` with the time budget forced
// OFF -- i.e. phase 1 IS the default run, byte-identical to what budget-off computes -- timing EACH
// leg individually this time (`leg_wall_times`, B2) as well as the aggregate `t0`.
//
// **Phase 2 (B2, REPLACES B5a.2's continued-SA)** is now a SEED SWEEP: for round = 1, 2, 3, ...,
// try the FULL registry again at seed `options.seed + 1000*round` (far from the `+1` offset
// runHybridPlacementSearch already uses internally for its bstar leg, so a phase-2 seed can never
// collide with that) -- but BEFORE EACH LEG (not once per round), check `shouldRunPhase2Leg` against
// that leg's OWN phase-1-measured wall time; a leg that fails the check is skipped (not the whole
// round). A round in which not even ONE leg could run ends the sweep -- remaining budget only
// shrinks in later rounds, so no later round could do better either, which also proves the loop
// terminates without an artificial round cap. Every phase-2 leg that DOES run goes to full
// completion (`time_budget_sec` forced 0 for that call) -- a genuine independent restart, not a
// truncated continuation. Rationale for the replacement (motivating evidence in b2-brief.md): T5's
// seed-variance probe (beta-task-5-report.md) measured bstar up to 5.8x across seeds with all prior
// legs pinned to seed 1; Cicirello (SoCS 2017 / arXiv:1709.02877, docs/research/2026-07-07-external-
// techniques-survey.md) found periodic best-solution sharing across parallel SA chains has no
// statistically significant benefit over fully INDEPENDENT chains on a comparably-sized benchmark --
// together arguing that spending extra wall-clock on a fresh seed (which can land in a genuinely
// different basin) beats continuing one chain past its own natural stopping point (which only
// explores further around a trajectory already sampled). The B5a.2 measurement that motivated the
// original continued-SA (hybrid@45 Sigma -1.4%) is not disputed -- seed diversity is simply the
// better spend of the SAME wall-clock, not a claim that continued-SA found nothing.
//
// Every leg from BOTH phases that ran is folded by the same `paydownBetter` (via PortfolioFold) --
// since phase 1's own legs are ALWAYS in the folded set, the portfolio's result can never be worse
// than the plain default run, regardless of whether phase 2 fires or what it finds: budget's
// contract stays "never worse than default, spend any surplus wall-clock exploring further."
// `SearchResult.budget_phase2` reports whether phase 2 ran at least one leg; `SearchResult.
// legs_run`/`picked_seed` (B2 item 5) report the total leg count and the winning leg's own seed.
//
// `pricing_official`/`elite_admission_v2`/`abutment_credit`/`move_portfolio_v2` on the returned
// result are stamped FRESH from the WINNING variant (never read from any leg's own copy of those
// fields -- same precedent B4.1/B5a.1 established). Existing direct callers of runPlacementSearch/
// runHybridPlacementSearch keep single-leg semantics unchanged (elite_admission_v2/move_portfolio_v2
// default false on SearchOptions; abutment_credit defaults false on Problem).
SearchResult runPricingPortfolio(const Problem& problem, const std::string& engine, const SearchOptions& options,
                                 const AnytimeWriteFn& on_improved = AnytimeWriteFn());

}  // namespace efgr
