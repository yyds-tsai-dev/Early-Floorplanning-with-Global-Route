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

// Named weights (T0 deferred item): match eval.cpp's shared 1e6 multipliers exactly (routing.cpp's
// area-denominated channel_overflow and feedthrough_mismatch, both um^2). Do NOT change eval.cpp's
// literals in this task (T6 wires that consistency); these constants exist so this header and any
// future router code reference a name instead of repeating the magic number.
constexpr double kOverflowAreaWeight = 1e6;  // per um^2 of channel overflow area (eval.cpp:145)
constexpr double kFtMismatchWeight = 1e6;    // per um^2 of FT required-area shortfall (eval.cpp:146)

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

}  // namespace efgr
