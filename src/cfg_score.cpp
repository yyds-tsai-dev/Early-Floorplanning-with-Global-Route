#include "cfg_score.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <fstream>
#include <limits>
#include <sstream>
#include <string>
#include <unordered_map>

#include "ft.hpp"

namespace efgr {

namespace {

// Official cost-function parameters: unchanged in magnitude across the V5 recalculation (still
// byte-verified from every alpha report.json's `cost_breakdown` block, beta-task-2-report.md
// Section 1). What V5 changes is the wirelength guiding-point rule, the channel-demand accounting,
// and it confirms the overflow-tail formula exact (see docs/research/2026-07-28-alpha-v5-cost-
// recalculation-analysis.md).
constexpr double kBetaLin = 20.0;    // linear term coefficient
constexpr double kBetaTail = 1.5;    // tail term coefficient (R > threshold)
constexpr double kThreshold = 0.05;  // overflow-rate threshold
constexpr double kCapPerUm = 25.0;   // wire density limit: 25 nets / um
constexpr double kBetaFt = 3.0;      // FT aggregate-slack penalty coefficient (analysis doc section 1.4)
// G4 (edge-LOCATION soft-anchor exploratory lever, analysis doc section 1 item 4 / section 6):
// official per-um edge-LOCATION violation-distance coefficient. Locked copy of route_cost.hpp's
// kGammaEdge (oracle-duplication policy, same as this file's other constants above) -- charged
// UNCONDITIONALLY below (officialScoreCfg), unlike eval.cpp's flag-gated solver-side term.
constexpr double kGammaEdge = 1.0;

// V5 wirelength chain (docs/research/2026-07-28-alpha-v5-cost-recalculation-analysis.md, section
// 1.1): replaces the retired CENTER-anchored guiding-point chain. Two guiding-point kinds, walked
// pairwise over a Path's RAW (object, edge) step list (no more dedup of consecutive identical
// objects -- the edge values themselves now drive corner insertion):
//   * crossGuidingPoint(a, b) -- CROSS point between two DIFFERENT consecutive objects. The shared
//     border is still picked by MINIMUM gap (same classification shape as the retired guidingPoint,
//     robust to the .cfg's 3-decimal rounding -- abutting edges can differ by up to ~0.001 um);
//     the ACROSS-border coordinate stays the average of the two near edges, but the ALONG-border
//     coordinate is now the midpoint of the two rects' OVERLAP interval (not their centers).
//   * cornerGuidingPoint(r, e_in, e_out, point) -- CORNER point for an internal turn within the
//     SAME object (a traversal's entry/exit edge pair). Returns false (no point) when e_in/e_out
//     share an axis (edges 1/3 are x-fixed, 2/4 are y-fixed) -- a straight pass-through or a
//     same-edge U-turn (e.g. "CH3 3 CH3 3") inserts nothing.
// Verified EXACT (<=5e-6 abs) against the five official example-path subtotals; see
// test/regression_cfg_score.cpp.
std::pair<double, double> crossGuidingPoint(const Rect& a, const Rect& b) {
    const double g_ar_bx = std::abs(a.right() - b.x);  // a.right meets b.left  (vertical border)
    const double g_br_ax = std::abs(b.right() - a.x);  // b.right meets a.left  (vertical border)
    const double g_at_by = std::abs(a.top() - b.y);    // a.top   meets b.bottom(horizontal border)
    const double g_bt_ay = std::abs(b.top() - a.y);    // b.top   meets a.bottom(horizontal border)
    const double min_vertical = std::min(g_ar_bx, g_br_ax);
    const double min_horizontal = std::min(g_at_by, g_bt_ay);
    if (min_vertical <= min_horizontal) {
        const double x = (g_ar_bx <= g_br_ax) ? 0.5 * (a.right() + b.x) : 0.5 * (b.right() + a.x);
        const double y = 0.5 * (std::max(a.y, b.y) + std::min(a.top(), b.top()));
        return {x, y};
    }
    const double y = (g_at_by <= g_bt_ay) ? 0.5 * (a.top() + b.y) : 0.5 * (b.top() + a.y);
    const double x = 0.5 * (std::max(a.x, b.x) + std::min(a.right(), b.right()));
    return {x, y};
}

// See crossGuidingPoint's doc comment above. `edge` axis: 1/3 = x-fixed (vertical edges: 1=left,
// 3=right), 2/4 = y-fixed (horizontal edges: 2=top, 4=bottom).
bool cornerGuidingPoint(const Rect& r, int e_in, int e_out, std::pair<double, double>& point) {
    const bool in_is_x = (e_in == 1 || e_in == 3);
    const bool out_is_x = (e_out == 1 || e_out == 3);
    if (in_is_x == out_is_x) return false;  // same axis: straight pass-through / U-turn -> no point
    const int ex = in_is_x ? e_in : e_out;
    const int ey = in_is_x ? e_out : e_in;
    point = {(ex == 1) ? r.x : r.right(), (ey == 2) ? r.top() : r.y};
    return true;
}

// A Path step resolved to its object's rect (block or channel); unresolved (unknown) names are
// filtered out defensively before chaining, same as the retired dedup loop used to do.
struct ResolvedStep {
    std::string name;
    int edge;
    Rect rect;
};

// G4 (edge-LOCATION soft-anchor exploratory lever, analysis doc section 1 item 4 / section 6): this
// oracle's OWN independent copy of route_cost.hpp's edgeLocationViolationDistance geometry (oracle-
// duplication policy, same twin relationship as crossGuidingPoint/cornerGuidingPoint above vs
// route_geometry.hpp) -- deliberately re-derived here rather than including route_cost.hpp, so this
// file stays independent ground truth. See route_cost.hpp's copy for the full doc comment (semantics,
// the OR-vs-AND single/multi-side split, and the CAVEAT that the official per-violation distance
// metric has never been observed nonzero on any alpha-test case -- this is a best-guess, not a
// verified reproduction).
char edgeLocSide(const std::string& loc) { return loc.empty() ? '\0' : loc[0]; }

std::pair<double, double> edgeLocSegment(double outline_w, double outline_h, const std::string& loc) {
    double third_w = outline_w / 3.0;
    double third_h = outline_h / 3.0;
    if (loc == "TL" || loc == "BL") return {0.0, third_w};
    if (loc == "TM" || loc == "BM") return {third_w, 2.0 * third_w};
    if (loc == "TR" || loc == "BR") return {2.0 * third_w, outline_w};
    if (loc == "LB" || loc == "RB") return {0.0, third_h};
    if (loc == "LM" || loc == "RM") return {third_h, 2.0 * third_h};
    if (loc == "LT" || loc == "RT") return {2.0 * third_h, outline_h};
    return {0.0, 0.0};
}

bool edgeLocPositiveOverlap(double a0, double a1, double b0, double b1) {
    return std::min(a1, b1) - std::max(a0, b0) > kEps;
}

bool edgeLocSatisfied(const Rect& r, double outline_w, double outline_h, const std::string& loc) {
    char side = edgeLocSide(loc);
    auto [lo, hi] = edgeLocSegment(outline_w, outline_h, loc);
    if (side == 'T') return std::abs(r.top() - outline_h) < kEps && edgeLocPositiveOverlap(r.x, r.right(), lo, hi);
    if (side == 'B') return std::abs(r.y) < kEps && edgeLocPositiveOverlap(r.x, r.right(), lo, hi);
    if (side == 'L') return std::abs(r.x) < kEps && edgeLocPositiveOverlap(r.y, r.top(), lo, hi);
    if (side == 'R') return std::abs(r.right() - outline_w) < kEps && edgeLocPositiveOverlap(r.y, r.top(), lo, hi);
    return true;
}

double edgeLocRangeGap(double p0, double p1, double lo, double hi) {
    if (p1 <= lo) return lo - p1;
    if (p0 >= hi) return hi - p0;
    return 0.0;
}

std::pair<double, double> edgeLocDelta(const Rect& r, double outline_w, double outline_h, const std::string& loc) {
    char side = edgeLocSide(loc);
    auto [lo, hi] = edgeLocSegment(outline_w, outline_h, loc);
    double dx = 0.0, dy = 0.0;
    if (side == 'T' || side == 'B') {
        dy = (side == 'T') ? (outline_h - r.h - r.y) : (0.0 - r.y);
        dx = edgeLocRangeGap(r.x, r.right(), lo, hi);
    } else if (side == 'L' || side == 'R') {
        dx = (side == 'L') ? (0.0 - r.x) : (outline_w - r.w - r.x);
        dy = edgeLocRangeGap(r.y, r.top(), lo, hi);
    }
    return {dx, dy};
}

double edgeLocationViolationDistance(const Rect& r, const Problem& scoped, const std::vector<std::string>& locs) {
    if (locs.empty()) return 0.0;
    std::vector<char> sides;
    for (const auto& loc : locs) {
        char side = edgeLocSide(loc);
        if (std::find(sides.begin(), sides.end(), side) == sides.end()) sides.push_back(side);
    }
    const bool single_side = sides.size() == 1;

    if (single_side) {
        double best = std::numeric_limits<double>::infinity();
        for (const auto& loc : locs) {
            if (edgeLocSatisfied(r, scoped.outline_w, scoped.outline_h, loc)) return 0.0;
            auto [dx, dy] = edgeLocDelta(r, scoped.outline_w, scoped.outline_h, loc);
            best = std::min(best, std::abs(dx) + std::abs(dy));
        }
        return best;
    }

    bool all_satisfied = true;
    for (const auto& loc : locs) {
        if (!edgeLocSatisfied(r, scoped.outline_w, scoped.outline_h, loc)) { all_satisfied = false; break; }
    }
    if (all_satisfied) return 0.0;

    double dx = 0.0, dy = 0.0;
    for (const auto& loc : locs) {
        auto [ldx, ldy] = edgeLocDelta(r, scoped.outline_w, scoped.outline_h, loc);
        if (std::abs(ldx) > std::abs(dx)) dx = ldx;
        if (std::abs(ldy) > std::abs(dy)) dy = ldy;
    }
    return std::abs(dx) + std::abs(dy);
}

}  // namespace

// V5 overflow-penalty tail (docs/research/2026-07-28-alpha-v5-cost-recalculation-analysis.md,
// section 1.3): EXACT official piecewise formula (not an approximation) -- reproduces the
// historical R>threshold datapoint (case1's pre-V5 report.json penalty) to 1e-9 rel. A free
// function (not file-local) so it can be tested directly; see test/regression_cfg_score.cpp.
double officialOverflowPenalty(double rate, double outline_area) {
    if (rate <= kThreshold) return kBetaLin * outline_area * rate;
    return kBetaLin * kThreshold * outline_area + kBetaTail * std::sqrt(rate - kThreshold) * outline_area;
}

CfgSolution parseCfg(const std::string& path) {
    CfgSolution cfg;
    std::ifstream in(path);
    std::string line;
    std::string section;
    while (std::getline(in, line)) {
        line = trim(line);  // strips CR and surrounding whitespace
        if (line.empty()) continue;
        std::istringstream ss(line);
        std::string tok;
        ss >> tok;
        tok = trim(tok);
        if (tok == "OUTLINE") {
            ss >> cfg.outline_w >> cfg.outline_h;
            section.clear();
            continue;
        }
        if (tok == "BLOCK" || tok == "CHANNEL") {
            section = tok;
            continue;
        }
        if (tok == "PATH") {
            std::string rest;
            std::getline(ss, rest);
            rest = trim(rest);
            if (rest.empty()) {          // section header "PATH"
                section = "PATH";
                continue;
            }
            std::istringstream ps(rest);  // data line "PATH <nets> <obj> <edge> ..."
            CfgSolution::Path p;
            ps >> p.nets;
            std::string obj;
            int edge = 0;
            while (ps >> obj >> edge) p.steps.push_back({trim(obj), edge});
            cfg.paths.push_back(std::move(p));
            section = "PATH";
            continue;
        }
        if (tok == "END") {
            section.clear();
            continue;
        }
        if (section == "BLOCK") {
            Rect r;
            ss >> r.x >> r.y >> r.w >> r.h;
            cfg.blocks[tok] = r;
        } else if (section == "CHANNEL") {
            Channel ch;
            ch.name = tok;
            ss >> ch.rect.x >> ch.rect.y >> ch.rect.w >> ch.rect.h;
            cfg.channels.push_back(ch);
        }
    }
    return cfg;
}

OfficialBreakdown officialScoreCfg(const Problem& problem, const CfgSolution& cfg) {
    OfficialBreakdown out;
    out.outline_area = cfg.outline_w * cfg.outline_h;

    // Name lookups. Channels own mutable H/V demand accumulators (V5 per-axis-per-traversal model).
    std::unordered_map<std::string, int> chan_index;
    std::vector<double> hu(cfg.channels.size(), 0.0);
    std::vector<double> vu(cfg.channels.size(), 0.0);
    for (std::size_t k = 0; k < cfg.channels.size(); ++k) chan_index[cfg.channels[k].name] = static_cast<int>(k);

    auto rect_of = [&](const std::string& name, Rect& out_rect) -> bool {
        auto b = cfg.blocks.find(name);
        if (b != cfg.blocks.end()) { out_rect = b->second; return true; }
        auto c = chan_index.find(name);
        if (c != chan_index.end()) { out_rect = cfg.channels[c->second].rect; return true; }
        return false;  // unknown object -> skip (defensive; legal .cfg never hits this)
    };

    // V5 FT crossing-net accrual (analysis doc section 1.4): per-SOFT-block total, accumulated
    // across ALL paths before any FT-tier conversion happens (see the ft_area_sum block below).
    std::vector<double> ft_nets_by_block(problem.blocks.size(), 0.0);

    double wirelength = 0.0;
    for (const CfgSolution::Path& p : cfg.paths) {
        // (1) V5 FT crossing-net accrual: walk this path's steps with the same traversal-pairing
        // shape the channel-demand walk below uses (consecutive same-name steps form one
        // traversal), but over ALL steps, not just channels. commitRoutePath (routing.cpp) emits
        // the route's source/target ONCE each (p.steps.front()/.back()) and every intermediate node
        // TWICE consecutively (entry edge, exit edge), so excluding this path's first and last step
        // isolates exactly the INTERMEDIATE (feedthrough) traversals. A traversal whose object
        // resolves to a SOFT block in `problem` (by name) accrues p.nets ONCE.
        for (std::size_t i = 0; i < p.steps.size();) {
            std::size_t consumed = (i + 1 < p.steps.size() && p.steps[i + 1].first == p.steps[i].first) ? 2 : 1;
            bool is_terminal = (i == 0) || (i + consumed == p.steps.size());
            if (!is_terminal) {
                auto bi = problem.block_index.find(p.steps[i].first);
                if (bi != problem.block_index.end() && isSoft(problem.blocks[bi->second].type))
                    ft_nets_by_block[bi->second] += p.nets;
            }
            i += consumed;
        }

        // (2) V5 channel demand: each channel TRAVERSAL (a consecutive entry/exit step pair on the
        // same channel) charges p.nets ONCE per axis touched by {e_in, e_out} -- a straight
        // pass-through charges one axis once (not each edge separately, unlike the retired per-edge
        // model); a turn charges both axes once each. A channel step with no following same-name
        // partner (defensive) charges its own edge's axis once. Block steps are unchanged (no charge).
        for (std::size_t i = 0; i < p.steps.size();) {
            auto it = chan_index.find(p.steps[i].first);
            if (it == chan_index.end()) { ++i; continue; }  // block step
            const int e_in = p.steps[i].second;
            int e_out = e_in;
            std::size_t consumed = 1;
            if (i + 1 < p.steps.size() && p.steps[i + 1].first == p.steps[i].first) {
                e_out = p.steps[i + 1].second;
                consumed = 2;
            }
            const bool touches_h = (e_in == 1 || e_in == 3 || e_out == 1 || e_out == 3);
            const bool touches_v = (e_in == 2 || e_in == 4 || e_out == 2 || e_out == 4);
            if (touches_h) hu[it->second] += p.nets;
            if (touches_v) vu[it->second] += p.nets;
            i += consumed;
        }

        // (3) Wirelength: V5 chain over the RAW step list (no dedup of consecutive identical
        // objects -- the edge pairs themselves drive corner insertion; see crossGuidingPoint /
        // cornerGuidingPoint above). Unknown object names are filtered out first, defensively.
        std::vector<ResolvedStep> steps;
        steps.reserve(p.steps.size());
        for (const auto& step : p.steps) {
            Rect r;
            if (rect_of(step.first, r)) steps.push_back({step.first, step.second, r});
        }
        std::vector<std::pair<double, double>> guides;
        for (std::size_t i = 0; i + 1 < steps.size(); ++i) {
            if (steps[i].name == steps[i + 1].name) {
                std::pair<double, double> corner;
                if (cornerGuidingPoint(steps[i].rect, steps[i].edge, steps[i + 1].edge, corner))
                    guides.push_back(corner);
            } else {
                guides.push_back(crossGuidingPoint(steps[i].rect, steps[i + 1].rect));
            }
        }
        if (guides.size() < 2) continue;  // <2 GPs -> path contributes 0
        double path_len = 0.0;
        for (std::size_t i = 0; i + 1 < guides.size(); ++i)
            path_len += manhattan(guides[i].first, guides[i].second, guides[i + 1].first, guides[i + 1].second);
        wirelength += path_len * p.nets;
    }
    out.wirelength = wirelength;

    // Overflow / capacity: horizontal capacity = 25*height, vertical capacity = 25*width.
    double overflow = 0.0;
    double capacity = 0.0;
    for (std::size_t k = 0; k < cfg.channels.size(); ++k) {
        const Rect& r = cfg.channels[k].rect;
        const double cap_h = kCapPerUm * r.h;
        const double cap_v = kCapPerUm * r.w;
        overflow += std::max(0.0, hu[k] - cap_h) + std::max(0.0, vu[k] - cap_v);
        capacity += cap_h + cap_v;
        out.demand_by_channel[cfg.channels[k].name] = {hu[k], vu[k]};
    }
    out.overflow_nets = overflow;
    out.capacity_nets = capacity;
    out.rate = (capacity > 0.0) ? overflow / capacity : 0.0;

    // Overflow penalty: exact V5 piecewise tail (docs/research/2026-07-28-alpha-v5-cost-
    // recalculation-analysis.md, section 1.3). All 5 alpha cases sit in the linear region under
    // V5's halved (single-charge-per-axis) demand accounting; the old case1 H1-tail / 0.31%-cost
    // doubt is resolved -- that gap was the demand accounting, not the tail formula (section 1.3
    // reproduces the historical R>threshold datapoint to the last digit).
    const double A = out.outline_area;
    const double R = out.rate;
    out.overflow_penalty = officialOverflowPenalty(R, A);
    // V5 FT penalty = beta_ft * max(0, FT_area_sum - outline_area) -- an AGGREGATE slack across
    // every block's FT-converted area, not a per-block mismatch sum (beta_ft = 3.0; same analysis
    // doc, section 1 item 4). Mirrors routing.cpp's computeOverflowAndMismatch ft_area_sum
    // computation exactly: both sides accumulate each soft block's crossing-net TOTAL first (across
    // every path/route touching it -- ft_nets_by_block above), THEN convert to area ONCE via
    // ftRequiredArea/ftExtend/ftTierRate -- required for identical tier evaluation, since a block's
    // FT tier depends on its cumulative nets, not any single path's contribution. All 5 alpha cases
    // still resolve to ft_penalty == 0 (no FT-carrying paths in those outputs; see
    // test/regression_cfg_score.cpp), but this now genuinely computes it rather than hardcoding it,
    // so the self-consistency identity (test/run_selfconsistency_regressions.sh) also holds for
    // FT-carrying solver output.
    double ft_area_sum = 0.0;
    for (std::size_t i = 0; i < problem.blocks.size(); ++i) {
        const Block& block = problem.blocks[i];
        if (isSoft(block.type)) {
            double base = block.ft_base_area > 0.0 ? block.ft_base_area : block.area;
            double nets_i = ft_nets_by_block[i];
            ft_area_sum += ftRequiredArea(base, ftExtend(nets_i, ftTierRate(block, nets_i)));
        } else {
            ft_area_sum += block.area;
        }
    }
    out.ft_penalty = kBetaFt * std::max(0.0, ft_area_sum - out.outline_area);

    // G4 (edge-LOCATION soft-anchor exploratory lever, docs/research/2026-07-28-alpha-v5-cost-
    // recalculation-analysis.md section 1 item 4 / section 6): official PENALTY, charged
    // UNCONDITIONALLY -- unlike the solver's own eval.cpp (gated behind problem.edge_soft_anchor,
    // since only a flag-on search run may legitimately place an edge block off-anchor), the OFFICIAL
    // checker this file reproduces has no such flag: every edge block's LOCATION mismatch is priced
    // at kGammaEdge/um regardless of how the .cfg was produced. Scoped to the .cfg's OWN declared
    // outline (cfg.outline_w/h, matching out.outline_area above), not problem.outline_w/h (CSV MAX) --
    // LOCATION thirds/flush are relative to the outline actually used, same convention validate.cpp's
    // validateSolution uses (scoped to placement.outline_w/h when stamped). All 5 alpha-test .cfgs
    // place every edge block exactly on a satisfying LOCATION (see test/run_cfg_score_regressions.sh),
    // so this term is measured 0 on the golden suite -- CAVEAT: the official per-violation DISTANCE
    // metric itself has never been observed nonzero, so edgeLocationViolationDistance above is a
    // best-guess reproduction (see its own doc comment), not a verified one.
    Problem outline_scoped = problem;
    outline_scoped.outline_w = cfg.outline_w;
    outline_scoped.outline_h = cfg.outline_h;
    double edge_location_penalty = 0.0;
    for (std::size_t i = 0; i < problem.blocks.size(); ++i) {
        const Block& block = problem.blocks[i];
        if (!isEdge(block.type)) continue;
        auto it = cfg.blocks.find(block.name);
        if (it == cfg.blocks.end()) continue;  // unknown object -> skip (defensive; legal .cfg never hits this)
        edge_location_penalty += edgeLocationViolationDistance(it->second, outline_scoped, block.locations);
    }
    out.edge_location_penalty = kGammaEdge * edge_location_penalty;

    out.checker_cost = A + problem.alpha * out.wirelength + out.overflow_penalty;
    out.checker_cost += out.ft_penalty;
    out.checker_cost += out.edge_location_penalty;
    return out;
}

}  // namespace efgr
