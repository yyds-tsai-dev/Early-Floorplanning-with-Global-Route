#pragma once

#include <random>

#include "model.hpp"

namespace efgr {

struct BTreeState {
    std::vector<int> order;
    std::vector<double> soft_aspect;
    std::vector<int> edge_location_choice;
};

Placement makeSeedPlacement(const Problem& problem);
BTreeState initialState(const Problem& problem);
Placement decodeState(const Problem& problem, const BTreeState& state);
void mutateState(const Problem& problem, BTreeState& state, std::mt19937& rng);

}  // namespace efgr
