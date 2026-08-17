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

// L1a/L1c (cheap-tier objective realignment, case5-design.md §2.4/§4-L1a/§4-L1c,
// .superpowers/sdd/case5-l1-brief.md): replaces v1's routeAwareProxyPenalty above -- the FT-bbox
// loop AND the congestion term -- WHOLESALE under problem.cheap_proxy_v2. L1c sweeps cut lines
// across both axes (design §4-L1c pseudo-code): vertical cuts are midpoints between consecutive
// distinct block x-edges that lie strictly inside (0,w) (symmetric y-cuts inside (0,h)). Per cut,
// `demand` is the net count of every bundle whose source/target CENTERS straddle the cut, and `cap`
// is the raw 25*free-perpendicular-span channel capacity (routing.cpp:544-564's capacity unit),
// clamped >=0 so an illegal mid-SA overlap (free span negative) cannot earn negative capacity.
// Feedthrough has no capacity limit in routeProblem (routing.cpp:94-98) -- only priced by the
// aggregate FT area (L1a) -- so demand a cut cannot carry through channels is charged as true
// (hard) overflow ONLY when no soft block covers the cut; otherwise it becomes estimated
// feedthrough load (est_ft_nets), distributed across the covering soft blocks weighted by their
// perpendicular side (height for a vertical/x-cut, width for a horizontal/y-cut) -- the same
// measure used to compute the covering deduction, per the design's own weighting. L1a then prices
// est_ft_nets exactly as the exact tier's own aggregate FT term (routing.cpp:594-604's
// rr.ft_area_sum recompute, eval.cpp's kBetaFt*max(0, ft_area_sum-OutlineArea) at the
// routeAndScoreAt site below): kBetaFt*max(0.0, est_ft_area_sum - w*h). The v2 penalty returned is
// exactly L1c's officialOverflowPenalty(hard_overflow/max(1,cap_total), w*h) + L1a's FT term --
// nothing else (brief pinned facts 3/4). Pure and deterministic -- per-call locals only, no
// statics -- so cheapScore's addend (below) and routeAndScoreAt's subtraction (eval.cpp) cancel
// exactly regardless of which (w,h) this is called at.
double routeAwareProxyPenaltyV2(const Problem& problem, const Placement& placement, double w, double h) {
    const std::size_t n = problem.blocks.size();
    std::vector<double> est_ft_nets(n, 0.0);
    double hard_overflow = 0.0;
    double cap_total = 0.0;

    // One axis sweep shared by the vertical (x-cut) and horizontal (y-cut) passes. `vertical`
    // selects the cut axis; `span` is that axis's own outline extent (w for x-cuts, h for y-cuts),
    // used only to clip candidate cut coordinates to the open interval (0, span) per the design's
    // "xs = ... intersect (0, w)" pseudo-code. The PERPENDICULAR extent (h for x-cuts, w for
    // y-cuts) is what a covering block consumes from free channel span and what an FT-diverted
    // block is weighted by.
    auto sweep = [&](bool vertical, double span) {
        std::vector<double> edges;
        edges.reserve(2 * n);
        for (std::size_t i = 0; i < n; ++i) {
            const auto& r = placement.rects[i];
            double lo = vertical ? r.x : r.y;
            double hi = vertical ? r.right() : r.top();
            if (lo > kEps && lo < span - kEps) edges.push_back(lo);
            if (hi > kEps && hi < span - kEps) edges.push_back(hi);
        }
        std::sort(edges.begin(), edges.end());
        edges.erase(std::unique(edges.begin(), edges.end(),
                                 [](double a, double b) { return std::abs(a - b) < kEps; }),
                    edges.end());

        for (std::size_t k = 0; k + 1 < edges.size(); ++k) {
            double cut = 0.5 * (edges[k] + edges[k + 1]);

            double covered = 0.0;
            std::vector<std::size_t> softs;
            double soft_weight_total = 0.0;
            for (std::size_t i = 0; i < n; ++i) {
                const auto& r = placement.rects[i];
                double lo = vertical ? r.x : r.y;
                double hi = vertical ? r.right() : r.top();
                if (lo < cut && cut < hi) {
                    double perp = vertical ? r.h : r.w;
                    covered += perp;
                    if (isSoft(problem.blocks[i].type)) {
                        softs.push_back(i);
                        soft_weight_total += perp;
                    }
                }
            }

            double free_span = (vertical ? h : w) - covered;
            double cap = std::max(0.0, 25.0 * free_span);
            cap_total += cap;

            double demand = 0.0;
            for (const auto& bundle : problem.bundles) {
                const auto& a = placement.rects[bundle.source];
                const auto& b = placement.rects[bundle.target];
                double ca = vertical ? a.cx() : a.cy();
                double cb = vertical ? b.cx() : b.cy();
                if ((ca < cut) != (cb < cut)) demand += bundle.nets;
            }

            double excess = std::max(0.0, demand - cap);
            if (!softs.empty() && soft_weight_total > kEps) {
                for (std::size_t i : softs) {
                    double perp = vertical ? placement.rects[i].h : placement.rects[i].w;
                    est_ft_nets[i] += excess * (perp / soft_weight_total);
                }
            } else {
                // No soft block covers this cut (or the covering softs are all degenerate-width) --
                // there is nowhere for the excess to feedthrough, so it is true channel overflow.
                hard_overflow += excess;
            }
        }
    };

    sweep(/*vertical=*/true, w);
    sweep(/*vertical=*/false, h);

    // L1a: mirror routing.cpp:594-604's exact-tier aggregate FT recompute, using est_ft_nets in
    // place of the routed rr.soft_feedthrough_nets.
    double est_ft_area_sum = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        const Block& block = problem.blocks[i];
        if (isSoft(block.type)) {
            double base = block.ft_base_area > 0.0 ? block.ft_base_area : block.area;
            double nets_i = est_ft_nets[i];
            est_ft_area_sum += ftRequiredArea(base, ftExtend(nets_i, ftTierRate(block, nets_i)));
        } else {
            est_ft_area_sum += block.area;
        }
    }

    double R = hard_overflow / std::max(1.0, cap_total);
    return officialOverflowPenalty(R, w * h) + kBetaFt * std::max(0.0, est_ft_area_sum - w * h);
}

