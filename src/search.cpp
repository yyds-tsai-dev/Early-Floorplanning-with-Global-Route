#include "search.hpp"

#include <cmath>
#include <random>
#include <sstream>
#include <unordered_map>
#include <utility>

#include "eval.hpp"

namespace efgr {
namespace {

bool axisOverlap(double a0, double a1, double b0, double b1) {
    return std::min(a1, b1) - std::max(a0, b0) > kEps;
}

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
    return placement;
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
    return out.str();
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
        Score compact_exact = exactCached(compacted, &routing);
        if (compact_exact.value < result.best_score.value) {
            result.best_score = compact_exact;
            result.best_placement = compacted;
            result.best_routing = routing;
        }
    }

    (void)best_state;
    (void)current_place;
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
    PlacementState bstar_seed = bstar->seedFromPlacement(problem, sp_result.best_placement, rng);
    SearchResult bstar_result = runPlacementSearch(problem, *bstar, bstar_options, bstar_seed);

    SearchResult result = bstar_result.best_score.value < sp_result.best_score.value ? bstar_result : sp_result;
    result.engine_name = "hybrid";
    result.iterations = sp_result.iterations + bstar_result.iterations;
    result.exact_evaluations = sp_result.exact_evaluations + bstar_result.exact_evaluations;
    return result;
}

}  // namespace efgr
