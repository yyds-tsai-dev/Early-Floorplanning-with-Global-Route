#include "search.hpp"

#include <cmath>
#include <random>
#include <sstream>
#include <unordered_map>
#include <utility>

#include "eval.hpp"
#include "ft.hpp"
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
        RoutingResult new_routing;
        Score exact = exactScore(inflated, placement, &new_routing);
        routing = new_routing;
        tryCommit(placement, exact, new_routing);
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
    for (int iter = 1; iter <= options.iters; ++iter) {
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

    for (const auto& candidate : top_candidates) {
        RoutingResult routing;
        Score exact = exactCached(candidate.placement, &routing);
        if (exact.value < result.best_score.value) {
            result.best_score = exact;
            result.best_placement = candidate.placement;
            result.best_routing = routing;
        }

        Placement compacted = compactTowardOrigin(problem, candidate.placement);
        // Re-stamp immediately (see the matching comment in finalizeFeedthrough above): keeps
        // `compacted` internally consistent before it is cached/stored as best_placement, and
        // gives exactCached's placementKey the real declared outline instead of the reset 0x0.
        shrinkWrapOutline(problem, compacted);
        Score compact_exact = exactCached(compacted, &routing);
        if (compact_exact.value < result.best_score.value) {
            result.best_score = compact_exact;
            result.best_placement = compacted;
            result.best_routing = routing;
        }
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