// D4/A5 pack (notch/pocket whitespace penalty, .superpowers/sdd/levers-batch2-briefs-20260817.md
// Brief 3): additive regularizer over the OUTLINE's empty (uncovered) area -- charges small,
// roughly-square pockets of whitespace that no bundle's channel routing can realistically use
// (both dimensions well under a tenth of the outline), while leaving genuine channels (long, thin
// in one dimension) untouched. OUR ADAPTATION of the notch/pocket recipe's single-dimension rule:
// counts a component only when BOTH bbox_w < w/10 AND bbox_h < h/10 -- a single-dim rule would
// punish legitimate long-thin channels. Orthogonal to v1/v2 (routeAwareProxyPenalty/V2 above):
// hooked OUTSIDE that branch by both call sites (cheapScore/routeAndScoreAt below), an independent
// additive term. MUST NOT read problem.outline_w/h (only the explicit w,h arguments) -- the
// cancellation invariant (cheapScore's addend at (w,h) exactly cancelling routeAndScoreAt's
// subtraction at the same (w,h), on a `scoped` Problem that differs from `problem` ONLY in
// outline_w/h) depends on this function being blind to which Problem it was handed; the `problem`
// parameter is unnamed for exactly this reason (nothing in the body may read it). Pure and
// deterministic: no RNG, no mutation.
//
// Algorithm: coordinate-compress the outline into a grid using every rect's clamped x/y edges (plus
// the outline's own 0/w and 0/h bounds) as cut lines -- at most 62x62 cells (kEps-deduped, a grid-
// size guard: a placement with a pathological number of distinct edges is not this regularizer's
// problem to solve, so it returns 0 rather than blow up an O(cells) fill); mark each cell covered by
// some block's rect as occupied (index-range fill per block, O(n + cells)); 4-connected flood fill
// over the UNoccupied cells finds empty connected components, each with a real-coordinate bounding
// box; a component "counts" (is a notch/pocket, not a channel) iff BOTH its bbox dimensions are
// under a tenth of the corresponding outline dimension; each counted component contributes
// kNotchWeight*sqrt(w*h*bbox_w*bbox_h) (the recipe's sublinear shape, scaled by the pocket's own
// footprint against the whole outline).
constexpr double kNotchWeight = 0.25;

