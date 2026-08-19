#include "lns_polish.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <numeric>
#include <utility>

#include "placement_engine.hpp"

namespace efgr {
namespace {

bool isInGroup(int block, const std::vector<int>& group) {
    return std::find(group.begin(), group.end(), block) != group.end();
}

// A3's lexicographic proxy objective: `p` = bundle-weighted facing-edge gap (C1a's L1b term,
// eval.cpp:314-316 / c1-design-20260817.md's gapx/gapy), `s` = bundle-weighted center Manhattan
// (the same secondary tie-break wl_redistribute.cpp's axisTarget already uses, via
// wireMedianTarget's underlying quantity). Additive; compared via objLess below (kEps-banded
// lexicographic: primary decides unless within kEps, then secondary decides).
struct Obj {
    double p = 0.0;
    double s = 0.0;
};

Obj operator+(const Obj& a, const Obj& b) { return Obj{a.p + b.p, a.s + b.s}; }

bool objLess(const Obj& a, const Obj& b) {
    if (a.p < b.p - kEps) return true;
    if (b.p < a.p - kEps) return false;
    return a.s < b.s - kEps;
}

// Sum, over every bundle touching `block` (either endpoint), of `nets` -- A7's "bundle-less blocks
// never selected" test and A3's DFS member ordering both key off this. Self-loop bundles
// (source==target) are skipped (A3 "skip source==target bundles").
int totalNets(const Problem& problem, int block) {
    int total = 0;
    for (const auto& b : problem.bundles) {
        if (b.source == b.target) continue;
        if (b.source == block || b.target == block) total += b.nets;
    }
    return total;
}

// A1: Sum of bundle nets connecting `candidate` to any CURRENT member of `group` -- the group-growth
// connectivity score.
int connectionToGroup(const Problem& problem, int candidate, const std::vector<int>& group) {
    int total = 0;
    for (const auto& b : problem.bundles) {
        if (b.source == b.target) continue;
        int other = -1;
        if (b.source == candidate) {
            other = b.target;
        } else if (b.target == candidate) {
            other = b.source;
        } else {
            continue;
        }
        if (isInGroup(other, group)) total += b.nets;
    }
    return total;
}

// A1: real routed wirelength attributed to `block` -- Sum over `routing.routes` with source or
// target == block of Route.wirelength (route_geometry.hpp:118-121's accumulatePathUsage output,
// already nets * Sum|delta guiding point|).
double wlShare(const RoutingResult& routing, int block) {
    double total = 0.0;
    for (const auto& r : routing.routes) {
        if (r.source == block || r.target == block) total += r.wirelength;
    }
    return total;
}

bool hasOpenRoute(const RoutingResult& routing, int block) {
    for (const auto& r : routing.routes) {
        if ((r.source == block || r.target == block) && r.open) return true;
    }
    return false;
}

// A3: bundle-weighted (gapx+gapy, centerManhattan) contribution between two specific rects for every
// bundle directly connecting `blockA`/`blockB` (either direction; self-loop bundles skipped).
Obj pairTermBetween(const Problem& problem, int blockA, const Rect& rectA, int blockB, const Rect& rectB) {
    Obj total;
    for (const auto& b : problem.bundles) {
        if (b.source == b.target) continue;
        bool match = (b.source == blockA && b.target == blockB) || (b.source == blockB && b.target == blockA);
        if (!match) continue;
        double gapx = std::max(0.0, std::max(rectA.x - rectB.right(), rectB.x - rectA.right()));
        double gapy = std::max(0.0, std::max(rectA.y - rectB.top(), rectB.y - rectA.top()));
        total.p += b.nets * (gapx + gapy);
        total.s += b.nets * manhattan(rectA.cx(), rectA.cy(), rectB.cx(), rectB.cy());
    }
    return total;
}

// A3's own[i][c]: same bundle-term sum as pairTermBetween, but over every bundle touching `blockI`
// whose OTHER endpoint is a FIXED (non-group) block, read at that partner's CURRENT `base` position
// -- fixed partners never move during this group's B&B, so their contribution to `blockI`'s own term
// is a plain per-candidate function, no dependency on any other group member's choice. Partners that
// are themselves group members are excluded here (their contribution is a pairTerm, handled
// separately, so it is not double-counted).
Obj ownTerm(const Problem& problem, const Placement& base, const std::vector<int>& group, int blockI,
           const Rect& rectI) {
    Obj total;
    for (const auto& b : problem.bundles) {
        if (b.source == b.target) continue;
        int partner = -1;
        if (b.source == blockI) {
            partner = b.target;
        } else if (b.target == blockI) {
            partner = b.source;
        } else {
            continue;
        }
        if (isInGroup(partner, group)) continue;  // handled by pairTermBetween instead
        const Rect& rectPartner = base.rects[static_cast<std::size_t>(partner)];
        double gapx = std::max(0.0, std::max(rectI.x - rectPartner.right(), rectPartner.x - rectI.right()));
        double gapy = std::max(0.0, std::max(rectI.y - rectPartner.top(), rectPartner.y - rectI.top()));
        total.p += b.nets * (gapx + gapy);
        total.s += b.nets * manhattan(rectI.cx(), rectI.cy(), rectPartner.cx(), rectPartner.cy());
    }
    return total;
}

// C3 (A7 failure-mode belt-and-braces, mirrors wl_redistribute.cpp's own local
// `placementLegalWithin`, itself mirroring respace.cpp's `rectsLegalWithinOutline`): containment in
// [0,w]x[0,h] + pairwise non-overlap over ALL rects. Reproduced locally per this module's own
// "new module, links standalone with only placement_engine.cpp" scope.
bool placementLegalWithin(const Placement& placement, double w, double h) {
    for (std::size_t i = 0; i < placement.rects.size(); ++i) {
        const Rect& r = placement.rects[i];
        if (r.x < -kEps || r.y < -kEps || r.right() > w + kEps || r.top() > h + kEps) return false;
        for (std::size_t j = i + 1; j < placement.rects.size(); ++j) {
            if (overlaps(r, placement.rects[j])) return false;
        }
    }
    return true;
}

// A2: clip every value into [lo,hi], sort ascending, dedupe within kEps.
std::vector<double> sortUniqueClamped(std::vector<double> values, double lo, double hi) {
    for (double& v : values) v = std::min(hi, std::max(lo, v));
    std::sort(values.begin(), values.end());
    std::vector<double> out;
    out.reserve(values.size());
    for (double v : values) {
        if (out.empty() || v - out.back() > kEps) out.push_back(v);
    }
    return out;
}

// A2's raw (uncapped) legal candidate grid for one member, as Rects (member's own frozen w/h,
// A7). lnsCandidatePositions (public) is a thin (x,y) projection of this.
std::vector<Rect> rawCandidateRects(const Problem& problem, const Placement& base, const std::vector<int>& group,
                                    int member) {
    const double W = base.outline_w;
    const double H = base.outline_h;
    const Rect& mine = base.rects[static_cast<std::size_t>(member)];
    const double w_i = mine.w;
    const double h_i = mine.h;
    if (w_i > W + kEps || h_i > H + kEps) return {};  // A7: no candidates -> caller drops this member

    auto isFixed = [&](int j) { return j != member && !isInGroup(j, group); };

    std::vector<double> xs = {0.0, W - w_i};
    std::vector<double> ys = {0.0, H - h_i};
    for (std::size_t j = 0; j < base.rects.size(); ++j) {
        if (!isFixed(static_cast<int>(j))) continue;
        const Rect& r = base.rects[j];
        xs.push_back(r.right());
        xs.push_back(r.x - w_i);
        ys.push_back(r.top());
        ys.push_back(r.y - h_i);
    }
    std::pair<double, double> median = wireMedianTarget(problem, base, member);
    xs.push_back(static_cast<double>(std::llround(median.first - w_i / 2.0)));
    ys.push_back(static_cast<double>(std::llround(median.second - h_i / 2.0)));

    std::vector<double> Xs = sortUniqueClamped(std::move(xs), 0.0, std::max(0.0, W - w_i));
    std::vector<double> Ys = sortUniqueClamped(std::move(ys), 0.0, std::max(0.0, H - h_i));

    std::vector<Rect> raw;
    raw.reserve(Xs.size() * Ys.size());
    for (double x : Xs) {
        for (double y : Ys) {
            Rect cand{x, y, w_i, h_i};
            bool legal = true;
            for (std::size_t j = 0; j < base.rects.size(); ++j) {
                if (!isFixed(static_cast<int>(j))) continue;
                if (overlaps(cand, base.rects[j])) {
                    legal = false;
                    break;
                }
            }
            if (legal) raw.push_back(cand);
        }
    }
    // A2: current position force-included regardless of the filter above.
    bool have_current = false;
    for (const auto& r : raw) {
        if (std::abs(r.x - mine.x) <= kEps && std::abs(r.y - mine.y) <= kEps) {
            have_current = true;
            break;
        }
    }
    if (!have_current) raw.push_back(Rect{mine.x, mine.y, w_i, h_i});
    return raw;
}

// A3's node-capped DFS, operating on the CAPPED, DFS-ordered per-member candidate tables built by
// lnsPolishIterates below.
struct LnsSearch {
    const Problem& problem;
    const std::vector<int>& dGroup;                                  // block indices, DFS order
    const std::vector<std::vector<Rect>>& dCapped;                   // [depth][candidate]
    const std::vector<std::vector<Obj>>& dOwn;                       // [depth][candidate]
    const std::vector<std::vector<std::vector<Obj>>>& crossMin;      // [depth][candidate][otherDepth]
    const std::vector<std::vector<Obj>>& pairMin;                    // [depth][otherDepth]
    const std::vector<Obj>& minOwn;                                  // [depth]
    int node_budget;
    LnsPolishStats& stats;
    bool budget_exceeded = false;
    Obj best_obj;
    std::vector<int> chosen;
    std::vector<std::vector<int>> incumbent_choices;  // up to 3, chronological, earliest first

