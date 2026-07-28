#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <utility>
#include <vector>

#include "model.hpp"

namespace efgr {

// Shared route-geometry/accounting helpers. Beta Task 1 moved these verbatim out of routing.cpp so a
// future .cfg re-scorer could reuse the solver's exact accounting; Beta Task 3 then REPLACED the
// accounting model here with the OFFICIAL checker's (per-edge channel demand + center-anchored
// guiding-point wirelength), so the solver's internal accounting agreed with the golden re-scorer
// (src/cfg_score.cpp) on the same artifact. The V5 recalculation (docs/research/2026-07-28-alpha-v5-
// cost-recalculation-analysis.md) then replaced BOTH pieces again: the guiding-point rule is now the
// cross/corner chain (section 1.1) and channel demand is charged once per TOUCHED AXIS per traversal,
// not twice per hop (section 1.2) -- this header is the solver-side TWIN of cfg_score.cpp's own V5
// rewrite, kept in agreement by test/run_selfconsistency_regressions.sh. Header-only, no I/O; the only
// mutation is to the Channel objects the caller points `accumulatePathUsage`'s `node_channels` at.

// Edge numbering (QA A22): 1 = left, 2 = top, 3 = right, 4 = bottom (clockwise from left).
// Moved verbatim from routing.cpp:24.
inline int edgeFromTo(const Rect& a, const Rect& b) {
    if (std::abs(a.x - b.right()) < kEps) return 1;
    if (std::abs(a.top() - b.y) < kEps) return 2;
    if (std::abs(a.right() - b.x) < kEps) return 3;
    return 4;
}

// V5 wirelength CROSS guiding point (docs/research/2026-07-28-alpha-v5-cost-recalculation-analysis.md,
// section 1.1): the CROSS point between two DIFFERENT consecutive path nodes. The shared border is
// still chosen by MINIMUM gap so classification is robust to the .cfg's 3-decimal rounding (abutting
// edges can round-differ by up to ~0.001 um -- unchanged from the retired center-anchored rule).
// ACROSS the border the coordinate is still the midpoint of the two near edges (unchanged); ALONG the
// border the coordinate is now the midpoint of the two rects' OVERLAP interval (not their centers) --
// this is the one piece V5 changes here.
//
// This is the solver-side TWIN of the locked oracle copy in src/cfg_score.cpp:crossGuidingPoint (kept
// byte-identical in logic; the two are kept in sync by test/run_selfconsistency_regressions.sh, which
// fails if the solver's routed wirelength ever diverges from the re-scorer's on the same .cfg).
// cfg_score.cpp deliberately keeps its OWN copy (it is a calibration oracle, independent of the
// solver's headers); this duplication is intentional and gate-enforced.
inline std::pair<double, double> crossGuidingPoint(const Rect& a, const Rect& b) {
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

// V5 wirelength CORNER guiding point (same analysis doc, section 1.1): the point for an internal turn
// AT a single path node (its entry edge `e_in` and exit edge `e_out` land on different axes -- edges
// 1/3 are x-fixed/vertical borders, 2/4 are y-fixed/horizontal borders). Returns false (no point)
// when e_in/e_out share an axis -- a straight pass-through inserts nothing. Mirrors
// src/cfg_score.cpp:cornerGuidingPoint exactly (same twin relationship as crossGuidingPoint above).
inline bool cornerGuidingPoint(const Rect& r, int e_in, int e_out, std::pair<double, double>& point) {
    const bool in_is_x = (e_in == 1 || e_in == 3);
    const bool out_is_x = (e_out == 1 || e_out == 3);
    if (in_is_x == out_is_x) return false;  // same axis: straight pass-through -> no point
    const int ex = in_is_x ? e_in : e_out;
    const int ey = in_is_x ? e_out : e_in;
    point = {(ex == 1) ? r.x : r.right(), (ey == 2) ? r.top() : r.y};
    return true;
}

// V5 (same analysis doc, sections 1.1/1.2): one routed rect-sequence's contribution to wirelength and
// per-channel directional usage, computed the OFFICIAL V5 checker way (src/cfg_score.cpp) so the
// solver's committed accounting equals what the judge charges on the written .cfg. `node_rects[i]` is
// the rect of the i-th node on the path; `node_channels[i]` is the Channel* the i-th node owns if it is
// a channel node, else nullptr (non-null exactly when route.node_path[i] >= n_blocks).
//
//   1. WIRELENGTH = nets * Sum manhattan(consecutive guiding points). The guide chain has a CROSS
//      point (crossGuidingPoint above) between every consecutive node pair, PLUS a CORNER point
//      (cornerGuidingPoint above) inserted between the two cross points at every INTERIOR node
//      (0 < i < n-1) whose entry edge (from its predecessor) and exit edge (to its successor) land on
//      different axes -- a straight pass-through (same axis) inserts nothing. This applies uniformly
//      to every interior node, channel OR soft-FT block: the RouteStep pair commitRoutePath (below,
//      routing.cpp) writes for an interior node's entry/exit edges has the identical shape whether
//      that node is a channel or a soft block being fed through, so the corner test (driven purely by
//      those two edge values) stays consistent with cfg_score.cpp's own raw-.cfg-step chain by
//      construction. A path of <2 nodes (e.g. a direct block<->block abut) has 0 wirelength (0 or 1
//      guiding point produced -- nothing to sum a distance over).
//
//   2. CHANNEL DEMAND: each INTERIOR node that is a channel charges `nets` ONCE per AXIS touched by
//      its entry/exit edges (not once per edge) -- a straight pass-through (both edges the same axis)
//      charges that one axis once; an L-turn (entry/exit on different axes) charges each axis once.
//      This replaces the retired per-edge model, which charged the entry axis AND the exit axis
//      separately and so double-charged every straight pass-through. ch.usage (max aggregate) is
//      refreshed on every charge, preserving the field's prior meaning for any reader.
inline double accumulatePathUsage(const std::vector<Rect>& node_rects,
                                   const std::vector<Channel*>& node_channels,
                                   double nets) {
    const std::size_t n = node_rects.size();

    // (1) Wirelength: cross GP per hop, corner GP inserted at interior turns (see doc comment above).
    std::vector<std::pair<double, double>> guides;
    for (std::size_t i = 0; i + 1 < n; ++i) {
        guides.push_back(crossGuidingPoint(node_rects[i], node_rects[i + 1]));
        if (i + 2 < n) {  // node i+1 is interior: has both a predecessor (i) and a successor (i+2)
            int e_in = edgeFromTo(node_rects[i + 1], node_rects[i]);
            int e_out = edgeFromTo(node_rects[i + 1], node_rects[i + 2]);
            std::pair<double, double> corner;
            if (cornerGuidingPoint(node_rects[i + 1], e_in, e_out, corner)) guides.push_back(corner);
        }
    }
    double wirelength = 0.0;
    for (std::size_t i = 0; i + 1 < guides.size(); ++i) {
        wirelength += manhattan(guides[i].first, guides[i].second, guides[i + 1].first, guides[i + 1].second) * nets;
    }

    // (2) Channel demand: per-axis-per-traversal (see doc comment above). Non-channel interior nodes
    // (soft-FT blocks) are skipped here -- they have no horizontal_usage/vertical_usage to charge.
    for (std::size_t i = 1; i + 1 < n; ++i) {
        Channel* ch = node_channels[i];
        if (ch == nullptr) continue;
        int e_in = edgeFromTo(node_rects[i], node_rects[i - 1]);
        int e_out = edgeFromTo(node_rects[i], node_rects[i + 1]);
        bool touches_h = (e_in == 1 || e_in == 3 || e_out == 1 || e_out == 3);
        bool touches_v = (e_in == 2 || e_in == 4 || e_out == 2 || e_out == 4);
        if (touches_h) ch->horizontal_usage += nets;
        if (touches_v) ch->vertical_usage += nets;
        ch->usage = std::max(ch->horizontal_usage, ch->vertical_usage);
    }
    return wirelength;
}

}  // namespace efgr
