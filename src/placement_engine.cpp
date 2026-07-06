#include "placement_engine.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace efgr {
namespace {

Rect dimensionFor(const Block& block, double aspect) {
    if (!isSoft(block.type)) return Rect{0.0, 0.0, block.width, block.height};
    double ar = std::max(block.ar_min, std::min(block.ar_max, aspect));
    double w = std::sqrt(std::max(1.0, block.area * ar));
    double h = block.area / w;
    return Rect{0.0, 0.0, w, h};
}

bool locHasTop(const std::string& loc) { return !loc.empty() && loc[0] == 'T'; }
bool locHasBottom(const std::string& loc) { return !loc.empty() && loc[0] == 'B'; }
bool locHasLeft(const std::string& loc) { return !loc.empty() && loc[0] == 'L'; }
bool locHasRight(const std::string& loc) { return !loc.empty() && loc[0] == 'R'; }

bool locInTopBand(const std::string& loc) { return loc.find('T') != std::string::npos; }
bool locInBottomBand(const std::string& loc) { return loc.find('B') != std::string::npos; }
bool locInLeftBand(const std::string& loc) { return loc.find('L') != std::string::npos; }
bool locInRightBand(const std::string& loc) { return loc.find('R') != std::string::npos; }

bool collidesWithPlaced(const std::vector<Rect>& rects, const std::vector<bool>& placed, int self,
                        const Rect& candidate);

Rect placeEdge(const Problem& problem, const Block& block, const std::vector<std::string>& locs) {
    Rect r = dimensionFor(block, 1.0);
    double third_w = problem.outline_w / 3.0;
    double third_h = problem.outline_h / 3.0;
    bool top = false, bottom = false, left = false, right = false;
    bool mid_x = false, mid_y = false;
    for (const auto& loc : locs) {
        top = top || locHasTop(loc);
        bottom = bottom || locHasBottom(loc);
        left = left || locHasLeft(loc);
        right = right || locHasRight(loc);
        mid_x = mid_x || loc == "TM" || loc == "BM";
        mid_y = mid_y || loc == "LM" || loc == "RM";
        if (!left && locInLeftBand(loc)) r.x = 0.0;
        if (!right && locInRightBand(loc)) r.x = problem.outline_w - r.w;
        if (!top && locInTopBand(loc)) r.y = problem.outline_h - r.h;
        if (!bottom && locInBottomBand(loc)) r.y = 0.0;
    }

    if (top) r.y = problem.outline_h - r.h;
    if (bottom) r.y = 0.0;
    if (left) r.x = 0.0;
    if (right) r.x = problem.outline_w - r.w;
    if (!left && !right && mid_x) r.x = std::max(0.0, (problem.outline_w - r.w) * 0.5);
    if (!top && !bottom && mid_y) r.y = std::max(0.0, (problem.outline_h - r.h) * 0.5);

    if (!left && !right && mid_x) r.x = std::min(std::max(r.x, third_w), 2.0 * third_w - r.w);
    if (!top && !bottom && mid_y) r.y = std::min(std::max(r.y, third_h), 2.0 * third_h - r.h);
    r.x = std::max(0.0, std::min(r.x, problem.outline_w - r.w));
    r.y = std::max(0.0, std::min(r.y, problem.outline_h - r.h));
    return r;
}

char locSide(const std::string& loc) {
    return loc.empty() ? '\0' : loc[0];
}

std::pair<double, double> locSegment(const Problem& problem, const std::string& loc) {
    double third_w = problem.outline_w / 3.0;
    double third_h = problem.outline_h / 3.0;
    if (loc == "TL" || loc == "BL") return {0.0, third_w};
    if (loc == "TM" || loc == "BM") return {third_w, 2.0 * third_w};
    if (loc == "TR" || loc == "BR") return {2.0 * third_w, problem.outline_w};
    if (loc == "LB" || loc == "RB") return {0.0, third_h};
    if (loc == "LM" || loc == "RM") return {third_h, 2.0 * third_h};
    if (loc == "LT" || loc == "RT") return {2.0 * third_h, problem.outline_h};
    return {0.0, 0.0};
}

bool positiveOverlap(double a0, double a1, double b0, double b1) {
    return std::min(a1, b1) - std::max(a0, b0) > kEps;
}

bool rectTouchesLocation(const Problem& problem, const Rect& rect, const std::string& loc) {
    auto [lo, hi] = locSegment(problem, loc);
    char side = locSide(loc);
    if (side == 'T') return std::abs(rect.top() - problem.outline_h) < kEps && positiveOverlap(rect.x, rect.right(), lo, hi);
    if (side == 'B') return std::abs(rect.y) < kEps && positiveOverlap(rect.x, rect.right(), lo, hi);
    if (side == 'L') return std::abs(rect.x) < kEps && positiveOverlap(rect.y, rect.top(), lo, hi);
    if (side == 'R') return std::abs(rect.right() - problem.outline_w) < kEps && positiveOverlap(rect.y, rect.top(), lo, hi);
    return true;
}

bool edgeRectLegal(const Problem& problem, const Block& block, const Rect& rect) {
    if (block.locations.empty()) return true;
    std::vector<char> sides;
    for (const auto& loc : block.locations) {
        char side = locSide(loc);
        if (std::find(sides.begin(), sides.end(), side) == sides.end()) sides.push_back(side);
    }
    if (sides.size() == 1) {
        for (const auto& loc : block.locations) {
            if (rectTouchesLocation(problem, rect, loc)) return true;
        }
        return false;
    }
    for (const auto& loc : block.locations) {
        if (!rectTouchesLocation(problem, rect, loc)) return false;
    }
    return true;
}

bool hasMultipleBoundarySides(const Block& block) {
    std::vector<char> sides;
    for (const auto& loc : block.locations) {
        char side = locSide(loc);
        if (std::find(sides.begin(), sides.end(), side) == sides.end()) sides.push_back(side);
    }
    return sides.size() > 1;
}

void addCandidate(std::vector<double>& values, double value, double lo, double hi) {
    values.push_back(std::max(lo, std::min(hi, value)));
}

Rect firstLegalEdgeSlot(const Problem& problem, const Block& block, const std::vector<Rect>& rects,
                        const std::vector<bool>& placed, int idx) {
    Rect base = dimensionFor(block, 1.0);
    std::vector<std::string> locs = block.locations.empty() ? std::vector<std::string>{"TL"} : block.locations;
    if (hasMultipleBoundarySides(block)) return placeEdge(problem, block, locs);

    char side = locSide(locs.front());
    std::vector<double> coords;
    double lo = 0.0;
    double hi = 0.0;
    if (side == 'T' || side == 'B') {
        hi = std::max(0.0, problem.outline_w - base.w);
        for (const auto& loc : locs) {
            auto [seg_lo, seg_hi] = locSegment(problem, loc);
            addCandidate(coords, seg_lo, lo, hi);
            addCandidate(coords, seg_hi - base.w, lo, hi);
        }
        for (std::size_t i = 0; i < rects.size(); ++i) {
            if (!placed[i]) continue;
            addCandidate(coords, rects[i].right(), lo, hi);
            addCandidate(coords, rects[i].x - base.w, lo, hi);
        }
    } else {
        hi = std::max(0.0, problem.outline_h - base.h);
        for (const auto& loc : locs) {
            auto [seg_lo, seg_hi] = locSegment(problem, loc);
            addCandidate(coords, seg_lo, lo, hi);
            addCandidate(coords, seg_hi - base.h, lo, hi);
        }
        for (std::size_t i = 0; i < rects.size(); ++i) {
            if (!placed[i]) continue;
            addCandidate(coords, rects[i].top(), lo, hi);
            addCandidate(coords, rects[i].y - base.h, lo, hi);
        }
    }
    std::sort(coords.begin(), coords.end());
    coords.erase(std::unique(coords.begin(), coords.end(), [](double a, double b) {
                     return std::abs(a - b) < kEps;
                 }),
                 coords.end());

    for (double coord : coords) {
        Rect candidate = base;
        if (side == 'T') {
            candidate.x = coord;
            candidate.y = problem.outline_h - candidate.h;
        } else if (side == 'B') {
            candidate.x = coord;
            candidate.y = 0.0;
        } else if (side == 'L') {
            candidate.x = 0.0;
            candidate.y = coord;
        } else if (side == 'R') {
            candidate.x = problem.outline_w - candidate.w;
            candidate.y = coord;
        }
        if (!edgeRectLegal(problem, block, candidate)) continue;
        if (!collidesWithPlaced(rects, placed, idx, candidate)) return candidate;
    }
    return placeEdge(problem, block, locs);
}

void legalizeEdgeBlocks(const Problem& problem, Placement& placement) {
    std::vector<int> edges;
    for (std::size_t i = 0; i < problem.blocks.size(); ++i) {
        if (isEdge(problem.blocks[i].type)) edges.push_back(static_cast<int>(i));
    }
    std::sort(edges.begin(), edges.end(), [&](int a, int b) {
        bool ac = hasMultipleBoundarySides(problem.blocks[a]);
        bool bc = hasMultipleBoundarySides(problem.blocks[b]);
        if (ac != bc) return ac > bc;
        double as = std::max(placement.rects[a].w, placement.rects[a].h);
        double bs = std::max(placement.rects[b].w, placement.rects[b].h);
        if (std::abs(as - bs) > kEps) return as > bs;
        return a < b;
    });

    std::vector<bool> placed(problem.blocks.size(), false);
    for (int idx : edges) {
        placement.rects[idx] = firstLegalEdgeSlot(problem, problem.blocks[idx], placement.rects, placed, idx);
        placed[idx] = true;
    }
}

std::vector<std::string> chosenEdgeLocation(const Problem& problem, const PlacementState& state, int block_idx) {
    const auto& locs = problem.blocks[block_idx].locations;
    if (locs.empty()) return {"TL"};
    int choice = 0;
    if (block_idx >= 0 && static_cast<std::size_t>(block_idx) < state.edge_location_choice.size()) {
        choice = state.edge_location_choice[block_idx];
    }
    choice %= static_cast<int>(locs.size());
    if (choice < 0) choice += static_cast<int>(locs.size());
    return {locs[choice]};
}

std::vector<int> movableBlocks(const Problem& problem) {
    std::vector<int> ids;
    for (std::size_t i = 0; i < problem.blocks.size(); ++i) {
        if (!isEdge(problem.blocks[i].type)) ids.push_back(static_cast<int>(i));
    }
    return ids;
}

std::vector<double> initialAspect(const Problem& problem) {
    std::vector<double> aspect(problem.blocks.size(), 1.0);
    for (std::size_t i = 0; i < problem.blocks.size(); ++i) {
        if (isSoft(problem.blocks[i].type)) {
            aspect[i] = std::max(problem.blocks[i].ar_min, std::min(problem.blocks[i].ar_max, 1.0));
        }
    }
    return aspect;
}

PlacementState baseState(const Problem& problem) {
    PlacementState state;
    state.soft_aspect = initialAspect(problem);
    state.edge_location_choice.assign(problem.blocks.size(), 0);
    return state;
}

Placement basePlacement(const Problem& problem, const PlacementState& state) {
    Placement placement;
    placement.rects.resize(problem.blocks.size());
    placement.aspect = state.soft_aspect.empty() ? initialAspect(problem) : state.soft_aspect;
    if (placement.aspect.size() < problem.blocks.size()) placement.aspect.resize(problem.blocks.size(), 1.0);

    for (std::size_t i = 0; i < problem.blocks.size(); ++i) {
        double ar = isSoft(problem.blocks[i].type) ? placement.aspect[i] : 1.0;
        placement.rects[i] = dimensionFor(problem.blocks[i], ar);
        if (isEdge(problem.blocks[i].type)) {
            placement.rects[i] = placeEdge(problem, problem.blocks[i],
                                           chosenEdgeLocation(problem, state, static_cast<int>(i)));
        }
    }
    legalizeEdgeBlocks(problem, placement);
    return placement;
}

bool collidesWithPlaced(const std::vector<Rect>& rects, const std::vector<bool>& placed, int self,
                        const Rect& candidate) {
    for (std::size_t i = 0; i < rects.size(); ++i) {
        if (static_cast<int>(i) == self || !placed[i]) continue;
        if (overlaps(candidate, rects[i])) return true;
    }
    return false;
}

double maxTopOverlap(const std::vector<Rect>& rects, const std::vector<bool>& placed, int self, double x, double w) {
    double y = 0.0;
    Rect probe{x, 0.0, w, 1.0};
    for (std::size_t i = 0; i < rects.size(); ++i) {
        if (static_cast<int>(i) == self || !placed[i]) continue;
        probe.y = rects[i].y;
        probe.h = rects[i].h;
        if (probe.x + kEps < rects[i].right() && rects[i].x + kEps < probe.right()) {
            y = std::max(y, rects[i].top());
        }
    }
    return y;
}

void placeAtFirstOpenSlot(const Problem& problem, Placement& placement, std::vector<bool>& placed, int idx) {
    Rect r = placement.rects[idx];
    double step = 10.0;
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

void placeWithContour(const Problem& problem, Placement& placement, std::vector<bool>& placed, int idx, double desired_x,
                      double desired_y) {
    Rect r = placement.rects[idx];
    if (problem.outline_w > r.w) {
        r.x = std::max(0.0, std::min(desired_x, problem.outline_w - r.w));
    } else {
        r.x = 0.0;
    }
    r.y = std::max(std::max(0.0, desired_y), maxTopOverlap(placement.rects, placed, idx, r.x, r.w));

    for (int guard = 0; guard < static_cast<int>(placement.rects.size()) + 2; ++guard) {
        if (!collidesWithPlaced(placement.rects, placed, idx, r)) break;
        r.y = std::max(r.y, maxTopOverlap(placement.rects, placed, idx, r.x, r.w));
    }

    placement.rects[idx] = r;
    if (r.top() > problem.outline_h + kEps || collidesWithPlaced(placement.rects, placed, idx, r)) {
        placeAtFirstOpenSlot(problem, placement, placed, idx);
    } else {
        placed[idx] = true;
    }
}

void mutateSoftAspect(const Problem& problem, PlacementState& state, std::mt19937& rng) {
    std::vector<int> softs;
    for (std::size_t i = 0; i < problem.blocks.size(); ++i) {
        if (isSoft(problem.blocks[i].type)) softs.push_back(static_cast<int>(i));
    }
    if (softs.empty()) return;
    if (state.soft_aspect.size() < problem.blocks.size()) state.soft_aspect = initialAspect(problem);
    std::uniform_int_distribution<int> pick(0, static_cast<int>(softs.size()) - 1);
    std::uniform_real_distribution<double> factor(0.85, 1.15);
    int idx = softs[pick(rng)];
    state.soft_aspect[idx] = std::max(problem.blocks[idx].ar_min,
                                      std::min(problem.blocks[idx].ar_max, state.soft_aspect[idx] * factor(rng)));
}

void mutateEdgeChoice(const Problem& problem, PlacementState& state, std::mt19937& rng) {
    std::vector<int> edges;
    for (std::size_t i = 0; i < problem.blocks.size(); ++i) {
        if (isEdge(problem.blocks[i].type) && problem.blocks[i].locations.size() > 1) {
            edges.push_back(static_cast<int>(i));
        }
    }
    if (edges.empty()) return;
    if (state.edge_location_choice.size() < problem.blocks.size()) {
        state.edge_location_choice.assign(problem.blocks.size(), 0);
    }
    std::uniform_int_distribution<int> pick(0, static_cast<int>(edges.size()) - 1);
    int idx = edges[pick(rng)];
    state.edge_location_choice[idx] =
        (state.edge_location_choice[idx] + 1) % static_cast<int>(problem.blocks[idx].locations.size());
}

bool sameElements(std::vector<int> a, std::vector<int> b) {
    std::sort(a.begin(), a.end());
    std::sort(b.begin(), b.end());
    return a == b;
}

class SequencePairEngine final : public PlacementEngine {
public:
    std::string name() const override { return "sp"; }

    PlacementState initialState(const Problem& problem, std::mt19937&) const override {
        PlacementState state = baseState(problem);
        state.sequence_pos = movableBlocks(problem);
        state.sequence_neg = state.sequence_pos;
        return state;
    }

    PlacementState seedFromPlacement(const Problem& problem, const Placement& placement, std::mt19937&) const override {
        PlacementState state = baseState(problem);
        state.sequence_pos = movableBlocks(problem);
        std::sort(state.sequence_pos.begin(), state.sequence_pos.end(), [&](int a, int b) {
            if (std::abs(placement.rects[a].x - placement.rects[b].x) > kEps) {
                return placement.rects[a].x < placement.rects[b].x;
            }
            return placement.rects[a].y < placement.rects[b].y;
        });
        state.sequence_neg = state.sequence_pos;
        std::sort(state.sequence_neg.begin(), state.sequence_neg.end(), [&](int a, int b) {
            if (std::abs(placement.rects[a].y - placement.rects[b].y) > kEps) {
                return placement.rects[a].y < placement.rects[b].y;
            }
            return placement.rects[a].x < placement.rects[b].x;
        });
        return state;
    }

    void mutate(const Problem& problem, PlacementState& state, std::mt19937& rng) const override {
        if (state.sequence_pos.empty() || state.sequence_neg.empty()) state = initialState(problem, rng);
        std::uniform_int_distribution<int> move_dist(0, 4);
        int move = move_dist(rng);
        if (move <= 2 && state.sequence_pos.size() >= 2) {
            std::uniform_int_distribution<int> pick(0, static_cast<int>(state.sequence_pos.size()) - 1);
            int a = pick(rng);
            int b = pick(rng);
            if (move == 0 || move == 2) std::swap(state.sequence_pos[a], state.sequence_pos[b]);
            if (move == 1 || move == 2) std::swap(state.sequence_neg[a], state.sequence_neg[b]);
        } else if (move == 3) {
            mutateSoftAspect(problem, state, rng);
        } else {
            mutateEdgeChoice(problem, state, rng);
        }
    }

    Placement decode(const Problem& problem, const PlacementState& state) const override {
        Placement placement = basePlacement(problem, state);
        std::vector<int> blocks = state.sequence_pos.empty() ? movableBlocks(problem) : state.sequence_pos;
        std::vector<int> neg = state.sequence_neg.empty() ? blocks : state.sequence_neg;

        std::vector<int> pos_rank(problem.blocks.size(), -1);
        std::vector<int> neg_rank(problem.blocks.size(), -1);
        for (std::size_t i = 0; i < blocks.size(); ++i) pos_rank[blocks[i]] = static_cast<int>(i);
        for (std::size_t i = 0; i < neg.size(); ++i) neg_rank[neg[i]] = static_cast<int>(i);

        struct Constraint {
            int from;
            int to;
            double weight;
        };
        std::vector<Constraint> horizontal;
        std::vector<Constraint> vertical;
        for (std::size_t i = 0; i < blocks.size(); ++i) {
            for (std::size_t j = i + 1; j < blocks.size(); ++j) {
                int a = blocks[i];
                int b = blocks[j];
                if (pos_rank[a] < 0 || pos_rank[b] < 0 || neg_rank[a] < 0 || neg_rank[b] < 0) continue;
                if (pos_rank[a] < pos_rank[b] && neg_rank[a] < neg_rank[b]) {
                    horizontal.push_back(Constraint{a, b, placement.rects[a].w});
                } else if (pos_rank[a] < pos_rank[b]) {
                    vertical.push_back(Constraint{a, b, placement.rects[a].h});
                } else if (neg_rank[a] < neg_rank[b]) {
                    vertical.push_back(Constraint{b, a, placement.rects[b].h});
                } else {
                    horizontal.push_back(Constraint{b, a, placement.rects[b].w});
                }
            }
        }

        std::vector<double> x(problem.blocks.size(), 0.0);
        std::vector<double> y(problem.blocks.size(), 0.0);
        for (std::size_t pass = 0; pass < blocks.size(); ++pass) {
            for (const auto& c : horizontal) x[c.to] = std::max(x[c.to], x[c.from] + c.weight);
            for (const auto& c : vertical) y[c.to] = std::max(y[c.to], y[c.from] + c.weight);
        }

        std::vector<bool> placed(problem.blocks.size(), false);
        for (std::size_t i = 0; i < problem.blocks.size(); ++i) placed[i] = isEdge(problem.blocks[i].type);
        for (int idx : blocks) {
            placement.rects[idx].x = x[idx];
            placement.rects[idx].y = y[idx];
        }
        std::vector<int> order = blocks;
        std::sort(order.begin(), order.end(), [&](int a, int b) {
            if (std::abs(placement.rects[a].x - placement.rects[b].x) > kEps) {
                return placement.rects[a].x < placement.rects[b].x;
            }
            return placement.rects[a].y < placement.rects[b].y;
        });
        for (int idx : order) {
            placeWithContour(problem, placement, placed, idx, placement.rects[idx].x, placement.rects[idx].y);
        }
        shrinkWrapOutline(problem, placement);
        return placement;
    }

    bool isValidState(const Problem& problem, const PlacementState& state) const override {
        std::vector<int> expected = movableBlocks(problem);
        return sameElements(expected, state.sequence_pos) && sameElements(expected, state.sequence_neg);
    }
};

class BStarEngine final : public PlacementEngine {
public:
    std::string name() const override { return "bstar"; }

    PlacementState initialState(const Problem& problem, std::mt19937&) const override {
        PlacementState state = baseState(problem);
        state.nodes = movableBlocks(problem);
        int n = static_cast<int>(state.nodes.size());
        state.parent.assign(n, -1);
        state.left.assign(n, -1);
        state.right.assign(n, -1);
        state.root = n > 0 ? 0 : -1;
        for (int i = 1; i < n; ++i) {
            state.parent[i] = i - 1;
            state.left[i - 1] = i;
        }
        return state;
    }

    PlacementState seedFromPlacement(const Problem& problem, const Placement& placement, std::mt19937&) const override {
        PlacementState state = baseState(problem);
        state.nodes = movableBlocks(problem);
        std::sort(state.nodes.begin(), state.nodes.end(), [&](int a, int b) {
            if (std::abs(placement.rects[a].x - placement.rects[b].x) > kEps) {
                return placement.rects[a].x < placement.rects[b].x;
            }
            return placement.rects[a].y < placement.rects[b].y;
        });
        int n = static_cast<int>(state.nodes.size());
        state.parent.assign(n, -1);
        state.left.assign(n, -1);
        state.right.assign(n, -1);
        state.root = n > 0 ? 0 : -1;
        for (int i = 1; i < n; ++i) {
            state.parent[i] = i - 1;
            state.left[i - 1] = i;
        }
        return state;
    }

    void mutate(const Problem& problem, PlacementState& state, std::mt19937& rng) const override {
        if (!isValidState(problem, state)) state = initialState(problem, rng);
        std::uniform_int_distribution<int> move_dist(0, 3);
        int move = move_dist(rng);
        int n = static_cast<int>(state.nodes.size());
        if (move == 0 && n >= 2) {
            std::uniform_int_distribution<int> pick(0, n - 1);
            int a = pick(rng);
            int b = pick(rng);
            std::swap(state.nodes[a], state.nodes[b]);
        } else if (move == 1 && n >= 2) {
            moveLeaf(state, rng);
        } else if (move == 2) {
            mutateSoftAspect(problem, state, rng);
        } else {
            mutateEdgeChoice(problem, state, rng);
        }
    }

    Placement decode(const Problem& problem, const PlacementState& state) const override {
        Placement placement = basePlacement(problem, state);
        if (state.root < 0 || state.nodes.empty()) {
            shrinkWrapOutline(problem, placement);
            return placement;
        }

        std::vector<bool> placed(problem.blocks.size(), false);
        for (std::size_t i = 0; i < problem.blocks.size(); ++i) placed[i] = isEdge(problem.blocks[i].type);

        std::vector<int> stack{state.root};
        std::vector<bool> seen(state.nodes.size(), false);
        while (!stack.empty()) {
            int node = stack.back();
            stack.pop_back();
            if (node < 0 || node >= static_cast<int>(state.nodes.size()) || seen[node]) continue;
            seen[node] = true;

            int block_idx = state.nodes[node];
            double desired_x = 0.0;
            int parent = state.parent[node];
            if (parent >= 0 && parent < static_cast<int>(state.nodes.size())) {
                int parent_block = state.nodes[parent];
                if (state.left[parent] == node) {
                    desired_x = placement.rects[parent_block].right();
                } else {
                    desired_x = placement.rects[parent_block].x;
                }
            }
            placeWithContour(problem, placement, placed, block_idx, desired_x, 0.0);

            if (node < static_cast<int>(state.right.size()) && state.right[node] >= 0) stack.push_back(state.right[node]);
            if (node < static_cast<int>(state.left.size()) && state.left[node] >= 0) stack.push_back(state.left[node]);
        }

        for (std::size_t i = 0; i < state.nodes.size(); ++i) {
            if (!seen[i]) placeWithContour(problem, placement, placed, state.nodes[i], 0.0, 0.0);
        }
        shrinkWrapOutline(problem, placement);
        return placement;
    }

    bool isValidState(const Problem& problem, const PlacementState& state) const override {
        std::vector<int> expected = movableBlocks(problem);
        if (!sameElements(expected, state.nodes)) return false;
        int n = static_cast<int>(state.nodes.size());
        if (n == 0) return state.root == -1;
        if (state.root < 0 || state.root >= n) return false;
        if (state.parent.size() != state.nodes.size() || state.left.size() != state.nodes.size() ||
            state.right.size() != state.nodes.size()) {
            return false;
        }
        if (state.parent[state.root] != -1) return false;

        std::vector<int> seen(n, 0);
        return validateNode(state, state.root, -1, seen) && std::all_of(seen.begin(), seen.end(), [](int v) {
                   return v == 1;
               });
    }

private:
    static bool validateNode(const PlacementState& state, int node, int expected_parent, std::vector<int>& seen) {
        if (node < 0 || node >= static_cast<int>(state.nodes.size())) return false;
        if (seen[node] != 0) return false;
        if (state.parent[node] != expected_parent) return false;
        seen[node] = 1;
        int left = state.left[node];
        int right = state.right[node];
        if (left >= 0 && !validateNode(state, left, node, seen)) return false;
        if (right >= 0 && !validateNode(state, right, node, seen)) return false;
        return true;
    }

    static void moveLeaf(PlacementState& state, std::mt19937& rng) {
        std::vector<int> leaves;
        int n = static_cast<int>(state.nodes.size());
        for (int i = 0; i < n; ++i) {
            if (i != state.root && state.left[i] < 0 && state.right[i] < 0) leaves.push_back(i);
        }
        if (leaves.empty()) return;

        std::uniform_int_distribution<int> pick_leaf(0, static_cast<int>(leaves.size()) - 1);
        int leaf = leaves[pick_leaf(rng)];
        int old_parent = state.parent[leaf];
        if (old_parent >= 0) {
            if (state.left[old_parent] == leaf) state.left[old_parent] = -1;
            if (state.right[old_parent] == leaf) state.right[old_parent] = -1;
        }
        state.parent[leaf] = -1;

        std::vector<int> targets;
        for (int i = 0; i < n; ++i) {
            if (i == leaf) continue;
            if (state.left[i] < 0 || state.right[i] < 0) targets.push_back(i);
        }
        if (targets.empty()) {
            state.parent[leaf] = old_parent;
            if (old_parent >= 0) state.left[old_parent] = leaf;
            return;
        }

        std::uniform_int_distribution<int> pick_target(0, static_cast<int>(targets.size()) - 1);
        int target = targets[pick_target(rng)];
        bool use_left = state.left[target] < 0;
        if (state.left[target] < 0 && state.right[target] < 0) {
            std::uniform_int_distribution<int> side(0, 1);
            use_left = side(rng) == 0;
        }
        if (use_left) {
            state.left[target] = leaf;
        } else {
            state.right[target] = leaf;
        }
        state.parent[leaf] = target;
    }
};

}  // namespace

PlacementState PlacementEngine::seedFromPlacement(const Problem& problem, const Placement&, std::mt19937& rng) const {
    return initialState(problem, rng);
}

std::unique_ptr<PlacementEngine> makePlacementEngine(const std::string& name) {
    if (name == "sp" || name == "sequence-pair" || name == "sequence_pair") {
        return std::unique_ptr<PlacementEngine>(new SequencePairEngine());
    }
    if (name == "bstar" || name == "b-tree" || name == "btree") {
        return std::unique_ptr<PlacementEngine>(new BStarEngine());
    }
    throw std::invalid_argument("unknown placement engine: " + name);
}

std::vector<std::string> placementEngineNames() {
    return {"sp", "bstar", "hybrid"};
}

namespace {

// Cheap per-axis lower bound from core (non-edge) block extents and edge block width/height
// needs (design p1-design.md §1.2.1). Does not consider thirds feasibility -- that is probed
// separately in shrinkWrapEstimate.
void lowerBoundNeeds(const Problem& problem, const Placement& placement, double* w_min, double* h_min) {
    double w_core = 0.0;
    double h_core = 0.0;
    double w_edge = 0.0;
    double h_edge = 0.0;
    for (std::size_t i = 0; i < problem.blocks.size(); ++i) {
        const Rect& r = placement.rects[i];
        if (!isEdge(problem.blocks[i].type)) {
            w_core = std::max(w_core, r.right());
            h_core = std::max(h_core, r.top());
            continue;
        }
        // Edge block flush to its border occupies its own width/height regardless of anchor
        // side; corner (multi-side) blocks force both axes simultaneously.
        w_edge = std::max(w_edge, r.w);
        h_edge = std::max(h_edge, r.h);
    }
    *w_min = std::max({w_core, w_edge, 1.0});
    *h_min = std::max({h_core, h_edge, 1.0});
}

// Cheap feasibility probe: on a scoped Problem at candidate (W,H), verify every edge block can
// find a legal slot via firstLegalEdgeSlot, without mutating the caller's placement. Mirrors the
// existing legalizeEdgeBlocks ordering (multi-side/corner blocks first, then largest extent) so
// it estimates the same feasibility as the exact re-anchor loop would find (design §1.2.2-1.2.4).
bool edgeBlocksFeasibleAt(const Problem& problem, const Placement& placement, double w, double h) {
    Problem scoped = problem;
    scoped.outline_w = w;
    scoped.outline_h = h;

    std::vector<int> edges;
    for (std::size_t i = 0; i < problem.blocks.size(); ++i) {
        if (isEdge(problem.blocks[i].type)) edges.push_back(static_cast<int>(i));
    }
    if (edges.empty()) return true;

    std::sort(edges.begin(), edges.end(), [&](int a, int b) {
        bool ac = hasMultipleBoundarySides(problem.blocks[a]);
        bool bc = hasMultipleBoundarySides(problem.blocks[b]);
        if (ac != bc) return ac > bc;
        double as = std::max(placement.rects[a].w, placement.rects[a].h);
        double bs = std::max(placement.rects[b].w, placement.rects[b].h);
        if (std::abs(as - bs) > kEps) return as > bs;
        return a < b;
    });

    std::vector<Rect> rects = placement.rects;
    for (std::size_t i = 0; i < problem.blocks.size(); ++i) {
        if (isEdge(problem.blocks[i].type)) rects[i] = dimensionFor(problem.blocks[i], 1.0);
    }
    std::vector<bool> placed(problem.blocks.size(), false);
    for (int idx : edges) {
        Rect slot = firstLegalEdgeSlot(scoped, problem.blocks[idx], rects, placed, idx);
        if (!edgeRectLegal(scoped, problem.blocks[idx], slot)) return false;
        if (slot.x < -kEps || slot.y < -kEps || slot.right() > w + kEps || slot.top() > h + kEps) return false;
        // firstLegalEdgeSlot's multi-side (corner) branch defers straight to placeEdge, which is
        // a pure function of outline dims + block dims + locations with no awareness of
        // already-placed rects -- unlike the single-side branch it never runs collidesWithPlaced
        // itself. Gate on it explicitly so two opposite corner blocks whose footprints overlap at
        // a tight (W,H) are correctly rejected here (forcing the grow loop), matching the exact
        // shrinkWrapOutline's "no state emits overlap" invariant (design §3.1).
        if (collidesWithPlaced(rects, placed, idx, slot)) return false;
        rects[idx] = slot;
        placed[idx] = true;
    }
    return true;
}

// Verifies every rect in `placement` lies within [0,w]x[0,h] and no two rects overlap. Used as
// the exact shrink-wrap's post-legalization gate (design §1.2.4, §3.1): legalizeEdgeBlocks'
// corner/multi-side branch (firstLegalEdgeSlot -> placeEdge, see :130) places blindly with no
// collision check, so this must independently confirm no overlap survived, not just trust that
// legalizeEdgeBlocks "succeeded".
bool placementLegalAt(const Problem& problem, const Placement& placement, double w, double h) {
    for (std::size_t i = 0; i < placement.rects.size(); ++i) {
        const Rect& r = placement.rects[i];
        if (r.x < -kEps || r.y < -kEps || r.right() > w + kEps || r.top() > h + kEps) return false;
        for (std::size_t j = i + 1; j < placement.rects.size(); ++j) {
            if (overlaps(r, placement.rects[j])) return false;
        }
    }
    Problem scoped = problem;
    scoped.outline_w = w;
    scoped.outline_h = h;
    for (std::size_t i = 0; i < problem.blocks.size(); ++i) {
        if (!isEdge(problem.blocks[i].type)) continue;
        if (!edgeRectLegal(scoped, problem.blocks[i], placement.rects[i])) return false;
    }
    return true;
}

// Shared bisection search (T4.5, design amendment 2026-07-06): feasibility over (W,H) is monotone
// -- if a candidate is feasible, every larger candidate (component-wise) is too, and (MAX_w,MAX_h)
// is always feasible (it is what decode already legally produced). Given per-axis lower bounds
// and a feasibility callback, this finds a near-minimal legal (W,H) in a bounded number of probes:
//   1. try the lower bounds directly -- done if already feasible (tight).
//   2. else t-bisect the coupled line from (w_lb,h_lb) to (max_w,max_h): candidate(t) = lerp(t),
//      t=1 feasible by construction, binary-search the smallest feasible t.
//   3. per-axis refinement at that point: bisect W down alone (H held fixed), then H down alone
//      (W held at its refined value) -- same feasibility callback, same tolerance/iteration cap.
// `max_iterations` bounds both the coupled bisection and each per-axis refinement independently.
// Pure arithmetic on fixed midpoints -- deterministic, no RNG.
template <typename Feasible>
OutlineFit bisectShrinkWrap(double w_lb, double h_lb, double max_w, double max_h, int max_iterations,
                            Feasible feasible) {
    w_lb = std::min(w_lb, max_w);
    h_lb = std::min(h_lb, max_h);

    if (feasible(w_lb, h_lb)) {
        return OutlineFit{w_lb, h_lb, true};
    }

    // Step 1: coupled t-bisection along the line from the lower bounds to MAX. t=1 (MAX) is
    // feasible by construction (decode already legally produced it); t=0 (the lower bounds) was
    // just shown infeasible above, so t=0 is the low end of the search.
    double dw = max_w - w_lb;
    double dh = max_h - h_lb;
    double tol_dim = std::max(dw, dh) * 0.005;  // 0.5% of the larger span, per the brief.
    double t_lo = 0.0;
    double t_hi = 1.0;
    for (int i = 0; i < max_iterations; ++i) {
        if ((t_hi - t_lo) * std::max(dw, dh) < tol_dim) break;
        double t_mid = 0.5 * (t_lo + t_hi);
        double w_mid = w_lb + t_mid * dw;
        double h_mid = h_lb + t_mid * dh;
        if (feasible(w_mid, h_mid)) {
            t_hi = t_mid;
        } else {
            t_lo = t_mid;
        }
    }
    double w = std::min(max_w, w_lb + t_hi * dw);
    double h = std::min(max_h, h_lb + t_hi * dh);
    // t_hi's candidate is feasible by loop invariant (t=1 always feasible, t_hi only tightens
    // from 1 downward); re-verify defensively in case of floating-point edge effects at the cap.
    if (!feasible(w, h)) {
        w = max_w;
        h = max_h;
    }

    // Step 2: per-axis refinement -- bisect W down alone holding H fixed at the point just found.
    {
        double lo = w_lb;
        double hi = w;
        double tol_w = (hi - lo) > 0.0 ? std::max(hi * 0.005, kEps) : kEps;
        for (int i = 0; i < max_iterations && (hi - lo) >= tol_w; ++i) {
            double mid = 0.5 * (lo + hi);
            if (feasible(mid, h)) {
                hi = mid;
            } else {
                lo = mid;
            }
        }
        w = hi;
    }

    // Step 3: per-axis refinement -- bisect H down alone holding the refined W fixed.
    {
        double lo = h_lb;
        double hi = h;
        double tol_h = (hi - lo) > 0.0 ? std::max(hi * 0.005, kEps) : kEps;
        for (int i = 0; i < max_iterations && (hi - lo) >= tol_h; ++i) {
            double mid = 0.5 * (lo + hi);
            if (feasible(w, mid)) {
                hi = mid;
            } else {
                lo = mid;
            }
        }
        h = hi;
    }

    if (!feasible(w, h)) {
        // Should not happen (hi bounds are maintained feasible throughout), but never commit an
        // infeasible trial -- fall back to MAX, which is always feasible.
        return OutlineFit{max_w, max_h, feasible(max_w, max_h)};
    }
    return OutlineFit{w, h, true};
}

}  // namespace

OutlineFit shrinkWrapEstimate(const Problem& problem, const Placement& placement) {
    double w_lb = 0.0;
    double h_lb = 0.0;
    lowerBoundNeeds(problem, placement, &w_lb, &h_lb);

    auto feasible = [&](double w, double h) { return edgeBlocksFeasibleAt(problem, placement, w, h); };
    OutlineFit fit = bisectShrinkWrap(w_lb, h_lb, problem.outline_w, problem.outline_h, 12, feasible);
    if (!fit.legal) {
        return OutlineFit{problem.outline_w, problem.outline_h, false};
    }
    return fit;
}

OutlineFit shrinkWrapOutline(const Problem& problem, Placement& placement, int max_grow_steps) {
    double w_lb = 0.0;
    double h_lb = 0.0;
    lowerBoundNeeds(problem, placement, &w_lb, &h_lb);

    // Re-legalizes edge blocks against a scoped Problem at (w,h) and reports whether the result is
    // fully legal (containment + no overlap + edge-location rules), per design §1.2.3-1.2.4. This
    // is the exact predicate `bisectShrinkWrap` probes with; it does not mutate `placement` --
    // callers must re-run it once more at the winning (w,h) to commit the trial (below), matching
    // T3's "never commit an infeasible midpoint" contract.
    auto feasible = [&](double w, double h) {
        Problem scoped = problem;
        scoped.outline_w = w;
        scoped.outline_h = h;

        Placement trial = placement;
        for (std::size_t i = 0; i < problem.blocks.size(); ++i) {
            if (isEdge(problem.blocks[i].type)) trial.rects[i] = dimensionFor(problem.blocks[i], 1.0);
        }
        legalizeEdgeBlocks(scoped, trial);
        return placementLegalAt(scoped, trial, w, h);
    };

    OutlineFit fit = bisectShrinkWrap(w_lb, h_lb, problem.outline_w, problem.outline_h, max_grow_steps, feasible);
    double w = fit.legal ? fit.w : problem.outline_w;
    double h = fit.legal ? fit.h : problem.outline_h;

    // Commit: re-run the legalization once more at the winning (w,h) -- the FEASIBLE trial -- and
    // stamp its edge rects + declared outline into `placement`. Never commits from an infeasible
    // midpoint (design §1.2.4): `feasible` already re-verified `(w,h)` above (or it is the MAX
    // fallback, always legal since it reproduces decode's own legalization).
    Problem scoped = problem;
    scoped.outline_w = w;
    scoped.outline_h = h;
    Placement trial = placement;
    for (std::size_t i = 0; i < problem.blocks.size(); ++i) {
        if (isEdge(problem.blocks[i].type)) trial.rects[i] = dimensionFor(problem.blocks[i], 1.0);
    }
    legalizeEdgeBlocks(scoped, trial);
    bool legal = placementLegalAt(scoped, trial, w, h);
    for (std::size_t i = 0; i < problem.blocks.size(); ++i) {
        if (isEdge(problem.blocks[i].type)) placement.rects[i] = trial.rects[i];
    }
    placement.outline_w = w;
    placement.outline_h = h;
    return OutlineFit{w, h, legal};
}

}  // namespace efgr