    void recordIfBetter(const Obj& complete) {
        if (!objLess(complete, best_obj)) return;
        best_obj = complete;
        if (incumbent_choices.empty() || incumbent_choices.back() != chosen) {
            incumbent_choices.push_back(chosen);
            if (incumbent_choices.size() > 3) incumbent_choices.erase(incumbent_choices.begin());
        }
    }

    void dfs(std::size_t depth, Obj partial) {
        if (budget_exceeded) return;
        const std::size_t k = dGroup.size();
        if (depth == k) {
            recordIfBetter(partial);
            return;
        }

        // A3 lower bound: decided members' exact contribution (`partial`) + undecided members' own
        // minimum + decided-x-undecided cross minimum + undecided-x-undecided pair minimum.
        Obj bound;
        for (std::size_t i = depth; i < k; ++i) bound = bound + minOwn[i];
        for (std::size_t i = 0; i < depth; ++i) {
            const auto& row = crossMin[i][static_cast<std::size_t>(chosen[i])];
            for (std::size_t j = depth; j < k; ++j) bound = bound + row[j];
        }
        for (std::size_t i = depth; i < k; ++i) {
            for (std::size_t j = i + 1; j < k; ++j) bound = bound + pairMin[i][j];
        }
        Obj lb = partial + bound;
        if (!objLess(lb, best_obj)) return;  // prune: LB >= incumbent

        for (std::size_t c = 0; c < dCapped[depth].size(); ++c) {
            ++stats.nodes_expanded;
            if (stats.nodes_expanded > node_budget) {
                budget_exceeded = true;
                return;
            }
            const Rect& cand = dCapped[depth][c];
            bool feasible = true;
            for (std::size_t i = 0; i < depth; ++i) {
                if (overlaps(cand, dCapped[i][static_cast<std::size_t>(chosen[i])])) {
                    feasible = false;
                    break;
                }
            }
            if (!feasible) continue;

            Obj step = dOwn[depth][c];
            for (std::size_t i = 0; i < depth; ++i) {
                step = step + pairTermBetween(problem, dGroup[i], dCapped[i][static_cast<std::size_t>(chosen[i])],
                                              dGroup[depth], cand);
            }
            chosen[depth] = static_cast<int>(c);
            dfs(depth + 1, partial + step);
            if (budget_exceeded) return;
        }
    }
};

}  // namespace

std::vector<int> lnsSelectGroup(const Problem& problem, const RoutingResult& routing, int round, int group_size) {
    std::vector<int> connected;
    for (std::size_t i = 0; i < problem.blocks.size(); ++i) {
        int idx = static_cast<int>(i);
        if (isAnchoredEdge(problem, i)) continue;
        if (totalNets(problem, idx) == 0) continue;  // A7: bundle-less blocks never selected
        connected.push_back(idx);
    }
    if (connected.size() < 2) return {};

    std::vector<int> ranked = connected;
    std::sort(ranked.begin(), ranked.end(), [&](int a, int b) {
        bool oa = hasOpenRoute(routing, a);
        bool ob = hasOpenRoute(routing, b);
        if (oa != ob) return oa;  // open-route blocks ranked first
        double wa = wlShare(routing, a);
        double wb = wlShare(routing, b);
        if (std::abs(wa - wb) > kEps) return wa > wb;
        return a < b;
    });

    if (round < 0 || static_cast<std::size_t>(round) >= ranked.size()) return {};
    int seed = ranked[static_cast<std::size_t>(round)];

    std::vector<int> group = {seed};
    std::size_t target = std::min(static_cast<std::size_t>(std::max(0, group_size)), connected.size());
    while (group.size() < target) {
        int best = -1;
        int best_score = -1;
        double best_wl = 0.0;
        for (int cand : connected) {
            if (isInGroup(cand, group)) continue;
            int score = connectionToGroup(problem, cand, group);
            double wl = wlShare(routing, cand);
            bool take = false;
            if (best == -1 || score > best_score) {
                take = true;
            } else if (score == best_score) {
                if (wl > best_wl + kEps) {
                    take = true;
                } else if (std::abs(wl - best_wl) <= kEps && cand < best) {
                    take = true;
                }
            }
            if (take) {
                best = cand;
                best_score = score;
                best_wl = wl;
            }
        }
        if (best == -1) break;  // defensive: connected exhausted
        group.push_back(best);
    }
    return group;
}

std::vector<std::pair<double, double>> lnsCandidatePositions(const Problem& problem, const Placement& base,
                                                              const std::vector<int>& group, int member) {
    if (base.outline_w <= kEps || base.outline_h <= kEps) return {};
    std::vector<Rect> rects = rawCandidateRects(problem, base, group, member);
    std::vector<std::pair<double, double>> out;
    out.reserve(rects.size());
    for (const auto& r : rects) out.emplace_back(r.x, r.y);
    return out;
}

std::vector<Placement> lnsPolishIterates(const Problem& problem, const Placement& base,
                                         const std::vector<int>& group_in, LnsPolishStats* stats,
                                         int node_budget) {
    LnsPolishStats local_stats;
    if (stats == nullptr) stats = &local_stats;
    *stats = LnsPolishStats{};

    if (base.outline_w <= kEps || base.outline_h <= kEps) return {};  // stamp unset
    if (!placementLegalWithin(base, base.outline_w, base.outline_h)) return {};  // base illegal

    // A7: drop bundle-less members and members whose own w/h exceeds the outline; a dropped member
    // becomes a plain fixed obstacle for the survivors (it is no longer "in group").
    std::vector<int> group;
    for (int b : group_in) {
        if (b < 0 || static_cast<std::size_t>(b) >= base.rects.size()) continue;
        if (totalNets(problem, b) == 0) continue;
        const Rect& r = base.rects[static_cast<std::size_t>(b)];
        if (r.w > base.outline_w + kEps || r.h > base.outline_h + kEps) continue;
        if (isInGroup(b, group)) continue;  // defensive: no duplicate members
        group.push_back(b);
    }
    if (group.size() < 2) return {};

    const std::size_t k = group.size();
    std::vector<std::vector<Rect>> raw(k);
    for (std::size_t i = 0; i < k; ++i) raw[i] = rawCandidateRects(problem, base, group, group[i]);

    // A2: rank each member's raw candidates by own[i][c] + Sum_j crossMin[i][c][j] (against the
    // OTHER members' raw sets) ascending, tie x then y; cap to kLnsCandCap, force-including the
    // current position if the cap would otherwise drop it.
    std::vector<std::vector<Rect>> capped(k);
    std::vector<std::vector<Obj>> capped_own(k);
    for (std::size_t i = 0; i < k; ++i) {
        std::vector<Obj> own_raw(raw[i].size());
        for (std::size_t c = 0; c < raw[i].size(); ++c) own_raw[c] = ownTerm(problem, base, group, group[i], raw[i][c]);

        std::vector<Obj> score(raw[i].size());
        for (std::size_t c = 0; c < raw[i].size(); ++c) {
            Obj s = own_raw[c];
            for (std::size_t j = 0; j < k; ++j) {
                if (j == i) continue;
                Obj best;
                bool have = false;
                for (std::size_t d = 0; d < raw[j].size(); ++d) {
                    Obj pt = pairTermBetween(problem, group[i], raw[i][c], group[j], raw[j][d]);
                    if (!have || objLess(pt, best)) {
                        best = pt;
                        have = true;
                    }
                }
                if (have) s = s + best;
            }
            score[c] = s;
        }

        std::vector<std::size_t> order(raw[i].size());
        std::iota(order.begin(), order.end(), std::size_t{0});
        std::sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) {
            if (objLess(score[a], score[b])) return true;
            if (objLess(score[b], score[a])) return false;
            if (std::abs(raw[i][a].x - raw[i][b].x) > kEps) return raw[i][a].x < raw[i][b].x;
            return raw[i][a].y < raw[i][b].y;
        });

