#include "eval.hpp"

#include <algorithm>
#include <cmath>
#include <tuple>

#include "ft.hpp"
#include "placement_engine.hpp"
#include "route_cost.hpp"

namespace efgr {

std::pair<double, double> usedOutline(const Problem& problem, const Placement& placement) {
    double w = 0.0;
    double h = 0.0;
    for (const auto& r : placement.rects) {
        w = std::max(w, r.right());
        h = std::max(h, r.top());
    }
    w = std::max(1.0, std::min(problem.outline_w, w));
    h = std::max(1.0, std::min(problem.outline_h, h));
    return {w, h};
}

std::pair<double, double> usedOutline(const Problem& problem, const Placement& placement,
                                       const RoutingResult& routing) {
    double w = 0.0;
    double h = 0.0;
    for (const auto& r : placement.rects) {
        w = std::max(w, r.right());
        h = std::max(h, r.top());
    }
    for (const auto& ch : routing.channels) {
        w = std::max(w, ch.rect.right());
        h = std::max(h, ch.rect.top());
    }
    w = std::max(1.0, std::min(problem.outline_w, w));
    h = std::max(1.0, std::min(problem.outline_h, h));
    return {w, h};
}

namespace {

bool intersectsOpen(const Rect& a, const Rect& b) {
    return a.x + kEps < b.right() && b.x + kEps < a.right() &&
           a.y + kEps < b.top() && b.y + kEps < a.top();
}

// B5b (abutment-credit cheap-score upgrade, .superpowers/sdd/b5-design.md §3.2): ramp width, as a
// fraction of one block side, over which the credit rises from 0 (gap == d_thr) to 1 (gap == 0). A
// named tunable per the design's own recommendation ("partial credit (ramp), width one block side").
constexpr double kAbutRampFrac = 1.0;

// B5b (b5-design.md §3.2): bundle-weighted abutment credit in [0,1] -- 1.0 at exact
// "positiveTouch"-consistent abutment (a shared edge with positive perpendicular overlap, matching
// routing.cpp:17-23's adjacency test), ramping linearly down to 0.0 at a separation of d_thr (one
// block side away), so cheapScore's SA gradient rewards closing the gap all the way to the A31
// zero-WL/zero-demand cliff (design §3.3's monotonicity argument) instead of bottoming out at the
// un-credited center-manhattan floor. Diagonal pairs (no shared-edge axis at all, i.e. neither x nor
// y overlaps) cap at 0.5 -- they cannot truly abut without sliding onto a shared axis first. Pure
// function of the two rects; deterministic, no mutation, no problem-level state.
double abutmentCredit(const Rect& a, const Rect& b) {
    double x_overlap = std::min(a.right(), b.right()) - std::max(a.x, b.x);
    double y_overlap = std::min(a.top(), b.top()) - std::max(a.y, b.y);
    double dx_gap = std::max(0.0, -x_overlap);  // horizontal separation (0 if x-overlap)
    double dy_gap = std::max(0.0, -y_overlap);  // vertical separation (0 if y-overlap)
    bool has_abut_axis = (y_overlap > kEps) || (x_overlap > kEps);
    double g = (y_overlap > kEps) ? dx_gap             // can abut on a vertical edge
             : (x_overlap > kEps) ? dy_gap             // can abut on a horizontal edge
             : (dx_gap + dy_gap);                      // diagonal: corner approach, capped credit
    double d_thr = kAbutRampFrac * 0.5 * (std::min(a.w, b.w) + std::min(a.h, b.h));
    double credit = d_thr > kEps ? std::clamp(1.0 - g / d_thr, 0.0, 1.0) : (g <= kEps ? 1.0 : 0.0);
    if (!has_abut_axis) credit = std::min(credit, 0.5);
    return credit;
}

double routeAwareProxyPenalty(const Problem& problem, const Placement& placement) {
    double penalty = 0.0;
    for (const auto& bundle : problem.bundles) {
        const auto& a = placement.rects[bundle.source];
        const auto& b = placement.rects[bundle.target];
        double dx = std::abs(a.cx() - b.cx());
        double dy = std::abs(a.cy() - b.cy());
        double narrow = 0.0;
        if (dx >= dy) {
            narrow = std::max(1.0, std::min(a.h, b.h));
        } else {
            narrow = std::max(1.0, std::min(a.w, b.w));
        }
        double capacity = narrow * 25.0;
        double overflow = std::max(0.0, static_cast<double>(bundle.nets) - capacity);
        // C1 milestone: gentle LINEAR proxy (was overflow^2 * 100) so the cheap tier's overflow
        // gradient is the same order as the exact tier's kBetaLin-weighted global penalty, instead
        // of a quadratic that swamped everything else in the cheap score.
        double congestion = overflow * kBetaLin;
        // B5b (b5-design.md §3.2): scale the congestion term toward zero as (a,b) approach
        // abutment. Gated behind problem.abutment_credit so credit-off reproduces the un-credited
        // arithmetic exactly (an `if`, not a multiply-by-1) -- this function is called identically
        // from cheapScore's addend and routeAndScoreAt's subtraction (design §3.4), so whatever this
        // returns cancels out exactly regardless of the flag, keeping exactScore unaffected.
        if (problem.abutment_credit) congestion *= (1.0 - abutmentCredit(a, b));
        penalty += congestion;

        Rect bbox{std::min(a.cx(), b.cx()), std::min(a.cy(), b.cy()),
                  std::max(1.0, std::abs(a.cx() - b.cx())), std::max(1.0, std::abs(a.cy() - b.cy()))};
        for (std::size_t i = 0; i < problem.blocks.size(); ++i) {
            if (static_cast<int>(i) == bundle.source || static_cast<int>(i) == bundle.target) continue;
            if (!isSoft(problem.blocks[i].type)) continue;
            if (!intersectsOpen(bbox, placement.rects[i])) continue;
            double base = problem.blocks[i].ft_base_area > 0.0 ? problem.blocks[i].ft_base_area : problem.blocks[i].area;
            double required_area = ftRequiredArea(base, ftExtend(bundle.nets, ftTierRate(problem.blocks[i], bundle.nets)));
            // Proxy intentionally skips round2: cheap search gradient, not the scored quantity (exact path in routing.cpp rounds).
            double actual_area = placement.rects[i].w * placement.rects[i].h;
            penalty += std::max(0.0, required_area - actual_area) * 1000.0;
        }
    }
    return penalty;
}

// Crude v1 whitespace/channel-demand proxy (design p1-design.md §1.3): for each bundle, reserve
// area proportional to net count and the *aligned* axis distance between source/target centers
// (min(dx,dy) -- zero when the pair is already adjacent on one axis, so no margin is charged).
// Deliberately crude: biases cheapScore's SA gradient away from packings so tight they would
// overflow, not a routing prediction. Clamped so the shrink-wrapped outline_area (W*H) plus this
// margin never exceeds the MAX outline area -- the containment penalty (vs MAX) stays the hard
// legality gate; this term only shapes the gradient between the shrunk area and MAX.
//
// C1 milestone: no longer ADDED into outline_area in either tier (cheapScore/routeAndScoreAt below
// now both use plain w*h -- channels are interior to w*h, and the official checker's OutlineArea is
// the used die area with no margin term). Kept defined, unused, per instruction -- not deleted --
// hence [[maybe_unused]] to stay -Wall/-Wextra/-pedantic clean.
[[maybe_unused]] double channelMarginProxy(const Problem& problem, const Placement& placement, double w, double h) {
    double margin = 0.0;
    for (const auto& bundle : problem.bundles) {
        const auto& a = placement.rects[bundle.source];
        const auto& b = placement.rects[bundle.target];
        double dx = std::abs(a.cx() - b.cx());
        double dy = std::abs(a.cy() - b.cy());
        margin += (static_cast<double>(bundle.nets) / 25.0) * std::min(dx, dy);
    }
    double max_area = problem.outline_w * problem.outline_h;
    double headroom = std::max(0.0, max_area - w * h);
    return std::min(margin, headroom);
}

}  // namespace

Score cheapScore(const Problem& problem, const Placement& placement) {
    Score score;
    double w = placement.outline_w;
    double h = placement.outline_h;
    if (w <= 0.0 || h <= 0.0) std::tie(w, h) = usedOutline(problem, placement);
    // C1 milestone: plain W*H, no channelMarginProxy addend -- channels are interior to the
    // outline, and the official checker's OutlineArea is the used die area with no margin term.
    // Matches the exact tier (routeAndScoreAt below) so both tiers see the same area gradient.
    score.outline_area = w * h;
    double penalty = 0.0;
    for (std::size_t i = 0; i < placement.rects.size(); ++i) {
        const auto& r = placement.rects[i];
        if (r.x < 0.0 || r.y < 0.0 || r.right() > problem.outline_w || r.top() > problem.outline_h) penalty += 1e12;
        for (std::size_t j = i + 1; j < placement.rects.size(); ++j) {
            penalty += overlapArea(r, placement.rects[j]) * 1e9;
        }
    }
    // L2 (edge-free mode, docs/research/2026-07-30-final-levers-design.md §L2.3d): edge-LOCATION
    // gamma penalty, MOVED here from the exact tier (routeAndScoreAt below used to compute this same
    // term; deleted there -- see that function's own comment) so cheapScore, which runs every SA
    // iteration unlike the exact tier's periodic routeAndScoreAt, actually carries a gradient toward
    // freeing an edge block from a costly LOCATION anchor. Equivalent to the deleted exact-tier
    // block on every routeAndScoreAt call path: that function always calls cheapScore(scoped,
    // placement) with placement.outline_w/h == (w,h) == scoped.outline_w/h (stamped path: w,h are
    // read directly from placement.outline_w/h; unset path: shrinkWrapOutline stamps
    // placement.outline_w/h = fit.w/h before the w,h it returns are used; grow path: anchorAtOutline
    // stamps placement.outline_w/h = the exact target it was given) -- so the (w,h) resolved above
    // is bit-identical to what the deleted block computed via scoped.outline_w/h. Gated behind
    // problem.edge_soft_anchor, same contract as before: a flag-off run's cheapScore stays byte-
    // identical to pre-L2 (this loop never executes).
    if (problem.edge_soft_anchor) {
        double edge_location_penalty = 0.0;
        for (std::size_t i = 0; i < problem.blocks.size(); ++i) {
            if (!isEdge(problem.blocks[i].type)) continue;
            edge_location_penalty +=
                edgeLocationViolationDistance(placement.rects[i], w, h, problem.blocks[i].locations);
        }
        penalty += kGammaEdge * edge_location_penalty;
    }
    double wire_proxy = 0.0;
    for (const auto& b : problem.bundles) {
        const auto& a = placement.rects[b.source];
        const auto& c = placement.rects[b.target];
        double term = manhattan(a.cx(), a.cy(), c.cx(), c.cy()) * b.nets;
        // B5b (abutment-credit cheap-score upgrade, .superpowers/sdd/b5-design.md §3.2): scale the
        // WL proxy toward zero as (a,c) approach abutment. Gated behind problem.abutment_credit so
        // credit-off reproduces cheapScore's pre-B5b arithmetic BYTE-IDENTICALLY -- the multiplier
        // is skipped entirely via `if`, never merely multiplied by 1.0.
        if (problem.abutment_credit) term *= (1.0 - abutmentCredit(a, c));
        wire_proxy += term;
    }
    score.wirelength = wire_proxy;
    score.penalty = penalty + routeAwareProxyPenalty(problem, placement);
    score.value = score.outline_area + problem.alpha * wire_proxy + score.penalty;
    return score;
}

namespace {

// Builds the scoped Problem + score/routing for `placement` at declared outline (w,h) -- the
// common tail shared by exactScore's stamped path, its unset-recompute path, and the one-step
// corrective grow (design p1-design.md §1.4).
Score routeAndScoreAt(const Problem& problem, const Placement& placement, double w, double h,
                      RoutingResult* routing_out) {
    Problem scoped = problem;
    scoped.outline_w = w;
    scoped.outline_h = h;
    RoutingResult routing = routeProblem(scoped, placement);
    Score score = cheapScore(scoped, placement);
    // Beta Task 3 (self-consistency): this exact tier charges REAL routed overflow/FT (the V5 terms
    // below), so strip cheapScore's cheap ROUTE-AWARE overflow/FT PROXY -- keeping both would
    // double-charge congestion the exact router already measures. What remains of cheapScore's
    // penalty is pure legality (out-of-outline + pairwise overlap), which is 0 for a legal placement.
    // V5 (docs/research/2026-07-28-alpha-v5-cost-recalculation-analysis.md): a legal, zero-open
    // solution now scores EXACTLY `A + alpha*WL + officialOverflowPenalty(R,A) + kBetaFt*max(0,
    // ft_area_sum-A)` -- the property test/run_selfconsistency_regressions.sh proves against the
    // golden re-scorer (src/cfg_score.cpp, which independently reproduces the same
    // officialOverflowPenalty tail on the written .cfg). routeAwareProxyPenalty is deterministic, so
    // the recompute here is bit-identical to cheapScore's addend and cancels it exactly.
    score.penalty -= routeAwareProxyPenalty(scoped, placement);
    // C1 milestone: explicit plain W*H (the declared outline this call is scoping/scoring at),
    // overriding whatever cheapScore computed -- channels are interior to w*h, and FT expansion is
    // already captured by shrinkWrapOutline, so no margin term belongs in the official OutlineArea.
    score.outline_area = w * h;
    score.wirelength = routing.total_wirelength;
    score.penalty += routing.open_routes * 1e15;
    // V5 (docs/research/2026-07-28-alpha-v5-cost-recalculation-analysis.md, sections 1.3/1.4;
    // byte-verified against the official "PATH Detail Cost Calculation" page): the scorer charges the
    // OFFICIAL checker's GLOBAL overflow-rate penalty via the exact piecewise tail
    // (officialOverflowPenalty, route_cost.hpp) applied to R = channel_overflow_nets /
    // channel_capacity_total (routing.cpp, RAW 25*h/25*w capacity, nets units) -- deliberately
    // DECOUPLED from the router's own per-channel area-denominated marginal pricing (route_cost.hpp's
    // kOverflowAreaWeight/channelMarginalCost, which still guides the router toward low-overflow
    // paths but is no longer required to equal this weight). The FT term is now the AGGREGATE excess
    // of ft_area_sum (routing.cpp's computeOverflowAndMismatch, summed over EVERY block) over the
    // outline area, replacing the retired flat per-block feedthrough_mismatch charge.
    double R = routing.channel_capacity_total > 0.0
             ? routing.channel_overflow_nets / routing.channel_capacity_total : 0.0;
    score.penalty += officialOverflowPenalty(R, score.outline_area);
    score.penalty += kBetaFt * std::max(0.0, routing.ft_area_sum - score.outline_area);
    // L2 (edge-free mode, docs/research/2026-07-30-final-levers-design.md §L2.3d): the exact-tier
    // edge-LOCATION gamma penalty that used to be computed HERE (G4) is deleted -- it is now charged
    // by cheapScore itself (see cheapScore's own L2 comment above), and this function's score
    // already carries it via `score.penalty -= routeAwareProxyPenalty(...)` above, which only strips
    // the ROUTE-AWARE proxy, not gamma. Recomputing it again here would double-charge every edge
    // block's LOCATION violation. Proven equivalent to the deleted block (not just non-doubled): see
    // cheapScore's comment for why the (w,h) it resolves is bit-identical to this function's own
    // (w,h) on every call path.
    score.value = score.outline_area + problem.alpha * score.wirelength + score.penalty;
    if (routing_out) *routing_out = routing;
    return score;
}

}  // namespace

Score exactScore(const Problem& problem, const Placement& placement, RoutingResult* routing_out,
                 Placement* placement_out) {
    // Scope routing to the placement's STAMPED declared outline (design §1.4) -- not a bbox
    // recomputed from usedOutline, which ignores the shrink-wrap entirely. When unset (0,0; e.g.
    // a hand-built placement, or one whose stamp went stale -- see compactTowardOrigin's reset),
    // recompute via the exact shrinkWrapOutline on a LOCAL copy: exactScore takes `placement` by
    // const&, and shrinkWrapOutline mutates (re-anchors edge rects to the shrunk border), so it
    // must never touch the caller's placement. Route/score the local copy's rects, since edge
    // re-anchoring changes which rects touch which channels.
    Placement local = placement;
    double w = local.outline_w;
    double h = local.outline_h;
    if (w <= 0.0 || h <= 0.0) {
        OutlineFit fit = shrinkWrapOutline(problem, local);
        w = fit.w;
        h = fit.h;
    }

    RoutingResult routing;
    Score score = routeAndScoreAt(problem, local, w, h, &routing);

    // One-step corrective grow (design §1.4): if the declared outline was too tight to route,
    // grow once and re-route. Target size scales toward MAX by the worse of a flat +5% floor or a
    // margin sized to the observed channel_overflow (overflow/25, matching channelMarginProxy's
    // nets-per-25 capacity unit), clamped to MAX.
    // T7.1 bugfix: this used to scope a Problem whose ceiling equals the grown target and call
    // shrinkWrapOutline -- but shrinkWrapOutline is a minimizer that bisects back DOWN to the tight
    // legal frontier regardless of the ceiling given, so `regrow` was silently re-stamped at ~the
    // SAME tight outline every time, never actually growing (this open-route relief path was dead
    // code). anchorAtOutline anchors directly at the grown target (no bisection) instead.
    if (routing.open_routes > 0) {
        double margin = std::max(0.05, routing.channel_overflow / 25.0 / std::max(w, h));
        // Part D (T5 integer-leak rider): the grown target feeds anchorAtOutline, which stamps it
        // exactly -- ceil to integers (MAX is integer) so a winning grown outline stays integral in
        // the emitted cfg and the score stays self-consistent with the golden rescorer's
        // integer-outline recompute. Clamp to MAX, matching the fractional form this replaces.
        double grown_w = std::min(problem.outline_w, std::ceil(w * (1.0 + margin) - kEps));
        double grown_h = std::min(problem.outline_h, std::ceil(h * (1.0 + margin) - kEps));

        Problem ceiling = problem;
        ceiling.outline_w = grown_w;
        ceiling.outline_h = grown_h;
        Placement regrow = local;
        OutlineFit grown_fit = anchorAtOutline(ceiling, regrow);

        if (grown_fit.legal) {
            RoutingResult grown_routing;
            Score grown_score = routeAndScoreAt(problem, regrow, grown_fit.w, grown_fit.h, &grown_routing);

            bool prefer_grown;
            if (routing.open_routes != grown_routing.open_routes) {
                prefer_grown = grown_routing.open_routes == 0;
            } else {
                prefer_grown = grown_score.value < score.value;
            }
            if (prefer_grown) {
                local = regrow;
                score = grown_score;
                routing = grown_routing;
            }
        }
    }

    if (routing_out) *routing_out = routing;
    // Surface the placement actually scored/routed (design §1.4 consistency fix): `local` is the
    // input when the grow never fired, or the grown+re-anchored geometry when it did. Its outline
    // stamp is integral in the grow path -- anchorAtOutline stamps EXACTLY grown_w/grown_h, and both
    // are ceil'd integers above (Part D). Callers that keep the returned score/routing for emission
    // or comparison store THIS placement, never the raw input, so channels/paths never reference
    // geometry outside the emitted BLOCK rects' outline.
    if (placement_out) *placement_out = local;
    return score;
}

}  // namespace efgr
