#pragma once

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

#include "ft.hpp"
#include "model.hpp"
#include "route_geometry.hpp"

namespace efgr {

// P2 Task 1 (design p2-design.md §Q1 / §3; plan Global Constraint 1/3/4): pure cost-unit routing
// edge functions. Mirrors the ft.hpp pattern -- header-only, no mutation, no I/O. These give the
// (future) cost-driven Dijkstra pass edge weights denominated in the SAME units the scorer charges
// (eval.cpp:145-146), so the shortest path is the min-Delta-Cost path.

// Named weights: HISTORICAL router-side marginal pricing (channelMarginalCost/softFtMarginalCost
// below). B4 (b4-design.md §7.4) removed both from the router's own call sites in routing.cpp --
// the Dijkstra edge pricing, history surcharge, and round-selection/comparison-gate key
// (roundCostOfficial below) now price directly in the SCORER's own units (kBetaLin/kBetaTail/
// kBetaFt applied to marginalOverflowNets/ftMismatchDelta, routing.cpp), replacing the 1e6-area-
// denominated proxy these two weights drove. kOverflowAreaWeight/kFtMismatchWeight and the
// functions that use them are KEPT, UNUSED BY THE ROUTER, solely so their existing T1 pure-function
// unit tests (test/regression_p2_20260707.cpp) stay meaningful; no production code path reads them
// anymore.
constexpr double kOverflowAreaWeight = 1e6;  // per um^2 of channel overflow area (T1 test fixture only, no longer router-facing)
constexpr double kFtMismatchWeight = 1e6;    // per um^2 of FT required-area shortfall (T1 test fixture only, no longer router-facing)

// C1 milestone (official-checker cost replica): SCORER-side global overflow-rate penalty
// coefficients + FT excess weight, matching the byte-verified official checker's cost_breakdown
// fields exactly (beta_lin, beta_tail, threshold, beta_ft). Consumed by eval.cpp's exact-tier scorer
// (routeAndScoreAt) applied to R = channel_overflow_nets/channel_capacity_total (model.hpp fields,
// populated by routing.cpp). Beta Task 3: these live HERE, not in model.hpp, so they do not collide
// with src/cfg_score.cpp's own anonymous-namespace kBetaLin/kBetaTail (the locked golden re-scorer,
// which includes model.hpp but NOT this header) -- see model.hpp's reconciliation note.
constexpr double kBetaLin = 20.0;                 // linear overflow-rate coefficient
constexpr double kBetaTail = 1.5;                 // steeper tail coefficient (R > threshold)
constexpr double kOverflowRateThreshold = 0.05;   // overflow-rate threshold for the tail
constexpr double kBetaFt = 3.0;                   // flat per-um^2 FT required-area shortfall weight
// G4 (edge-LOCATION soft-anchor exploratory lever, docs/research/2026-07-28-alpha-v5-cost-
// recalculation-analysis.md section 1 item 4 / section 6): official per-um-of-violation-distance
// coefficient for an edge block sitting off its declared LOCATION -- a PENALTY, not a legality FAIL
// (unlike validate.cpp's satisfiesEdgeLocations gate, which still hard-fails it unless
// problem.edge_soft_anchor is set; see validate.cpp's edge-location branch). src/cfg_score.cpp keeps
// its OWN locked copy of this constant (oracle-duplication policy, same as kBetaLin/kBetaTail/
// kBetaFt's twins there) and charges it UNCONDITIONALLY -- officially the penalty always applies;
// gamma=1 is confirmed from the alpha-test "PATH Detail Cost Calculation" page's constant table.
constexpr double kGammaEdge = 1.0;                // per-um edge-LOCATION violation-distance weight

// V5 overflow-penalty tail (docs/research/2026-07-28-alpha-v5-cost-recalculation-analysis.md,
// section 1.3): EXACT official piecewise formula -- the linear term caps at kOverflowRateThreshold
// and a sqrt tail takes over beyond it. src/cfg_score.cpp keeps its OWN locked copy of this exact
// formula (it is a calibration oracle, independent of the solver's headers, and does not include
// this file); this duplication is intentional and gate-enforced, the same policy as
// crossGuidingPoint's solver/oracle twin relationship (route_geometry.hpp).
inline double officialOverflowPenalty(double rate, double outline_area) {
    if (rate <= kOverflowRateThreshold) return kBetaLin * rate * outline_area;
    return kBetaLin * kOverflowRateThreshold * outline_area +
           kBetaTail * std::sqrt(rate - kOverflowRateThreshold) * outline_area;
}

// V5 tail-aware per-overflow-net ROUND price (Codex V5-port review finding #2; analysis doc
// section 6): the price routing.cpp charges per marginal overflow net during a negotiation round,
// given the PREVIOUS round's global overflow level O against the fixed raw capacity C. Three
// regimes:
//   (O+1)/C <= threshold  ->  the pristine linear derivative kBetaLin*A/C: the SAME operands in
//       the SAME order routing.cpp historically computed, so linear-regime pricing stays
//       bit-identical (every current benchmark case sits at R <= 0.00906 << threshold).
//   O/C <= threshold < (O+1)/C  ->  the exact one-net secant of officialOverflowPenalty: the
//       theta-entry cliff, finite at threshold+ (where the analytic sqrt derivative diverges) --
//       deliberately deterrent, matching the analysis doc's section-1.3 strategy note that
//       staying under theta matters more than previously believed.
//   O/C > threshold  ->  the exact AVERAGE officialOverflowPenalty(O/C, A) / O, NOT the endpoint
//       one-net secant. Each negotiation round is a full rip-up that rebuilds its overflow from
//       ZERO, and the official penalty is CONCAVE past theta -- pricing every net at the endpoint
//       marginal undercharges a from-zero rebuild (O*P'(O) << P(O)): measured on the p2
//       ChannelVsFt fixture, endpoint pricing steered every round to the all-channel routing
//       (official value 3.02M) instead of the channel+FT split (2.36M), and with every round
//       equally misled, best-round selection could not recover. The average is the unique flat
//       per-net price whose round total reproduces the true penalty: a round rebuilding the same
//       O pays exactly P(O), the route carrying the sole overflowing chunk is charged its true
//       global delta, and concurrent overflowers' shares sum to the exact total. (In the linear
//       regime average == derivative anyway -- P is linear through the origin -- which is why the
//       first branch can keep the historical expression verbatim.)
// Pure and deterministic -- a function of (O, C, A) only; routing.cpp feeds it the previous
// round's own deterministic result, so re-priced rounds stay iteration-order independent.
inline double roundOverflowPrice(double overflow_nets, double capacity_total, double outline_area) {
    if (capacity_total <= 0.0) return 0.0;
    const double rate_after = (overflow_nets + 1.0) / capacity_total;
    if (rate_after <= kOverflowRateThreshold) return kBetaLin * outline_area / capacity_total;
    const double rate_before = overflow_nets / capacity_total;
    if (rate_before <= kOverflowRateThreshold) {
        return officialOverflowPenalty(rate_after, outline_area) -
               officialOverflowPenalty(rate_before, outline_area);
    }
    return officialOverflowPenalty(rate_before, outline_area) / overflow_nets;
}

// Channel overflow AREA for a given direction at `usage` nets (design p2-design.md §8.1 / T0
// denomination, routing.cpp:221-243): horizontal usage is capacitated by 25*h (missing capacity is
// a height deficit spanning the channel's WIDTH); vertical usage is capacitated by 25*w (missing
// capacity is a width deficit spanning the channel's HEIGHT).
// B4 (b4-design.md §7.4): no longer called by the router (routing.cpp now prices overflow in NETS
// via marginalOverflowNets below, at the official P_ovf=kBetaLin*A/C rate). Kept for its T1 tests.
inline double channelOverflowArea(const Channel& ch, bool horizontal, double usage) {
    if (horizontal) {
        double hcap = 25.0 * std::max(1.0, ch.rect.h);
        if (usage <= hcap) return 0.0;
        double missing_height = (usage - hcap) / 25.0;
        return missing_height * ch.rect.w;
    }
    double vcap = 25.0 * std::max(1.0, ch.rect.w);
    if (usage <= vcap) return 0.0;
    double missing_width = (usage - vcap) / 25.0;
    return missing_width * ch.rect.h;
}

// Marginal area penalty (um^2 * weight) of pushing `n` more nets through channel `ch` in the given
// direction, given current accumulated `usage` in that direction (design §3.2, plan Global
// Constraint 3: MARGINAL pricing, i.e. penalty(usage+n) - penalty(usage), not total-demand area).
// Slack channels (usage+n <= cap) are free. Deterministic, pure -- no mutation of `ch`.
// B4 (b4-design.md §7.4): no longer called by the router. Kept for its T1 tests.
inline double channelMarginalCost(const Channel& ch, bool horizontal, double usage, double n) {
    double before = channelOverflowArea(ch, horizontal, usage);
    double after = channelOverflowArea(ch, horizontal, usage + n);
    return kOverflowAreaWeight * (after - before);
}

// B4 (design b4-design.md §3.2): exact marginal overflow in NETS (not area) of pushing `n` more
// nets through channel `ch` in the given direction, given current per-direction `usage`. RAW cap
// (25*ch.rect.h / 25*ch.rect.w, no max(1.0,.) clamp) matches the official checker's overflow_nets
// accounting exactly -- the same RAW capacity computeOverflowAndMismatch (routing.cpp) and
// cfg_score.cpp's officialScoreCfg both use for channel_overflow_nets/capacity_nets. This is the
// EXACT derivative of the scorer's piecewise-linear `20*A*R` term across the cap kink: 0 while
// usage+n <= cap (slack, free), n once usage >= cap (fully saturated, every pushed net is
// marginal overflow), and the exact straddling partial otherwise. Multiplying by P_ovf
// (routing.cpp: the linear kBetaLin*A/C for round 0, re-priced at the start of every later
// negotiation round from the previous round's global overflow via roundOverflowPrice above)
// gives the official per-edge marginal COST in the scorer's own units -- replacing
// channelMarginalCost's 1e6-area-denominated proxy as the router's channel-hop pricing
// (design §3.1-3.2).
inline double marginalOverflowNets(const Channel& ch, bool horizontal, double usage, double n) {
    double cap = horizontal ? 25.0 * ch.rect.h : 25.0 * ch.rect.w;
    return std::max(0.0, usage + n - cap) - std::max(0.0, usage - cap);
}

// V5 (docs/research/2026-07-28-alpha-v5-cost-recalculation-analysis.md, section 1.2): per-node
// channel slack for the greedy bottleneck-chunking loop's `pathSlack`, extracted as a pure function
// of the channel's two incident rects (predecessor, successor) so it is directly unit-testable --
// routing.cpp's `pathSlack` lambda calls this for every interior channel node, passing
// `rects[node_path[i-1]]` (predecessor), `rr.channels[...]` (this channel), `rects[node_path[i+1]]`
// (successor). An axis is either TOUCHED by this node's entry or exit edge -- its residual capacity
// counts as-is, no division -- or UNTOUCHED by both, meaning it is never charged and so carries
// infinite slack. This replaces the retired B4 per-edge-charge-count halving (an axis touched by
// BOTH the entry and exit edge -- a straight pass-through -- used to have its residual divided by 2,
// matching the old double-charge-per-hop demand model); V5 charges a touched axis only ONCE per
// traversal regardless of whether one or both edges land on it (route_geometry.hpp's
// accumulatePathUsage), so the residual is never divided here either. RAW caps (25*ch.rect.h/w),
// matching marginalOverflowNets/computeOverflowAndMismatch.
inline double channelPathSlack(const Rect& pred_rect, const Channel& ch, const Rect& succ_rect) {
    int entry_dir = edgeFromTo(pred_rect, ch.rect);  // charge from the p->k hop (k's entry edge)
    int exit_dir = edgeFromTo(ch.rect, succ_rect);   // charge from the k->s hop (k's exit edge)
    bool entry_h = (entry_dir == 1 || entry_dir == 3);
    bool exit_h = (exit_dir == 1 || exit_dir == 3);
    bool h_touched = entry_h || exit_h;
    bool v_touched = (!entry_h) || (!exit_h);
    double res_h = h_touched ? (25.0 * ch.rect.h - ch.horizontal_usage) : std::numeric_limits<double>::infinity();
    double res_v = v_touched ? (25.0 * ch.rect.w - ch.vertical_usage) : std::numeric_limits<double>::infinity();
    return std::max(0.0, std::min(res_h, res_v));
}

// Marginal FT MISMATCH (um^2, unweighted) of pushing `n` more crossing nets through soft block `b`
// whose accumulated crossing nets are `acc` and whose currently placed area is `placed_area`
// (design §3.3/§5, plan Global Constraint 4: incremental tier pricing). Uses ftRequiredArea/
// ftExtend/ftTierRate (ft.hpp) so a tier jump (3000/6000/9000) is priced exactly, not linearly
// approximated. Free while `placed_area` already covers the required area at `acc` and stays free
// until the required area at `acc+n` exceeds it (slack absorption, design §3.3 subtlety 1). B4
// (design §5): factored out of softFtMarginalCost below so the router can weight this same exact
// delta by the OFFICIAL kBetaFt=3.0 (routing.cpp call site) instead of the legacy 1e6 proxy, without
// duplicating the tier-math.
inline double ftMismatchDelta(const Block& b, double acc, double n, double placed_area) {
    double base = b.ft_base_area > 0.0 ? b.ft_base_area : b.area;
    double required_before = ftRequiredArea(base, ftExtend(acc, ftTierRate(b, acc)));
    double required_after = ftRequiredArea(base, ftExtend(acc + n, ftTierRate(b, acc + n)));
    double mismatch_before = std::max(0.0, required_before - placed_area);
    double mismatch_after = std::max(0.0, required_after - placed_area);
    return mismatch_after - mismatch_before;
}

// F-FT (docs/research/2026-07-28-alpha-v5-cost-recalculation-analysis.md §6, Codex finding #1): the
// V5 aggregate's EXACT per-block delta -- required(acc+n) - required(acc), using the same
// base/ftRequiredArea/ftExtend/ftTierRate calls as ftMismatchDelta above but WITHOUT that function's
// placed-area clamp: the aggregate FT term (RoutingResult.ft_area_sum, route_cost.hpp's
// roundCostOfficial / model.hpp's ft_area_sum doc comment) sums REQUIRED areas directly, never
// placed-vs-required mismatch. Used by routing.cpp's running rr.ft_area_sum accrual (commitRoutePath)
// and by the ft_pricing_aggressive Dijkstra edge pricing (routing.cpp) to price an FT hop at the exact
// marginal of the official aggregate penalty instead of the per-block mismatch delta.
inline double ftRequiredDelta(const Block& b, double acc, double n) {
    double base = b.ft_base_area > 0.0 ? b.ft_base_area : b.area;
    double required_before = ftRequiredArea(base, ftExtend(acc, ftTierRate(b, acc)));
    double required_after = ftRequiredArea(base, ftExtend(acc + n, ftTierRate(b, acc + n)));
    return required_after - required_before;
}

// F-FT: per-net epsilon added to an aggressive-mode FT hop's price so an officially-free FT hop (zero
// marginal aggregate cost) still LOSES ties against a channel route of equal wirelength -- 0.001/net
// is ~2 orders of magnitude below the smallest real WL delta (alpha*1um*nets at alpha>=0.1), and far
// above double-precision noise, so it breaks exact ties without perturbing any genuine cost ranking.
constexpr double kFtAggressiveTieBreak = 0.001;

// Marginal FT penalty (um^2 * weight) -- thin kFtMismatchWeight wrapper over ftMismatchDelta above.
// B4 (b4-design.md §7.4): no longer called by the router (routing.cpp now weights ftMismatchDelta
// by the official kBetaFt directly). Kept for its T1 tests.
inline double softFtMarginalCost(const Block& b, double acc, double n, double placed_area) {
    return kFtMismatchWeight * ftMismatchDelta(b, acc, n, placed_area);
}

// G4 (edge-LOCATION soft-anchor exploratory lever): per-LOCATION-string satisfaction test + minimum
// translation distance, mirroring validate.cpp's satisfiesEdgeLocation/locSegment/positiveOverlap and
// placement_engine.cpp's placeEdge/edgeRectLegal geometry (T/B pin the y coordinate flush to the
// scoped outline's top/bottom border and require the x-interval to overlap a third-of-width segment;
// L/R pin x flush to the left/right border and require the y-interval to overlap a third-of-height
// segment -- see either file's own locSegment thirds-of-outline table). Kept here as the ONE shared,
// external-linkage copy eval.cpp calls -- validate.cpp/placement_engine.cpp both keep this geometry
// file-local (anonymous namespace) for their own pass/fail and placement purposes, unchanged by G4.
// src/cfg_score.cpp keeps its own independent anonymous-namespace copy (oracle-duplication policy,
// matching route_geometry.hpp's crossGuidingPoint / cfg_score.cpp's twin relationship).
inline char edgeLocSide(const std::string& loc) { return loc.empty() ? '\0' : loc[0]; }

inline std::pair<double, double> edgeLocSegment(double outline_w, double outline_h, const std::string& loc) {
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

inline bool edgeLocPositiveOverlap(double a0, double a1, double b0, double b1) {
    return std::min(a1, b1) - std::max(a0, b0) > kEps;
}

// True exactly when validate.cpp's (file-local) satisfiesEdgeLocation(r, problem, loc) would be true
// -- same pin + range-overlap test, re-derived from (outline_w, outline_h) instead of a Problem&.
inline bool edgeLocSatisfied(const Rect& r, double outline_w, double outline_h, const std::string& loc) {
    char side = edgeLocSide(loc);
    auto [lo, hi] = edgeLocSegment(outline_w, outline_h, loc);
    if (side == 'T') return std::abs(r.top() - outline_h) < kEps && edgeLocPositiveOverlap(r.x, r.right(), lo, hi);
    if (side == 'B') return std::abs(r.y) < kEps && edgeLocPositiveOverlap(r.x, r.right(), lo, hi);
    if (side == 'L') return std::abs(r.x) < kEps && edgeLocPositiveOverlap(r.y, r.top(), lo, hi);
    if (side == 'R') return std::abs(r.right() - outline_w) < kEps && edgeLocPositiveOverlap(r.y, r.top(), lo, hi);
    return true;
}

// Signed 1-D gap to close between projection [p0,p1] and target segment [lo,hi]: positive/negative
// tells the caller which direction to translate; 0 when already overlapping (or touching within
// kEps). Shared by both axes of edgeLocDelta below.
inline double edgeLocRangeGap(double p0, double p1, double lo, double hi) {
    if (p1 <= lo) return lo - p1;
    if (p0 >= hi) return hi - p0;
    return 0.0;
}

// Minimum (dx,dy) translation of `r` that would satisfy ONE given LOCATION string alone (pin the
// required border axis flush, close any gap on the other axis to the assigned third-segment) -- both
// 0 if already satisfied. Signed (not magnitude) since the multi-LOCATION "corner" combination in
// edgeLocationViolationDistance below needs direction, not just distance.
inline std::pair<double, double> edgeLocDelta(const Rect& r, double outline_w, double outline_h,
                                              const std::string& loc) {
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

// CAVEAT (task G4): the official per-violation DISTANCE metric has never been observed nonzero on any
// alpha-test case (every alpha .cfg's edge blocks sit exactly on a satisfying LOCATION) -- there is no
// byte-verified reference for what "distance" means beyond gamma=1/um (docs/research/2026-07-28-
// alpha-v5-cost-recalculation-analysis.md section 1 item 4 / section 6), so this is a BEST-GUESS
// (minimum |dx|+|dy| translation reaching a satisfying position), not a verified reproduction:
//   * single-side blocks (all `locs` share one border letter T/B/L/R -- the common case, OR
//     semantics: any ONE of the listed LOCATION strings satisfying `r` is enough, matching validate.
//     cpp's satisfiesEdgeLocations) -- EXACT: the minimum over each individual LOCATION's own
//     edgeLocDelta magnitude.
//   * multi-side "corner" blocks (locs span >1 distinct border letter -- AND semantics: `r` must
//     satisfy EVERY listed LOCATION simultaneously, e.g. "TL"+"LT" = touch top AND left) --
//     APPROXIMATE: combines every loc's own (dx,dy) by keeping, per axis, the larger-magnitude
//     requirement seen across the set (same-side locs pin identically; a translation covering the
//     most demanding single-axis requirement satisfies every corner case in the current test data,
//     but this is not proven optimal in general).
// 0 exactly when the (re-derived, byte-identical) satisfiesEdgeLocations predicate holds -- that
// zero-case, not the nonzero magnitude, is the property callers actually depend on (eval.cpp only
// adds a nonzero term when a real violation exists; the golden alpha suite must see 0 on all cases).
// Pure and deterministic -- no mutation, no I/O.
// L2 (edge-free mode, docs/research/2026-07-30-final-levers-design.md §L2.3d): (outline_w, outline_h)
// base overload -- edgeLocSatisfied/edgeLocDelta above already take (w,h), not a whole Problem, so
// this is the natural primary signature. The (rect, const Problem&, locs) overload right below just
// forwards scoped.outline_w/h here (byte-identical result, same body). Lets eval.cpp's cheapScore
// (hot path, runs every SA iteration) call this directly without copying a Problem, unlike the old
// exact-tier-only call site this replaces.
inline double edgeLocationViolationDistance(const Rect& r, double outline_w, double outline_h,
                                            const std::vector<std::string>& locs) {
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
            if (edgeLocSatisfied(r, outline_w, outline_h, loc)) return 0.0;
            auto [dx, dy] = edgeLocDelta(r, outline_w, outline_h, loc);
            best = std::min(best, std::abs(dx) + std::abs(dy));
        }
        return best;
    }

    bool all_satisfied = true;
    for (const auto& loc : locs) {
        if (!edgeLocSatisfied(r, outline_w, outline_h, loc)) { all_satisfied = false; break; }
    }
    if (all_satisfied) return 0.0;

    double dx = 0.0, dy = 0.0;
    for (const auto& loc : locs) {
        auto [ldx, ldy] = edgeLocDelta(r, outline_w, outline_h, loc);
        if (std::abs(ldx) > std::abs(dx)) dx = ldx;
        if (std::abs(ldy) > std::abs(dy)) dy = ldy;
    }
    return std::abs(dx) + std::abs(dy);
}

// (rect, const Problem&, locs) convenience overload, kept for the existing call-site shape --
// forwards to the (w,h) overload above. Byte-identical result (same body, same values read out of
// scoped.outline_w/h).
inline double edgeLocationViolationDistance(const Rect& r, const Problem& scoped,
                                            const std::vector<std::string>& locs) {
    return edgeLocationViolationDistance(r, scoped.outline_w, scoped.outline_h, locs);
}

// Wirelength term in Cost units for a hop of guiding-point distance `dist` carrying `n` nets
// (design §3.1: alpha*n*manhattan monotone WL proxy, same units as the area terms above).
inline double wireCost(double alpha, double n, double dist) { return alpha * n * dist; }

// B4 (design b4-design.md §7.2): the negotiation round-selection / legacy-vs-cost-driven
// comparison-gate key, re-denominated to the SAME units eval.cpp's exact scorer charges --
// `(open_routes, alpha*WL + officialOverflowPenalty(R,A) + kBetaFt*max(0, ft_area_sum-A))`, where
// `R = channel_overflow_nets/channel_capacity_total` (RAW-capacity nets rate, model.hpp fields
// populated by computeOverflowAndMismatch) and `outline_area` is the SAME declared W*H eval.cpp's
// routeAndScoreAt scores this routing at. V5 (docs/research/2026-07-28-alpha-v5-cost-recalculation-
// analysis.md): the overflow term is now the exact officialOverflowPenalty tail (section 1.3, above)
// and the FT term is the AGGREGATE excess of ft_area_sum over the outline (section 1.4, ft_area_sum
// populated by computeOverflowAndMismatch), replacing the retired per-block feedthrough_mismatch sum.
// Unlike the per-edge Dijkstra pricing (design §3.4), the tail term IS included here -- R is known
// globally and cheaply once a full routing exists, so the gate stays exactly scorer-faithful even in
// the R>threshold regime. Returned as (open_routes, value) so std::pair's lexicographic operator<
// sorts open routes first, mirroring eval.cpp's open_routes*1e15 dominance.
//
// Algebraic identity (test-pinned, design §8(v)/R2): for a routing with open_routes==0 and zero
// legality penalty (no overlap / out-of-outline), roundCostOfficial(rr, alpha, outline_area).second
// == exactScore(...).value - outline_area EXACTLY -- this identity is what makes the gate
// scorer-faithful (ranking candidates by this key ranks them exactly as the scorer would), and stays
// intact under V5 (both sides moved to the same new formula together, eval.cpp). A free function
// (not a routeProblem-local lambda) specifically so this identity is unit-testable against the SAME
// code routing.cpp's gate calls, not a reimplementation of the formula.
inline std::pair<int, double> roundCostOfficial(const RoutingResult& rr, double alpha, double outline_area) {
    double rate = (rr.channel_capacity_total > 0.0) ? rr.channel_overflow_nets / rr.channel_capacity_total : 0.0;
    double overflow_penalty = officialOverflowPenalty(rate, outline_area);
    double ft_penalty = kBetaFt * std::max(0.0, rr.ft_area_sum - outline_area);
    double value = alpha * rr.total_wirelength + overflow_penalty + ft_penalty;
    return std::pair<int, double>(rr.open_routes, value);
}

// P2 Task 3 (design p2-design.md §5.1-5.4; plan Global Constraint 8): named constants for the
// greedy split-at-bottleneck bundle-chunking loop (routing.cpp, cost_driven=true path only -- the
// cost_driven=false legacy path keeps the exact fixed ceil(nets/1000) split untouched).

// Upper bound on chunks emitted per bundle (design §5.3): double the old fixed cap of 8, since
// adaptive splitting needs a few more chunks than the old scheme to express bottleneck-fills, while
// staying bounded (Dijkstra-count budget, §5.3's ~2400-Dijkstra estimate at R=3 negotiation rounds).
constexpr int kMaxChunks = 16;

// Chunk-size ceiling (design §5.2 step 3, "size min(remaining, kChunkCeil)") used only when the
// current cheapest path is ALREADY saturated (slack<=0) and there is no cheaper split to express --
// the design leaves this constant's exact value unpinned; 1000 is chosen here to match the legacy
// fixed-split granule (routing.cpp's old ceil(nets/1000) chunk size), so a fully-saturated-with-no-
// alternative bundle degrades to the same per-chunk granularity the old router used.
constexpr int kChunkCeil = 1000;

// Minimum chunk size (design §5.3: "prevents pathological 1-net chunks"), scaled to the BUNDLE's
// total net count (not the shrinking `remaining` counter -- a floor proportional to remaining would
// shrink towards 1 near the end of a bundle, which is exactly the pathological case this guards
// against). Matches design §5.3's `max(1, nets/64)` verbatim.
inline double kMinChunkFor(int bundle_nets) { return std::max(1.0, bundle_nets / 64.0); }

// P2 Task 4 (design p2-design.md §4.1-§4.4; plan Global Constraint 9): named constants for the
// negotiated rip-up/reroute outer loop around the cost_driven=true bundle-routing pass in
// routing.cpp. cost_driven=false is entirely unaffected -- these constants are only read from the
// negotiation loop.

// Number of full rip-up/reroute rounds (design §4.1). Round 0 has all-zero history (no bumps have
// happened yet), so it reproduces the T3 adaptive-chunking router exactly; later rounds differ only
// because history (below) has accumulated non-zero surcharges.
constexpr int kNegotiationRounds = 3;

// Per-round history decay applied at the START of each round (round >= 1), design §4.2: standard
// PathFinder hygiene so a resource that is no longer congested relaxes back instead of staying
// permanently poisoned by an earlier round's spike.
constexpr double kHistoryDecay = 0.5;

// Per-round history bump applied AFTER each round to every resource that ended the round over
// capacity (design §4.2). The design leaves this constant's exact magnitude unpinned ("for
// robustness"); 1.0 is a tuning knob to be measured/re-tuned at T7's sweep, not a value derived from
// the scorer's own units the way kOverflowAreaWeight/kFtMismatchWeight are.
constexpr double kHistoryBump = 1.0;

// B1 (constant-gap negotiation pricing, .superpowers/sdd/levers-batch2-briefs-20260817.md Brief 2):
// what escalates today (kHistoryBump above, bumped additively per round and halved at the start of
// every later round) is a surcharge sized as a multiple of the RELATIVE overuse rho =
// (usage-cap)/cap -- for a resource barely over capacity (rho ~ 0.01, the common case) that surcharge
// is ~1% of the official per-overflow-net price p_ovf, orders of magnitude below alpha*(W+H) (the
// cost of any real detour across the outline), so 3 negotiation rounds cannot redirect demand off it;
// for a badly-overused resource the same escalating formula can instead blow up. This gives a
// STICKY (never decayed under problem.route_const_gap -- see routing.cpp's decay guard),
// order-independent, size-correct alternative: the history unit such that p_ovf*hist ==
// kConstGapDetourFactor*alpha*(outline_w+outline_h) EXACTLY -- one unit of overuse out-prices the
// worst-case legal detour (a full round-trip across the outline) by kConstGapDetourFactor. Assigned
// (never accumulated) at the same channel-history bump call site kHistoryBump's additive bump used to
// own (routing.cpp), replacing it entirely under the flag. Pure function of (alpha, outline dims,
// the current per-net price); 0.0 whenever price<=0 (never divide by zero/negative -- mirrors
// roundOverflowPrice's own capacity_total<=0 guard above).
constexpr double kConstGapDetourFactor = 1.0;

inline double constantGapHistoryUnits(double alpha, double outline_w, double outline_h, double price) {
    return price > 0.0 ? kConstGapDetourFactor * alpha * (outline_w + outline_h) / price : 0.0;
}

}  // namespace efgr
