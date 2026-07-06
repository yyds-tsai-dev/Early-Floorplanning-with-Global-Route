#pragma once

#include <memory>
#include <random>
#include <string>
#include <vector>

#include "model.hpp"

namespace efgr {

struct PlacementState {
    std::vector<int> sequence_pos;
    std::vector<int> sequence_neg;

    std::vector<int> nodes;
    std::vector<int> parent;
    std::vector<int> left;
    std::vector<int> right;
    int root = -1;

    std::vector<double> soft_aspect;
    std::vector<int> edge_location_choice;
};

class PlacementEngine {
public:
    virtual ~PlacementEngine() = default;

    virtual std::string name() const = 0;
    virtual PlacementState initialState(const Problem& problem, std::mt19937& rng) const = 0;
    virtual PlacementState seedFromPlacement(const Problem& problem, const Placement& placement,
                                             std::mt19937& rng) const;
    virtual void mutate(const Problem& problem, PlacementState& state, std::mt19937& rng) const = 0;
    virtual Placement decode(const Problem& problem, const PlacementState& state) const = 0;
    virtual bool isValidState(const Problem& problem, const PlacementState& state) const = 0;
};

std::unique_ptr<PlacementEngine> makePlacementEngine(const std::string& name);
std::vector<std::string> placementEngineNames();

struct OutlineFit {
    double w = 0.0;
    double h = 0.0;
    bool legal = true;
};

// Cheap: O(n) lower-bound arithmetic + thirds feasibility probe only, no edge relegalization loop.
// Does NOT mutate placement; returns the estimated (W,H) for cheapScore.
OutlineFit shrinkWrapEstimate(const Problem& problem, const Placement& placement);

// Exact: re-legalizes edge blocks against the shrunk outline via t-interpolation bisection plus
// per-axis refinement (design p1-design.md §1.2.4 amendment 2026-07-06; replaces the earlier
// linear x1.05 grow loop, which exhausted before reaching the true feasible frontier on loose
// cases). Mutates placement (edge rects re-anchored to the shrunk border + outline_w/h stamped).
// Core (non-edge) rects are never moved. Falls back to problem's outline (MAX) if no smaller
// (W,H) is legal within max_grow_steps bisection iterations -- always terminates legal.
// max_grow_steps default raised 6 -> 12 (bisection converges faster per step than linear growth,
// but the iteration cap is now shared with the coupled + two per-axis refinement passes).
OutlineFit shrinkWrapOutline(const Problem& problem, Placement& placement, int max_grow_steps = 12);

}  // namespace efgr
