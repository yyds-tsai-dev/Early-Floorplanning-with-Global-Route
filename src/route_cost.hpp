#pragma once

#include <algorithm>
#include <cmath>

#include "ft.hpp"
#include "model.hpp"

namespace efgr {

// P2 Task 1 (design p2-design.md §Q1 / §3; plan Global Constraint 1/3/4): pure cost-unit routing
// edge functions. Mirrors the ft.hpp pattern -- header-only, no mutation, no I/O. These give the
// (future) cost-driven Dijkstra pass edge weights denominated in the SAME units the scorer charges
// (eval.cpp:145-146), so the shortest path is the min-Delta-Cost path.

// Named weights: ROUTER-SIDE marginal pricing only (channelMarginalCost/softFtMarginalCost below,
// and the cost-driven Dijkstra's history surcharge / round-selection roundCost in routing.cpp).
// These area-denominated per-channel proxies (routing.cpp's channel_overflow and
// feedthrough_mismatch, both um^2) are what the router uses internally to find low-overflow /
// low-FT-mismatch PATHS -- a local, per-edge marginal signal for search, not the scorer's cost.
// C1 milestone: eval.cpp's exact tier no longer uses these weights at all. It charges a GLOBAL,
// checker-faithful penalty instead -- kBetaLin/kBetaTail (below) applied to R =
// channel_overflow_nets/channel_capacity_total (routing.cpp, RAW 25*h/25*w capacity), plus a flat
// kBetaFt * feedthrough_mismatch term. The router-scorer "consistency invariant" this header used
// to document is gone by design: the router's marginal proxy and the scorer's global penalty are
// deliberately decoupled now, and kOverflowAreaWeight/kFtMismatchWeight are kept unchanged here
// purely so the router's own search signal is undisturbed by the C1 scorer remap.
constexpr double kOverflowAreaWeight = 1e6;  // per um^2 of channel overflow area (router-side proxy only)
constexpr double kFtMismatchWeight = 1e6;    // per um^2 of FT required-area shortfall (router-side proxy only)

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

// Channel overflow AREA for a given direction at `usage` nets (design p2-design.md §8.1 / T0
// denomination, routing.cpp:221-243): horizontal usage is capacitated by 25*h (missing capacity is
// a height deficit spanning the channel's WIDTH); vertical usage is capacitated by 25*w (missing
// capacity is a width deficit spanning the channel's HEIGHT).
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
inline double channelMarginalCost(const Channel& ch, bool horizontal, double usage, double n) {
    double before = channelOverflowArea(ch, horizontal, usage);
    double after = channelOverflowArea(ch, horizontal, usage + n);
    return kOverflowAreaWeight * (after - before);
}

// Marginal FT penalty (um^2 * weight) of pushing `n` more crossing nets through soft block `b`
// whose accumulated crossing nets are `acc` and whose currently placed area is `placed_area`
// (design §3.3, plan Global Constraint 4: incremental tier pricing). Uses ftRequiredArea/ftExtend/
// ftTierRate (ft.hpp) so a tier jump (3000/6000/9000) is priced exactly, not linearly approximated.
// Free while `placed_area` already covers the required area at `acc` and stays free until the
// required area at `acc+n` exceeds it (slack absorption, design §3.3 subtlety 1).
inline double softFtMarginalCost(const Block& b, double acc, double n, double placed_area) {
    double base = b.ft_base_area > 0.0 ? b.ft_base_area : b.area;
    double required_before = ftRequiredArea(base, ftExtend(acc, ftTierRate(b, acc)));
    double required_after = ftRequiredArea(base, ftExtend(acc + n, ftTierRate(b, acc + n)));
    double mismatch_before = std::max(0.0, required_before - placed_area);
    double mismatch_after = std::max(0.0, required_after - placed_area);
    return kFtMismatchWeight * (mismatch_after - mismatch_before);
}

// Wirelength term in Cost units for a hop of guiding-point distance `dist` carrying `n` nets
// (design §3.1: alpha*n*manhattan monotone WL proxy, same units as the area terms above).
inline double wireCost(double alpha, double n, double dist) { return alpha * n * dist; }

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

}  // namespace efgr
