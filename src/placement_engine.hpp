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

}  // namespace efgr
