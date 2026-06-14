#pragma once

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
};

struct SearchResult {
    std::string engine_name;
    int iterations = 0;
    int exact_evaluations = 0;
    Score best_score;
    Placement best_placement;
    RoutingResult best_routing;
};

std::string placementKey(const Placement& placement);
SearchResult runPlacementSearch(const Problem& problem, const PlacementEngine& engine, const SearchOptions& options);
SearchResult runPlacementSearch(const Problem& problem, const PlacementEngine& engine, const SearchOptions& options,
                                const PlacementState& initial_state);
SearchResult runHybridPlacementSearch(const Problem& problem, const SearchOptions& options);

}  // namespace efgr
