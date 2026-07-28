#pragma once

#include <map>
#include <string>
#include <utility>
#include <vector>

#include "model.hpp"

namespace efgr {

// Beta Task 2: golden .cfg re-scorer, locked to the official alpha-test checker (report.json).
//
// This is a CALIBRATION reference, independent of the solver's own routing/eval accounting. It
// reproduces the official checker's four measured quantities from a written .cfg:
//   * total_wirelength      -- exact to 1e-6 rel on all 5 alpha cases
//   * channel_overflow_total, channel_total_capacity, per-channel demand -- exact on all 5 cases
//   * cost                  -- exact on cases 0/2/3/4; case1 within 0.31% (penalty tail, see .cpp)
//
// IMPORTANT (verified against the official report.json + viz + E_20260608.pdf; see
// .superpowers/sdd/beta-task-2-report.md): this file is a CALIBRATION ORACLE, deliberately kept
// independent of the solver's own headers -- it duplicates both the accounting constants
// (kBetaLin/kBetaTail/kThreshold/kCapPerUm above) and the guiding-point model (guidingPoint below)
// locally rather than including route_geometry.hpp, so it remains independent ground truth even if
// the solver's own accounting were to regress. The two things it locks down:
//   1. Channel demand is PER-EDGE: each entry/exit edge a route uses on a channel charges `nets` to
//      that edge's direction (edge 1/3 = left/right -> horizontal, 2/4 = top/bottom -> vertical), so
//      a straight pass-through double-charges.
//   2. Wirelength uses a CENTER-anchored guiding point (midpoint of the two nodes' facing-edge
//      midpoints, snapped to the shared border chosen by minimum gap -- rounding-robust for the
//      .cfg's 3-decimal coordinates), summed as a manhattan CHAIN, weighted by nets.
// Post-T3, route_geometry.hpp's `accumulatePathUsage`/`guidingPoint` implement this SAME model (the
// old overlap-midpoint `guidePoint` is retired), so routing.cpp's committed accounting now agrees
// with this oracle by construction -- not by assumption: test/run_selfconsistency_regressions.sh
// gates that agreement and fails if the solver's routed accounting ever diverges from this
// independent re-scorer on the same .cfg.

struct CfgSolution {
    double outline_w = 0.0;
    double outline_h = 0.0;
    std::map<std::string, Rect> blocks;   // BLOCK section, by name
    std::vector<Channel> channels;        // CHANNEL section (name + rect), in file order
    struct Path {
        double nets = 0.0;
        std::vector<std::pair<std::string, int>> steps;  // RAW (object, edge) tokens, not deduped
    };
    std::vector<Path> paths;              // PATH section
};

// Parse a .cfg (OUTLINE / BLOCK / CHANNEL / PATH sections). CRLF-safe (every token trimmed);
// accepts the solver's own 3-decimal fixed floats. Values are doubles (never assumed integer).
CfgSolution parseCfg(const std::string& path);

struct OfficialBreakdown {
    double outline_area = 0.0;
    double wirelength = 0.0;
    double overflow_nets = 0.0;
    double capacity_nets = 0.0;
    double rate = 0.0;               // overflow_nets / capacity_nets
    double overflow_penalty = 0.0;   // officialOverflowPenalty(rate, outline_area) -- V5 piecewise tail
    double ft_penalty = 0.0;         // V5 aggregate FT slack penalty; 0 on the alpha package (no FT used)
    double checker_cost = 0.0;       // A + problem.alpha*WL + overflow_penalty
    std::map<std::string, std::pair<double, double>> demand_by_channel;  // name -> (H, V)
};

// V5 overflow-penalty tail (docs/research/2026-07-28-alpha-v5-cost-recalculation-analysis.md,
// section 1.3): rate <= kThreshold(0.05) -> beta_lin*outline_area*rate; else the linear part caps
// at kThreshold and a sqrt tail (beta_tail*sqrt(rate-kThreshold)*outline_area) takes over. Exposed
// standalone (not file-local) so it can be tested directly against the historical R>threshold
// datapoint (case1's pre-V5 report.json overflow_ft_penalty, which V5 reproduces exactly).
double officialOverflowPenalty(double rate, double outline_area);

// Score a parsed .cfg against the official cost model. Uses problem.alpha (CSV alpha) for the cost.
OfficialBreakdown officialScoreCfg(const Problem& problem, const CfgSolution& cfg);

}  // namespace efgr
