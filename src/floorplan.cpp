#include "floorplan.hpp"

#include <numeric>

namespace efgr {
namespace {

Rect dimensionFor(const Block& b, double aspect) {
    if (!isSoft(b.type)) return Rect{0.0, 0.0, b.width, b.height};
    double ar = std::max(b.ar_min, std::min(b.ar_max, aspect));
    double w = std::sqrt(std::max(1.0, b.area * ar));
    double h = b.area / w;
    return Rect{0.0, 0.0, w, h};
}

bool locHasTop(const std::string& loc) { return !loc.empty() && loc[0] == 'T'; }
bool locHasBottom(const std::string& loc) { return !loc.empty() && loc[0] == 'B'; }
bool locHasLeft(const std::string& loc) { return !loc.empty() && loc[0] == 'L'; }
bool locHasRight(const std::string& loc) { return !loc.empty() && loc[0] == 'R'; }

Rect placeEdge(const Problem& p, const Block& b, const std::string& loc) {
    Rect r = dimensionFor(b, 1.0);
    double third_w = p.outline_w / 3.0;
    double third_h = p.outline_h / 3.0;
    if (locHasTop(loc)) r.y = p.outline_h - r.h;
    if (locHasBottom(loc)) r.y = 0.0;
    if (locHasLeft(loc)) r.x = 0.0;
    if (locHasRight(loc)) r.x = p.outline_w - r.w;

    if (loc == "TM") r.x = std::max(0.0, (p.outline_w - r.w) * 0.5);
    if (loc == "TR") r.x = p.outline_w - r.w;
    if (loc == "TL") r.x = 0.0;
    if (loc == "BM") r.x = std::max(0.0, (p.outline_w - r.w) * 0.5);
    if (loc == "BR") r.x = p.outline_w - r.w;
    if (loc == "BL") r.x = 0.0;
    if (loc == "LM") r.y = std::max(0.0, (p.outline_h - r.h) * 0.5);
    if (loc == "LB") r.y = 0.0;
    if (loc == "LT") r.y = p.outline_h - r.h;
    if (loc == "RM") r.y = std::max(0.0, (p.outline_h - r.h) * 0.5);
    if (loc == "RB") r.y = 0.0;
    if (loc == "RT") r.y = p.outline_h - r.h;

    if (loc == "TM" || loc == "BM") r.x = std::min(std::max(r.x, third_w), 2.0 * third_w - r.w);
    if (loc == "LM" || loc == "RM") r.y = std::min(std::max(r.y, third_h), 2.0 * third_h - r.h);
    r.x = std::max(0.0, std::min(r.x, p.outline_w - r.w));
    r.y = std::max(0.0, std::min(r.y, p.outline_h - r.h));
    return r;
}

bool collidesWithPlaced(const std::vector<Rect>& rects, const std::vector<bool>& placed, int self, const Rect& candidate) {
    for (std::size_t i = 0; i < rects.size(); ++i) {
        if (static_cast<int>(i) == self || !placed[i]) continue;
        if (overlaps(candidate, rects[i])) return true;
    }
    return false;
}

void placeAtFirstOpenSlot(const Problem& problem, Placement& placement, std::vector<bool>& placed, int idx) {
    Rect r = placement.rects[idx];
    double step = 20.0;
    for (double y = 0.0; y + r.h <= problem.outline_h + kEps; y += step) {
        for (double x = 0.0; x + r.w <= problem.outline_w + kEps; x += step) {
            Rect candidate{x, y, r.w, r.h};
            if (!collidesWithPlaced(placement.rects, placed, idx, candidate)) {
                placement.rects[idx] = candidate;
                placed[idx] = true;
                return;
            }
        }
    }
    placement.rects[idx] = r;
    placed[idx] = true;
}

}  // namespace

Placement makeSeedPlacement(const Problem& problem) {
    Placement placement;
    placement.rects.resize(problem.blocks.size());
    placement.aspect.resize(problem.blocks.size(), 1.0);

    for (std::size_t i = 0; i < problem.blocks.size(); ++i) {
        const auto& b = problem.blocks[i];
        if (isSoft(b.type)) {
            double ar = std::max(b.ar_min, std::min(b.ar_max, 1.0));
            placement.aspect[i] = ar;
            placement.rects[i] = dimensionFor(b, ar);
        } else {
            placement.rects[i] = dimensionFor(b, 1.0);
        }
    }

    for (std::size_t i = 0; i < problem.blocks.size(); ++i) {
        const auto& b = problem.blocks[i];
        if (isEdge(b.type)) {
            std::string loc = b.locations.empty() ? "TL" : b.locations.front();
            placement.rects[i] = placeEdge(problem, b, loc);
        }
    }

    std::vector<bool> placed(problem.blocks.size(), false);
    for (std::size_t i = 0; i < problem.blocks.size(); ++i) {
        if (isEdge(problem.blocks[i].type)) placed[i] = true;
    }
    for (std::size_t i = 0; i < problem.blocks.size(); ++i) {
        if (isEdge(problem.blocks[i].type)) continue;
        placeAtFirstOpenSlot(problem, placement, placed, static_cast<int>(i));
    }
    return placement;
}

BTreeState initialState(const Problem& problem) {
    BTreeState state;
    for (std::size_t i = 0; i < problem.blocks.size(); ++i) {
        if (!isEdge(problem.blocks[i].type)) state.order.push_back(static_cast<int>(i));
    }
    state.soft_aspect.resize(problem.blocks.size(), 1.0);
    state.edge_location_choice.resize(problem.blocks.size(), 0);
    for (std::size_t i = 0; i < problem.blocks.size(); ++i) {
        if (isSoft(problem.blocks[i].type)) {
            state.soft_aspect[i] = std::max(problem.blocks[i].ar_min, std::min(problem.blocks[i].ar_max, 1.0));
        }
    }
    return state;
}

Placement decodeState(const Problem& problem, const BTreeState& state) {
    Placement p;
    p.rects.resize(problem.blocks.size());
    p.aspect = state.soft_aspect;
    for (std::size_t i = 0; i < problem.blocks.size(); ++i) {
        double ar = isSoft(problem.blocks[i].type) ? state.soft_aspect[i] : 1.0;
        p.rects[i] = dimensionFor(problem.blocks[i], ar);
        if (isEdge(problem.blocks[i].type)) {
            const auto& locs = problem.blocks[i].locations;
            int idx = state.edge_location_choice[i];
            if (locs.empty()) {
                p.rects[i] = placeEdge(problem, problem.blocks[i], "TL");
            } else {
                idx = std::max(0, std::min(idx, static_cast<int>(locs.size()) - 1));
                p.rects[i] = placeEdge(problem, problem.blocks[i], locs[idx]);
            }
        }
    }

    std::vector<bool> placed(problem.blocks.size(), false);
    for (std::size_t i = 0; i < problem.blocks.size(); ++i) {
        if (isEdge(problem.blocks[i].type)) placed[i] = true;
    }
    for (int idx : state.order) {
        placeAtFirstOpenSlot(problem, p, placed, idx);
    }
    return p;
}

void mutateState(const Problem& problem, BTreeState& state, std::mt19937& rng) {
    std::uniform_int_distribution<int> move_dist(0, 3);
    int move = move_dist(rng);
    if (move == 0 && state.order.size() >= 2) {
        std::uniform_int_distribution<int> d(0, static_cast<int>(state.order.size()) - 1);
        int a = d(rng), b = d(rng);
        std::swap(state.order[a], state.order[b]);
    } else if (move == 1 && state.order.size() >= 2) {
        std::uniform_int_distribution<int> d(0, static_cast<int>(state.order.size()) - 1);
        int from = d(rng), to = d(rng);
        int value = state.order[from];
        state.order.erase(state.order.begin() + from);
        state.order.insert(state.order.begin() + std::min<int>(to, state.order.size()), value);
    } else if (move == 2) {
        std::vector<int> softs;
        for (std::size_t i = 0; i < problem.blocks.size(); ++i) {
            if (isSoft(problem.blocks[i].type)) softs.push_back(static_cast<int>(i));
        }
        if (!softs.empty()) {
            std::uniform_int_distribution<int> d(0, static_cast<int>(softs.size()) - 1);
            int idx = softs[d(rng)];
            std::uniform_real_distribution<double> f(0.85, 1.15);
            state.soft_aspect[idx] = std::max(problem.blocks[idx].ar_min,
                                             std::min(problem.blocks[idx].ar_max, state.soft_aspect[idx] * f(rng)));
        }
    } else {
        std::vector<int> edges;
        for (std::size_t i = 0; i < problem.blocks.size(); ++i) {
            if (isEdge(problem.blocks[i].type) && problem.blocks[i].locations.size() > 1) edges.push_back(static_cast<int>(i));
        }
        if (!edges.empty()) {
            std::uniform_int_distribution<int> d(0, static_cast<int>(edges.size()) - 1);
            int idx = edges[d(rng)];
            state.edge_location_choice[idx] = (state.edge_location_choice[idx] + 1) %
                                              static_cast<int>(problem.blocks[idx].locations.size());
        }
    }
}

}  // namespace efgr
