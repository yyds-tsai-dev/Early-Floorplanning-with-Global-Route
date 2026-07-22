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
// .superpowers/sdd/beta-task-2-report.md): the checker's accounting is NOT the guide-segment model
// in route_geometry.hpp (`accumulatePathUsage` / `guidePoint`). Two concrete differences, both
// reproduced here and NOT via the shared helpers:
//   1. Channel demand is PER-EDGE: each entry/exit edge a route uses on a channel charges `nets` to
//      that edge's direction (edge 1/3 = left/right -> horizontal, 2/4 = top/bottom -> vertical), so
//      a straight pass-through double-charges. `accumulatePathUsage`'s dx/dy guide-segment rule
//      under-counts pass-throughs and mis-attributes L-turns.
//   2. Wirelength uses a CENTER-anchored guiding point (midpoint of the two nodes' facing-edge
//      midpoints, snapped to the shared border chosen by minimum gap -- rounding-robust for the
//      .cfg's 3-decimal coordinates), summed as a manhattan CHAIN, weighted by nets.
//      `guidePoint` instead uses the overlap-segment midpoint, which is ~6% off.
// The solver-side port of this per-edge/center-anchored model is Task 3's scope; routing.cpp is
// deliberately untouched here.

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
    double overflow_penalty = 0.0;   // 20*A*R + 1.5*A*max(0, R-0.05)
    double ft_penalty = 0.0;         // 0 for the alpha package (no FT)
    double checker_cost = 0.0;       // A + problem.alpha*WL + overflow_penalty
    std::map<std::string, std::pair<double, double>> demand_by_channel;  // name -> (H, V)
};

// Score a parsed .cfg against the official cost model. Uses problem.alpha (CSV alpha) for the cost.
OfficialBreakdown officialScoreCfg(const Problem& problem, const CfgSolution& cfg);

}  // namespace efgr
