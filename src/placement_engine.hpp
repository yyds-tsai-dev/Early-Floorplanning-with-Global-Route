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

// Exact: re-legalizes edge blocks against the shrunk outline (grow-until-legal loop). Mutates
// placement (edge rects re-anchored to the shrunk border + outline_w/h stamped). Core (non-edge)
// rects are never moved. Falls back to problem's outline (MAX) if no smaller (W,H) is legal
// within max_grow_steps -- always terminates legal (design p1-design.md §1.2.4).
OutlineFit shrinkWrapOutline(const Problem& problem, Placement& placement, int max_grow_steps = 6);

}  // namespace efgr