        const double cur_x = base.rects[static_cast<std::size_t>(group[i])].x;
        const double cur_y = base.rects[static_cast<std::size_t>(group[i])].y;
        std::size_t take = std::min(order.size(), static_cast<std::size_t>(kLnsCandCap));
        bool have_current = false;
        for (std::size_t t = 0; t < take; ++t) {
            std::size_t c = order[t];
            capped[i].push_back(raw[i][c]);
            capped_own[i].push_back(own_raw[c]);
            if (std::abs(raw[i][c].x - cur_x) <= kEps && std::abs(raw[i][c].y - cur_y) <= kEps) have_current = true;
        }
        if (!have_current) {
            for (std::size_t c = 0; c < raw[i].size(); ++c) {
                if (std::abs(raw[i][c].x - cur_x) <= kEps && std::abs(raw[i][c].y - cur_y) <= kEps) {
                    capped[i].push_back(raw[i][c]);
                    capped_own[i].push_back(own_raw[c]);
                    break;
                }
            }
        }
    }

    // A3: DFS member order = own total bundle nets descending, ties by index ascending.
    std::vector<std::size_t> dfs_order(k);
    std::iota(dfs_order.begin(), dfs_order.end(), std::size_t{0});
    std::sort(dfs_order.begin(), dfs_order.end(), [&](std::size_t a, std::size_t b) {
        int na = totalNets(problem, group[a]);
        int nb = totalNets(problem, group[b]);
        if (na != nb) return na > nb;
        return group[a] < group[b];
    });

    std::vector<int> d_group(k);
    std::vector<std::vector<Rect>> d_capped(k);
    std::vector<std::vector<Obj>> d_own(k);
    for (std::size_t pos = 0; pos < k; ++pos) {
        std::size_t src = dfs_order[pos];
        d_group[pos] = group[src];
        d_capped[pos] = std::move(capped[src]);
        d_own[pos] = std::move(capped_own[src]);
    }

    // A3 precompute (on the CAPPED, DFS-ordered sets): minOwn[i], crossMin[i][c][j], pairMin[i][j].
    std::vector<Obj> min_own(k);
    for (std::size_t i = 0; i < k; ++i) {
        Obj m = d_own[i][0];
        for (std::size_t c = 1; c < d_own[i].size(); ++c) {
            if (objLess(d_own[i][c], m)) m = d_own[i][c];
        }
        min_own[i] = m;
    }

    std::vector<std::vector<std::vector<Obj>>> cross_min(k);
    for (std::size_t i = 0; i < k; ++i) {
        cross_min[i].assign(d_capped[i].size(), std::vector<Obj>(k));
        for (std::size_t c = 0; c < d_capped[i].size(); ++c) {
            for (std::size_t j = 0; j < k; ++j) {
                if (j == i) continue;
                Obj best;
                bool have = false;
                for (std::size_t d = 0; d < d_capped[j].size(); ++d) {
                    Obj pt = pairTermBetween(problem, d_group[i], d_capped[i][c], d_group[j], d_capped[j][d]);
                    if (!have || objLess(pt, best)) {
                        best = pt;
                        have = true;
                    }
                }
                cross_min[i][c][j] = have ? best : Obj{};
            }
        }
    }

    std::vector<std::vector<Obj>> pair_min(k, std::vector<Obj>(k));
    for (std::size_t i = 0; i < k; ++i) {
        for (std::size_t j = i + 1; j < k; ++j) {
            Obj best;
            bool have = false;
            for (std::size_t c = 0; c < d_capped[i].size(); ++c) {
                for (std::size_t d = 0; d < d_capped[j].size(); ++d) {
                    Obj pt = pairTermBetween(problem, d_group[i], d_capped[i][c], d_group[j], d_capped[j][d]);
                    if (!have || objLess(pt, best)) {
                        best = pt;
                        have = true;
                    }
                }
            }
            pair_min[i][j] = have ? best : Obj{};
            pair_min[j][i] = pair_min[i][j];
        }
    }

    // A3: incumbent initialised to the CURRENT assignment -- guaranteed present in every member's
    // capped set (A2's force-include).
    std::vector<int> status_quo(k, -1);
    for (std::size_t i = 0; i < k; ++i) {
        double cur_x = base.rects[static_cast<std::size_t>(d_group[i])].x;
        double cur_y = base.rects[static_cast<std::size_t>(d_group[i])].y;
        for (std::size_t c = 0; c < d_capped[i].size(); ++c) {
            if (std::abs(d_capped[i][c].x - cur_x) <= kEps && std::abs(d_capped[i][c].y - cur_y) <= kEps) {
                status_quo[i] = static_cast<int>(c);
                break;
            }
        }
    }
    Obj status_quo_obj;
    for (std::size_t i = 0; i < k; ++i) status_quo_obj = status_quo_obj + d_own[i][static_cast<std::size_t>(status_quo[i])];
    for (std::size_t i = 0; i < k; ++i) {
        for (std::size_t j = i + 1; j < k; ++j) {
            status_quo_obj = status_quo_obj + pairTermBetween(problem, d_group[i],
                                                               d_capped[i][static_cast<std::size_t>(status_quo[i])],
                                                               d_group[j],
                                                               d_capped[j][static_cast<std::size_t>(status_quo[j])]);
        }
    }

    LnsSearch search{problem,     d_group,   d_capped, d_own,  cross_min,
                     pair_min,    min_own,   node_budget, *stats, /*budget_exceeded=*/false,
                     status_quo_obj, /*chosen=*/{}, /*incumbent_choices=*/{}};
    search.chosen.assign(k, -1);
    search.dfs(0, Obj{});

    std::vector<Placement> iterates;
    iterates.reserve(search.incumbent_choices.size());
    for (const auto& choice : search.incumbent_choices) {
        Placement p = base;
        for (std::size_t i = 0; i < k; ++i) {
            p.rects[static_cast<std::size_t>(d_group[i])] = d_capped[i][static_cast<std::size_t>(choice[i])];
        }
        iterates.push_back(std::move(p));
    }
    return iterates;
}

}  // namespace efgr
