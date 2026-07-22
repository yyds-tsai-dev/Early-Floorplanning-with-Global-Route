#include "search.hpp"

#include <chrono>
#include <cmath>
#include <random>
#include <sstream>
#include <unordered_map>
#include <utility>

#include "eval.hpp"
#include "ft.hpp"
#include "respace.hpp"
#include "validate.hpp"

namespace efgr {
namespace {

bool axisOverlap(double a0, double a1, double b0, double b1) {
    return std::min(a1, b1) - std::max(a0, b0) > kEps;
}

}  // namespace

Placement compactTowardOrigin(const Problem& problem, Placement placement) {
    std::vector<int> order;
    for (std::size_t i = 0; i < problem.blocks.size(); ++i) {
        if (!isEdge(problem.blocks[i].type)) order.push_back(static_cast<int>(i));
    }
    std::sort(order.begin(), order.end(), [&](int a, int b) {
        double ka = placement.rects[a].x + placement.rects[a].y;
        double kb = placement.rects[b].x + placement.rects[b].y;
        if (std::abs(ka - kb) > kEps) return ka < kb;
        return a < b;
    });

    for (int pass = 0; pass < 3; ++pass) {
        for (int idx : order) {
            Rect& r = placement.rects[idx];
            double x = 0.0;
            for (std::size_t j = 0; j < placement.rects.size(); ++j) {
                if (static_cast<int>(j) == idx) continue;
                const Rect& o = placement.rects[j];
                if (o.right() <= r.x + kEps && axisOverlap(r.y, r.top(), o.y, o.top())) {
                    x = std::max(x, o.right());
                }
            }
            r.x = std::min(std::max(0.0, x), std::max(0.0, problem.outline_w - r.w));

            double y = 0.0;
            for (std::size_t j = 0; j < placement.rects.size(); ++j) {
                if (static_cast<int>(j) == idx) continue;
                const Rect& o = placement.rects[j];
                if (o.top() <= r.y + kEps && axisOverlap(r.x, r.right(), o.x, o.right())) {
                    y = std::max(y, o.top());
                }
            }
            r.y = std::min(std::max(0.0, y), std::max(0.0, problem.outline_h - r.h));
        }
    }
    // T5 carry-over fix (p1-design.md §1.4, binding on this task): compaction moves core rects,
    // which invalidates any previously stamped shrink-wrap outline (it was computed for the
    // pre-compaction rects). Reset so exactScore's unset-path recomputes a fresh shrink-wrap
    // against the post-compaction geometry instead of scoring against a stale stamp.
    placement.outline_w = 0.0;
    placement.outline_h = 0.0;
    return placement;
}

namespace {

bool fitsOutlineWithoutOverlap(const Problem& problem, const Placement& placement) {
    for (std::size_t i = 0; i < placement.rects.size(); ++i) {
        const Rect& r = placement.rects[i];
        if (r.x < -kEps || r.y < -kEps || r.right() > problem.outline_w + kEps || r.top() > problem.outline_h + kEps) {
            return false;
        }
        for (std::size_t j = i + 1; j < placement.rects.size(); ++j) {
            if (overlaps(r, placement.rects[j])) return false;
        }
    }
    return true;
}

}  // namespace

std::string placementKey(const Placement& placement) {
    std::ostringstream out;
    for (const auto& r : placement.rects) {
        out << std::llround(r.x * 1000.0) << ','
            << std::llround(r.y * 1000.0) << ','
            << std::llround(r.w * 1000.0) << ','
            << std::llround(r.h * 1000.0) << ';';
    }
    out << ':' << std::llround(placement.outline_w * 1000.0) << 'x'
        << std::llround(placement.outline_h * 1000.0);
    return out.str();
}

bool paydownBetter(const RoutingResult& a, double a_value, const RoutingResult& b, double b_value) {
    if (a.open_routes != b.open_routes) return a.open_routes < b.open_routes;
    if (a.feedthrough_mismatch < b.feedthrough_mismatch - kEps) return true;
    if (b.feedthrough_mismatch < a.feedthrough_mismatch - kEps) return false;
    return a_value < b_value;
}

bool acceptFeedthroughPaydown(const RoutingResult& candidate, double candidate_value,
                              const RoutingResult& best, double best_value) {
    if (candidate.open_routes != 0) return false;
    if (candidate.feedthrough_mismatch < best.feedthrough_mismatch - kEps) return true;
    return candidate.feedthrough_mismatch <= best.feedthrough_mismatch + kEps && candidate_value < best_value;
}

namespace {

// Elite-pass outline sweep preference (design p1-design.md §1.4 elite path): open_routes==0 is
// strictly preferred over any nonzero count (legality first, per the same discipline as
// acceptFeedthroughPaydown/paydownBetter above); among equally-legal candidates, lower value wins.
bool outlineSweepBetter(const RoutingResult& a, double a_value, const RoutingResult& b, double b_value) {
    if ((a.open_routes == 0) != (b.open_routes == 0)) return a.open_routes == 0;
    return a_value < b_value;
}

// T5 (design p3-design.md §5, plan Task 5): respaces the elite pass's then-current best against
// its own routed channel demand and adopts the result under the SAME preference rule the sweep
// adoptions above already use (`outlineSweepBetter`: open_routes==0 strictly first, then lower
// value). Called once per elite candidate, on `result.best_*` AFTER both of that candidate's
// sweep adoptions (raw + compacted) have already run -- never on the swept candidate itself, so a
// losing sweep variant never pays for an extra respaceFixedPoint call, and each elite contributes
// at most one respace exact-eval batch. `respaceFixedPoint` is itself Pareto-safe (never returns
// worse than its own `base` per its documented contract), so this can only match or improve
// `result`, never regress it.
void maybeAdoptRespace(const Problem& problem, const ExactCachedFn& exactCached, SearchResult& result) {
    RespaceResult respaced = respaceFixedPoint(problem, result.best_placement, exactCached);
    if (outlineSweepBetter(respaced.routing, respaced.score.value, result.best_routing, result.best_score.value)) {
        result.best_score = respaced.score;
        result.best_placement = respaced.placement;
        result.best_routing = respaced.routing;
    }
}

}  // namespace

OutlineSweepResult outlineSweep(const Problem& problem, const Placement& base, const ExactCachedFn& exactCached) {
    // Deterministic margin ladder (fractions of MAX-tight added to both dims), fixed per the task
    // brief -- no RNG. m=0.0 is the tight shrink-wrap itself (today's single-eval behavior), so
    // this sweep is strictly Pareto-safe: it can only match or beat the pre-sweep baseline.
    // T8.1: extended with 0.50/1.0 rungs so the ladder can recover MAX-like outlines on
    // overflow-bound cases, where tight outlines carry huge channel_overflow penalties that the
    // 0.02-0.20 rungs alone cannot climb back out of. The existing duplicate-target skip below
    // handles tight==MAX (all extra rungs collapse to a no-op there).
    static constexpr double kMargins[] = {0.0, 0.02, 0.05, 0.10, 0.20, 0.50, 1.0};

    Placement tight = base;
    tight.outline_w = 0.0;
    tight.outline_h = 0.0;
    OutlineFit tight_fit = shrinkWrapOutline(problem, tight);
    double tight_w = tight_fit.legal ? tight_fit.w : problem.outline_w;
    double tight_h = tight_fit.legal ? tight_fit.h : problem.outline_h;

    OutlineSweepResult best;
    bool have_best = false;
    double last_target_w = -1.0;
    double last_target_h = -1.0;
    for (double margin : kMargins) {
        // Part D (T5 integer-leak rider): anchorAtOutline preserves the caller's EXACT target
        // (T7.1 contract), so a fractional margin-rung target would leak into
        // result.best_placement's outline stamp AND its re-anchored edge x/y. MAX
        // (problem.outline_w/h) is integer, so ceil-then-clamp keeps every rung's target integral
        // and <= MAX. m=0.0 targets tight_w/h, already integer from shrinkWrapOutline's rule-5
        // ceiling, so this is a no-op there -- the strictly-Pareto-safe m=0.0 rung is unchanged.
        double target_w =
            std::min(problem.outline_w, std::ceil(tight_w + margin * (problem.outline_w - tight_w) - kEps));
        double target_h =
            std::min(problem.outline_h, std::ceil(tight_h + margin * (problem.outline_h - tight_h) - kEps));
        // Skip a duplicate rung (e.g. tight == MAX already, so every larger margin's target is
        // identical to m=0.0's) -- avoids a redundant exact-score call; the cache would dedupe it
        // anyway via placementKey, but skipping keeps the sweep's trial count honest.
        if (have_best && std::abs(target_w - last_target_w) < kEps && std::abs(target_h - last_target_h) < kEps) {
            continue;
        }
        last_target_w = target_w;
        last_target_h = target_h;

        Placement variant = base;
        variant.outline_w = 0.0;
        variant.outline_h = 0.0;
        Problem ceiling = problem;
        ceiling.outline_w = target_w;
        ceiling.outline_h = target_h;
        // T7.1 bugfix: shrinkWrapOutline is a minimizer -- it bisects back DOWN to the tight legal
        // frontier regardless of the ceiling it is given, so every margin>0 rung used to silently
        // collapse back to ~tight (dead sweep). m=0.0 IS the tight rung, so it alone still uses
        // shrinkWrapOutline; every larger rung anchors directly at its exact target via
        // anchorAtOutline (no bisection), re-anchoring edge blocks at that target's border.
        // Targets >= tight are feasible by upward-closedness (shrinkWrapOutline already proved
        // tight itself legal), so legal=false should not happen here -- skip the rung defensively
        // rather than risk stamping/using an illegal variant.
        bool anchored;
        if (margin == 0.0) {
            OutlineFit tight_variant_fit = shrinkWrapOutline(ceiling, variant);
            anchored = tight_variant_fit.legal;
        } else {
            OutlineFit fit = anchorAtOutline(ceiling, variant);
            anchored = fit.legal;
        }
        if (!anchored) continue;

        RoutingResult routing;
        Score score = exactCached(variant, &routing);
        if (!have_best || outlineSweepBetter(routing, score.value, best.routing, best.score.value)) {
            best = OutlineSweepResult{score, variant, routing};
            have_best = true;
        }
    }

    // P1 final-review fix: if every rung above failed its `anchored` check (all `continue`d), the
    // loop never assigns `best`, and a default-constructed OutlineSweepResult{} would be returned
    // instead -- an empty placement (no rects) with score.value == 0.0 (its default) and
    // routing.open_routes == 0 (its default), which outlineSweepBetter can then PREFER over a
    // legitimate incumbent with open_routes>0, silently committing an empty placement. Guarantee a
    // real fallback: base's own outline reset + shrinkWrapOutline, which the MAX-fallback contract
    // (shrinkWrapOutline's doc comment above) guarantees is always legal.
    if (!have_best) {
        Placement fallback = base;
        fallback.outline_w = 0.0;
        fallback.outline_h = 0.0;
        shrinkWrapOutline(problem, fallback);
        RoutingResult routing;
        Score score = exactCached(fallback, &routing);
        best = OutlineSweepResult{score, fallback, routing};
        have_best = true;
    }
    return best;
}

void finalizeFeedthrough(const Problem& problem, const PlacementEngine& engine, SearchResult& result) {
    if (result.best_routing.feedthrough_mismatch <= kEps) return;
    Problem inflated = problem;
    Placement placement = result.best_placement;
    RoutingResult routing = result.best_routing;
    std::mt19937 rng(0x9e3779b9u);
    // Commit gate: mismatch paydown first, but never at the price of a solution main.cpp
    // would report as invalid (the corrected decode can spill blocks past MAX).
    auto tryCommit = [&](const Placement& candidate, const Score& score, const RoutingResult& cand_routing) {
        if (!acceptFeedthroughPaydown(cand_routing, score.value, result.best_routing, result.best_score.value)) return;
        if (!validateSolution(problem, candidate, cand_routing).ok) return;
        result.best_score = score;
        result.best_placement = candidate;
        result.best_routing = cand_routing;
    };
    for (int pass = 0; pass < 3; ++pass) {
        bool grew = false;
        for (std::size_t i = 0; i < inflated.blocks.size(); ++i) {
            if (!isSoft(inflated.blocks[i].type)) continue;
            double nets = i < routing.soft_feedthrough_nets.size() ? routing.soft_feedthrough_nets[i] : 0.0;
            if (nets <= 0.0) continue;
            double base = problem.blocks[i].ft_base_area > 0.0 ? problem.blocks[i].ft_base_area : problem.blocks[i].area;
            double required = ftRequiredArea(base, ftExtend(nets, ftTierRate(problem.blocks[i], nets)));
            if (required > inflated.blocks[i].area + kEps) {
                inflated.blocks[i].area = required;
                grew = true;
            }
        }
        if (!grew) break;
        PlacementState state = engine.seedFromPlacement(inflated, placement, rng);
        placement = engine.decode(inflated, state);
        // Defensive symmetry with the compacted path below: decode stamps the declared outline
        // today, but future engines may not -- guard against committing an unstamped placement.
        if (placement.outline_w <= 0.0 || placement.outline_h <= 0.0) {
            shrinkWrapOutline(inflated, placement);
        }
        RoutingResult new_routing;
        Score exact = exactScore(inflated, placement, &new_routing);
        routing = new_routing;
        // T6 (design p3-design.md §3.4/§5, plan Task 6): respace the inflated re-decode against
        // its own routed channel demand BEFORE tryCommit -- FT inflation grows a soft block in
        // place, which can push a neighboring strip into overflow; respacing here widens exactly
        // that strip on the SAME inflated problem the candidate was decoded against. The commit
        // gate itself is untouched: tryCommit still scores/validates against the ORIGINAL
        // `problem` (not `inflated`), so respacing only ever supplies a possibly-better candidate
        // to the same paydown + validateSolution funnel, never changes what gets accepted.
        // Memoized the same way runPlacementSearch's `exactCached` is (placementKey-keyed cache):
        // respaceFixedPoint's first internal call scores `placement` itself, which is the exact
        // same candidate `tryCommit(placement, exact, new_routing)` a few lines below already
        // scored via `exact` above -- without a cache that duplicates a real exactScore/routeProblem
        // call per pass per variant (this function calls respaceFixedPoint up to twice per pass,
        // across up to 3 passes).
        std::unordered_map<std::string, std::pair<Score, RoutingResult>> inflated_exact_cache;
        ExactCachedFn inflatedExact = [&](const Placement& cand, RoutingResult* routing_out) {
            std::string key = placementKey(cand);
            auto it = inflated_exact_cache.find(key);
            if (it != inflated_exact_cache.end()) {
                if (routing_out) *routing_out = it->second.second;
                return it->second.first;
            }
            RoutingResult routing;
            Score score = exactScore(inflated, cand, &routing);
            inflated_exact_cache.emplace(std::move(key), std::make_pair(score, routing));
            if (routing_out) *routing_out = routing;
            return score;
        };
        RespaceResult respaced = respaceFixedPoint(inflated, placement, inflatedExact);
        tryCommit(placement, exact, new_routing);
        tryCommit(respaced.placement, respaced.score, respaced.routing);
        // Reclaim outline/overflow lost to the inflated re-decode: same dims, tighter packing.
        Placement compacted = compactTowardOrigin(inflated, placement);
        if (fitsOutlineWithoutOverlap(inflated, compacted)) {
            // compactTowardOrigin resets outline_w/h to 0 (moved core rects invalidate any prior
            // stamp). Re-stamp immediately so `compacted`, once committed as best_placement, is
            // internally consistent with its OWN rects/outline for later validateSolution/writeCfg
            // calls -- exactScore below would otherwise recompute the shrink-wrap on an internal
            // copy for scoring only, leaving this stored `compacted` stale (edges not re-anchored,
            // stamp 0 -> falls back to MAX) even though it was scored/routed at a smaller outline.
            shrinkWrapOutline(inflated, compacted);
            RoutingResult compacted_routing;
            Score compacted_exact = exactScore(inflated, compacted, &compacted_routing);
            tryCommit(compacted, compacted_exact, compacted_routing);
            // Same T6 respace, applied to the compacted variant before ITS tryCommit.
            RespaceResult compacted_respaced = respaceFixedPoint(inflated, compacted, inflatedExact);
            tryCommit(compacted_respaced.placement, compacted_respaced.score, compacted_respaced.routing);
        }
        // A single re-decode of the inflated problem often spills past MAX (a legal
        // arrangement may still exist -- other engines find one). Jiggle the inflated
        // topology a bounded number of times; only geometrically legal candidates pay
        // for exact routing, and all commits go through the same paydown gate.
        constexpr int kFinalizeRepairAttempts = 32;
        PlacementState repair = state;
        for (int attempt = 0; attempt < kFinalizeRepairAttempts; ++attempt) {
            engine.mutate(inflated, repair, rng);
            if (!engine.isValidState(inflated, repair)) continue;
            Placement cand = engine.decode(inflated, repair);
            if (!fitsOutlineWithoutOverlap(inflated, cand)) continue;
            RoutingResult cand_routing;
            Score cand_exact = exactScore(inflated, cand, &cand_routing);
            tryCommit(cand, cand_exact, cand_routing);
        }
    }
}

SearchResult runPlacementSearch(const Problem& problem, const PlacementEngine& engine, const SearchOptions& options) {
    std::mt19937 rng(options.seed);
    PlacementState initial = engine.initialState(problem, rng);
    return runPlacementSearch(problem, engine, options, initial);
}

SearchResult runPlacementSearch(const Problem& problem, const PlacementEngine& engine, const SearchOptions& options,
                                const PlacementState& initial_state) {
    SearchResult result;
    result.engine_name = engine.name();
    result.iterations = std::max(0, options.iters);

    std::mt19937 rng(options.seed);
    PlacementState current = initial_state;
    if (!engine.isValidState(problem, current)) current = engine.initialState(problem, rng);
    Placement current_place = engine.decode(problem, current);
    Score current_score = cheapScore(problem, current_place);

    PlacementState best_state = current;
    result.best_placement = current_place;
    std::unordered_map<std::string, std::pair<Score, RoutingResult>> exact_cache;
    auto exactCached = [&](const Placement& placement, RoutingResult* routing_out) {
        std::string key = placementKey(placement);
        auto it = exact_cache.find(key);
        if (it != exact_cache.end()) {
            if (routing_out) *routing_out = it->second.second;
            return it->second.first;
        }
        RoutingResult routing;
        Score score = exactScore(problem, placement, &routing);
        exact_cache.emplace(std::move(key), std::make_pair(score, routing));
        ++result.exact_evaluations;
        if (routing_out) *routing_out = routing;
        return score;
    };
    result.best_score = exactCached(result.best_placement, &result.best_routing);

    struct CheapCandidate {
        double value;
        Placement placement;
        std::string key;
    };
    std::vector<CheapCandidate> top_candidates;
    auto rememberCandidate = [&](const Placement& placement, const Score& score) {
        std::string key = placementKey(placement);
        for (const auto& candidate : top_candidates) {
            if (candidate.key == key) return;
        }
        top_candidates.push_back(CheapCandidate{score.value, placement, key});
        std::sort(top_candidates.begin(), top_candidates.end(), [](const CheapCandidate& a, const CheapCandidate& b) {
            return a.value < b.value;
        });
        constexpr std::size_t kTopCheapCandidates = 16;
        if (top_candidates.size() > kTopCheapCandidates) top_candidates.resize(kTopCheapCandidates);
    };
    rememberCandidate(result.best_placement, current_score);

    double temp = std::max(1.0, options.initial_temperature);
    int exact_interval = std::max(1, options.exact_interval);
    // Beta Task 5: wall-clock budget. When enabled, time (not the iter default) is the binding
    // stop -- raise the iteration ceiling so the steady_clock check below is what ends the loop.
    // Budget off (== 0) leaves the bound and reported iteration count byte-identical to the
    // pre-budget path (result.iterations stays the value stamped from options.iters above).
    const int iter_bound = options.time_budget_sec > 0.0 ? std::max(options.iters, 2000000)
                                                         : std::max(0, options.iters);
    const auto budget_start = std::chrono::steady_clock::now();
    int iters_run = 0;
    for (int iter = 1; iter <= iter_bound; ++iter) {
        if (options.time_budget_sec > 0.0 && (iter & 63) == 0) {
            double elapsed =
                std::chrono::duration<double>(std::chrono::steady_clock::now() - budget_start).count();
            if (elapsed >= options.time_budget_sec) break;
        }
        iters_run = iter;
        PlacementState candidate = current;
        engine.mutate(problem, candidate, rng);
        if (!engine.isValidState(problem, candidate)) continue;

        Placement cand_place = engine.decode(problem, candidate);
        Score cand_score = cheapScore(problem, cand_place);
        rememberCandidate(cand_place, cand_score);
        double delta = cand_score.value - current_score.value;
        std::uniform_real_distribution<double> unit(0.0, 1.0);
        if (delta <= 0.0 || std::exp(-delta / std::max(1.0, temp)) > unit(rng)) {
            current = candidate;
            current_place = cand_place;
            current_score = cand_score;
        }

        if (iter % exact_interval == 0 || cand_score.value < result.best_score.value) {
            RoutingResult routing;
            Score exact = exactCached(cand_place, &routing);
            if (exact.value < result.best_score.value) {
                result.best_score = exact;
                best_state = candidate;
                result.best_placement = cand_place;
                result.best_routing = routing;
            }
        }
        temp *= options.cooling;
    }
    // Report the iterations actually executed when time bounded the run; budget off keeps the
    // options.iters value stamped above, preserving the existing --iters summary line verbatim.
    if (options.time_budget_sec > 0.0) result.iterations = iters_run;

    for (const auto& candidate : top_candidates) {
        // T7 (design §1.4 elite path): replaces the single tight-stamp exactCached call with a
        // deterministic outline-margin sweep -- m=0.0 reproduces the exact prior single-eval
        // behavior, so this can only match or beat it (strictly Pareto-safe). Preference mirrors
        // outlineSweep's own internal rule (open_routes==0 strictly first, then lower value) so
        // the elite loop never regresses legality chasing a lower score.
        OutlineSweepResult swept = outlineSweep(problem, candidate.placement, exactCached);
        if (outlineSweepBetter(swept.routing, swept.score.value, result.best_routing, result.best_score.value)) {
            result.best_score = swept.score;
            result.best_placement = swept.placement;
            result.best_routing = swept.routing;
        }

        Placement compacted = compactTowardOrigin(problem, candidate.placement);
        // compactTowardOrigin resets outline_w/h to 0 (moved core rects invalidate any prior
        // stamp) -- outlineSweep re-shrink-wraps from scratch per rung regardless, so no
        // additional restamp is needed here before sweeping (unlike the pre-T7 single-eval path,
        // which required an explicit restamp so exactCached's placementKey stayed accurate).
        OutlineSweepResult swept_compacted = outlineSweep(problem, compacted, exactCached);
        if (outlineSweepBetter(swept_compacted.routing, swept_compacted.score.value, result.best_routing,
                               result.best_score.value)) {
            result.best_score = swept_compacted.score;
            result.best_placement = swept_compacted.placement;
            result.best_routing = swept_compacted.routing;
        }

        // T5 (design p3-design.md §5, plan Task 5): respace the then-current best ONCE per
        // elite candidate, after both sweep adoptions above have already run their course --
        // never on the swept candidate directly, so a sweep variant that lost either adoption
        // never separately pays for its own respaceFixedPoint call.
        maybeAdoptRespace(problem, exactCached, result);
    }

    (void)best_state;
    (void)current_place;
    finalizeFeedthrough(problem, engine, result);
    return result;
}

SearchResult runHybridPlacementSearch(const Problem& problem, const SearchOptions& options) {
    std::unique_ptr<PlacementEngine> sp = makePlacementEngine("sp");
    std::unique_ptr<PlacementEngine> bstar = makePlacementEngine("bstar");

    SearchOptions sp_options = options;
    SearchOptions bstar_options = options;
    sp_options.iters = std::max(1, options.iters / 2);
    bstar_options.iters = std::max(1, options.iters - sp_options.iters);
    bstar_options.seed = options.seed + 1;
    // Beta Task 5: split any wall-clock budget evenly across the two legs (the copies already carry
    // options.time_budget_sec; halve it here). Budget off (0) halves to 0 -> both legs iter-bounded.
    sp_options.time_budget_sec = options.time_budget_sec * 0.5;
    bstar_options.time_budget_sec = options.time_budget_sec * 0.5;

    SearchResult sp_result = runPlacementSearch(problem, *sp, sp_options);
    std::mt19937 rng(bstar_options.seed);
    // Re-decode derives soft dims from the ORIGINAL problem areas, so any FT inflation accepted by the sp leg's
    // finalize is intentionally discarded here; each leg finalizes independently and the better full result wins.
    PlacementState bstar_seed = bstar->seedFromPlacement(problem, sp_result.best_placement, rng);
    SearchResult bstar_result = runPlacementSearch(problem, *bstar, bstar_options, bstar_seed);

    SearchResult result = paydownBetter(bstar_result.best_routing, bstar_result.best_score.value,
                                        sp_result.best_routing, sp_result.best_score.value)
                              ? bstar_result
                              : sp_result;
    result.engine_name = "hybrid";
    result.iterations = sp_result.iterations + bstar_result.iterations;
    result.exact_evaluations = sp_result.exact_evaluations + bstar_result.exact_evaluations;
    return result;
}

}  // namespace efgr