double notchPocketPenalty(const Problem&, const Placement& placement, double w, double h) {
    if (w <= 0.0 || h <= 0.0) return 0.0;

    std::vector<double> xs = {0.0, w};
    std::vector<double> ys = {0.0, h};
    for (const auto& r : placement.rects) {
        if (r.w <= 0.0 || r.h <= 0.0) continue;
        xs.push_back(std::clamp(r.x, 0.0, w));
        xs.push_back(std::clamp(r.right(), 0.0, w));
        ys.push_back(std::clamp(r.y, 0.0, h));
        ys.push_back(std::clamp(r.top(), 0.0, h));
    }
    auto uniqSort = [](std::vector<double>& v) {
        std::sort(v.begin(), v.end());
        v.erase(std::unique(v.begin(), v.end(),
                             [](double a, double b) { return std::abs(a - b) < kEps; }),
                v.end());
    };
    uniqSort(xs);
    uniqSort(ys);
    if (xs.size() < 2 || ys.size() < 2 || xs.size() > 63 || ys.size() > 63) return 0.0;

    const int nx = static_cast<int>(xs.size()) - 1;
    const int ny = static_cast<int>(ys.size()) - 1;
    std::vector<char> occ(static_cast<std::size_t>(nx) * static_cast<std::size_t>(ny), 0);

    auto edgeIndex = [](const std::vector<double>& v, double target) {
        auto it = std::lower_bound(v.begin(), v.end(), target - kEps);
        return static_cast<int>(it - v.begin());
    };

    for (const auto& r : placement.rects) {
        if (r.w <= 0.0 || r.h <= 0.0) continue;
        double x0 = std::clamp(r.x, 0.0, w);
        double x1 = std::clamp(r.right(), 0.0, w);
        double y0 = std::clamp(r.y, 0.0, h);
        double y1 = std::clamp(r.top(), 0.0, h);
        if (x1 - x0 <= kEps || y1 - y0 <= kEps) continue;
        int cx0 = edgeIndex(xs, x0);
        int cx1 = edgeIndex(xs, x1);
        int cy0 = edgeIndex(ys, y0);
        int cy1 = edgeIndex(ys, y1);
        for (int cy = cy0; cy < cy1; ++cy) {
            for (int cx = cx0; cx < cx1; ++cx) {
                occ[static_cast<std::size_t>(cy) * nx + cx] = 1;
            }
        }
    }

    std::vector<char> visited(occ.size(), 0);
    std::vector<int> stack;
    double penalty = 0.0;
    for (int scan_y = 0; scan_y < ny; ++scan_y) {
        for (int scan_x = 0; scan_x < nx; ++scan_x) {
            std::size_t start = static_cast<std::size_t>(scan_y) * nx + scan_x;
            if (occ[start] || visited[start]) continue;
            stack.clear();
            stack.push_back(scan_y * nx + scan_x);
            visited[start] = 1;
            double bbox_x0 = xs[scan_x];
            double bbox_x1 = xs[scan_x + 1];
            double bbox_y0 = ys[scan_y];
            double bbox_y1 = ys[scan_y + 1];
            while (!stack.empty()) {
                int cell = stack.back();
                stack.pop_back();
                int cx = cell % nx;
                int cy = cell / nx;
                bbox_x0 = std::min(bbox_x0, xs[cx]);
                bbox_x1 = std::max(bbox_x1, xs[cx + 1]);
                bbox_y0 = std::min(bbox_y0, ys[cy]);
                bbox_y1 = std::max(bbox_y1, ys[cy + 1]);
                const int dx[4] = {-1, 1, 0, 0};
                const int dy[4] = {0, 0, -1, 1};
                for (int d = 0; d < 4; ++d) {
                    int ncx = cx + dx[d];
                    int ncy = cy + dy[d];
                    if (ncx < 0 || ncx >= nx || ncy < 0 || ncy >= ny) continue;
                    std::size_t nidx = static_cast<std::size_t>(ncy) * nx + ncx;
                    if (occ[nidx] || visited[nidx]) continue;
                    visited[nidx] = 1;
                    stack.push_back(ncy * nx + ncx);
                }
            }
            double bbox_w = bbox_x1 - bbox_x0;
            double bbox_h = bbox_y1 - bbox_y0;
            if (bbox_w < w / 10.0 && bbox_h < h / 10.0) {
                penalty += kNotchWeight * std::sqrt(w * h * bbox_w * bbox_h);
            }
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
        if (problem.cheap_proxy_v2) {
            // L1b (boundary-gap WL proxy, case5-design.md §4-L1b, .superpowers/sdd/case5-l1-
            // brief.md): facing-border separation -- zero when the pair already abuts, matching
            // route_geometry.hpp:93-94's "a path of <2 nodes has 0 wirelength" -- replacing v1's
            // centre-to-centre Manhattan distance below, which is blind to abutment. abutment_credit
            // is INERT under v2 (not consulted): the credit exists to approximate exactly this gap
            // and would double-count if also applied here.
            double dx = std::max(0.0, std::max(a.x, c.x) - std::min(a.right(), c.right()));
            double dy = std::max(0.0, std::max(a.y, c.y) - std::min(a.top(), c.top()));
            wire_proxy += (dx + dy) * b.nets;
        } else {
            double term = manhattan(a.cx(), a.cy(), c.cx(), c.cy()) * b.nets;
            // B5b (abutment-credit cheap-score upgrade, .superpowers/sdd/b5-design.md §3.2): scale
            // the WL proxy toward zero as (a,c) approach abutment. Gated behind
            // problem.abutment_credit so credit-off reproduces cheapScore's pre-B5b arithmetic
            // BYTE-IDENTICALLY -- the multiplier is skipped entirely via `if`, never merely
            // multiplied by 1.0.
            if (problem.abutment_credit) term *= (1.0 - abutmentCredit(a, c));
            wire_proxy += term;
        }
    }
    score.wirelength = wire_proxy;
    // L1 (case5-design.md §4-L1, .superpowers/sdd/case5-l1-brief.md): v2 replaces v1's
    // routeAwareProxyPenalty (FT-bbox loop + congestion term) wholesale with
    // routeAwareProxyPenaltyV2's cut-line-sweep FT/overflow estimate, at the SAME (w,h) this
    // function already resolved above -- required so routeAndScoreAt's subtraction (same branch,
    // same (w,h)) cancels this addend exactly (design invariant, brief pinned fact 1). An `if`,
    // never a multiply-by-one, so flag-off stays byte-identical to pre-L1.
    if (problem.cheap_proxy_v2) {
        score.penalty = penalty + routeAwareProxyPenaltyV2(problem, placement, w, h);
    } else {
        score.penalty = penalty + routeAwareProxyPenalty(problem, placement);
    }
    // D4/A5 pack (notch/pocket whitespace penalty): orthogonal to v1/v2 above -- hooked OUTSIDE
    // that branch, an independent additive term. An `if`, never a multiply-by-one, so flag-off
    // stays byte-identical to pre-D4/A5.
    if (problem.notch_penalty) score.penalty += notchPocketPenalty(problem, placement, w, h);
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
    // L1 (case5-design.md §4-L1d, .superpowers/sdd/case5-l1-brief.md): branches identically to
    // cheapScore's addend above (same flag, same (w,h) -- routeAndScoreAt's own parameters, proven
    // bit-identical to what cheapScore(scoped, placement) resolves internally on every call path,
    // per the L2 comment on cheapScore's (w,h) resolution above) so the pair cancels exactly and
    // the exact tier is provably unchanged by this lever either way.
    if (problem.cheap_proxy_v2) {
        score.penalty -= routeAwareProxyPenaltyV2(scoped, placement, w, h);
    } else {
        score.penalty -= routeAwareProxyPenalty(scoped, placement);
    }
    // D4/A5 pack (notch/pocket whitespace penalty): mirrors cheapScore's own addend above at the
    // SAME (w,h) -- notchPocketPenalty never reads problem.outline_w/h (only the explicit
    // arguments), so this subtraction is bit-identical to what cheapScore(scoped, placement)
    // resolved internally, cancelling the addend exactly (design invariant: the exact tier is
    // provably unchanged by this lever either way).
    if (problem.notch_penalty) score.penalty -= notchPocketPenalty(scoped, placement, w, h);
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
