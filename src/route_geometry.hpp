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
// guiding-point wirelength), so the solver's internal accounting agrees with the golden re-scorer
// (src/cfg_score.cpp) on the same artifact. Header-only, no I/O; the only mutation is to the Channel
// objects the caller points `accumulatePathUsage`'s `node_channels` at.

// Edge numbering (QA A22): 1 = left, 2 = top, 3 = right, 4 = bottom (clockwise from left).
// Moved verbatim from routing.cpp:24.
inline int edgeFromTo(const Rect& a, const Rect& b) {
    if (std::abs(a.x - b.right()) < kEps) return 1;
    if (std::abs(a.top() - b.y) < kEps) return 2;
    if (std::abs(a.right() - b.x) < kEps) return 3;
    return 4;
}

// Beta Task 3 (official-checker accounting): the checker's wirelength guiding point. The point sits
// on the shared border between `a` and `b`; the border is chosen by MINIMUM gap so classification is
// robust to the .cfg's 3-decimal rounding (abutting edges can round-differ by up to ~0.001 um). Along
// the border the coordinate is the average of the two rectangles' centers (equivalently, the midpoint
// of the two facing-edge midpoints); across the border it is the midpoint of the two near edges.
//
// This is the solver-side TWIN of the locked oracle copy in src/cfg_score.cpp:guidingPoint (kept
// byte-identical in logic; the two are kept in sync by test/run_selfconsistency_regressions.sh, which
// fails if the solver's routed wirelength ever diverges from the re-scorer's on the same .cfg). It
// REPLACES Beta Task 1's overlap-midpoint `guidePoint`, which mis-measured wirelength vs the judge
// (~6% off) and is now retired. cfg_score.cpp deliberately keeps its OWN copy (it is a calibration
// oracle, independent of the solver's headers); this duplication is intentional and gate-enforced.
inline std::pair<double, double> guidingPoint(const Rect& a, const Rect& b) {
    const double g_ar_bx = std::abs(a.right() - b.x);  // a.right meets b.left  (vertical border)
    const double g_br_ax = std::abs(b.right() - a.x);  // b.right meets a.left  (vertical border)
    const double g_at_by = std::abs(a.top() - b.y);    // a.top   meets b.bottom(horizontal border)
    const double g_bt_ay = std::abs(b.top() - a.y);    // b.top   meets a.bottom(horizontal border)
    const double min_vertical = std::min(g_ar_bx, g_br_ax);
    const double min_horizontal = std::min(g_at_by, g_bt_ay);
    if (min_vertical <= min_horizontal) {
        const double x = (g_ar_bx <= g_br_ax) ? 0.5 * (a.right() + b.x) : 0.5 * (b.right() + a.x);
        return {x, 0.5 * (a.cy() + b.cy())};
    }
    const double y = (g_at_by <= g_bt_ay) ? 0.5 * (a.top() + b.y) : 0.5 * (b.top() + a.y);
    return {0.5 * (a.cx() + b.cx()), y};
}

// Beta Task 3 (official-checker accounting): one routed rect-sequence's contribution to wirelength and
// per-channel directional usage, computed the OFFICIAL checker way (src/cfg_score.cpp) so the solver's
// committed accounting equals what the judge charges on the written .cfg. `node_rects[i]` is the rect
// of the i-th node on the path; `node_channels[i]` is the Channel* the i-th node owns if it is a
// channel node, else nullptr (non-null exactly when route.node_path[i] >= n_blocks). Two changes vs
// the Beta Task 1 transplant this replaces:
//
//   1. WIRELENGTH = nets * Sum manhattan(consecutive guiding points), guiding points CENTER-anchored
//      (guidingPoint above), chained exactly as cfg_score.cpp does -- NOT the old overlap-midpoint
//      chain. A path of <3 distinct nodes (e.g. a direct block<->block abut) has 0 wirelength.
//
//   2. PER-EDGE channel demand: each channel TRAVERSAL charges `nets` to its ENTRY edge's axis AND
//      `nets` to its EXIT edge's axis (edge 1/3 = left/right -> horizontal_usage, 2/4 = top/bottom ->
//      vertical_usage). A straight pass-through therefore charges ONE axis TWICE; an L-turn charges
//      each axis once. This mirrors, edge-for-edge, the two RouteStep entries routing.cpp emits for
//      every hop -- (name_a, edgeFromTo(a,b)) and (name_b, edgeFromTo(b,a)) -- which are the exact
//      edge tokens written into the .cfg and re-read by cfg_score's per-edge demand loop. So the
//      committed ch.horizontal_usage/vertical_usage equal the judged demand BY CONSTRUCTION (usage is
//      integer nets; no coordinate rounding enters the classification, since the same edgeFromTo call
//      feeds both the accrual here and the emitted step). ch.usage (max aggregate) is refreshed on
//      every charge, preserving the field's prior meaning for any reader.
inline double accumulatePathUsage(const std::vector<Rect>& node_rects,
                                   const std::vector<Channel*>& node_channels,
                                   double nets) {
    // (1) Wirelength: center-anchored guiding-point chain (checker model).
    std::vector<std::pair<double, double>> guides;
    for (std::size_t i = 0; i + 1 < node_rects.size(); ++i) {
        guides.push_back(guidingPoint(node_rects[i], node_rects[i + 1]));
    }
    double wirelength = 0.0;
    for (std::size_t i = 0; i + 1 < guides.size(); ++i) {
        wirelength += manhattan(guides[i].first, guides[i].second, guides[i + 1].first, guides[i + 1].second) * nets;
    }

    // (2) Per-edge channel demand: for each hop, charge the FROM node's exit edge and the TO node's
    // entry edge (each to its own axis) -- exactly the two RouteStep edges routing.cpp emits per hop.
    auto chargeEdge = [&](Channel* ch, int edge) {
        if (edge == 1 || edge == 3) ch->horizontal_usage += nets;  // left/right -> horizontal
        else                        ch->vertical_usage += nets;    // top/bottom -> vertical
        ch->usage = std::max(ch->horizontal_usage, ch->vertical_usage);
    };
    for (std::size_t i = 0; i + 1 < node_rects.size(); ++i) {
        if (node_channels[i] != nullptr) {
            chargeEdge(node_channels[i], edgeFromTo(node_rects[i], node_rects[i + 1]));      // exit edge of node i
        }
        if (node_channels[i + 1] != nullptr) {
            chargeEdge(node_channels[i + 1], edgeFromTo(node_rects[i + 1], node_rects[i]));  // entry edge of node i+1
        }
    }
    return wirelength;
}

}  // namespace efgr
