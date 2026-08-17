#pragma once

#include <vector>

#include "model.hpp"

namespace efgr {

// C1a (fixed-topology WL-driven whitespace redistribution, .superpowers/sdd/c1-design-20260817.md
// §1/§8): observation-only counters from one wlRedistributeIterates call -- `sweeps_run` is the
// number of (H-then-V) sweep pairs actually executed (1..max_sweeps, whichever the fixpoint break
// or the max_sweeps cap stops at); `blocks_moved` is the cumulative count of individual block moves
// across every one of those sweep passes (H and V summed). Purely a diagnostic for callers/tests --
// never consulted by the enumerator's own control flow.
struct WlRedistributeStats {
    int sweeps_run = 0;
    int blocks_moved = 0;
};

// C1a (design §1 "Iteration" + §8 file plan): pure enumerator -- runs up to `max_sweeps` (default
// 8, design's K) alternating-direction Gauss-Seidel weighted-median sweeps over `base`'s movable
// (non-anchored-edge) block coordinates, one axis at a time (H then V per sweep, mirroring
// respace.cpp's relaxOneAxis ordering rationale: V reads POST-H geometry), and returns the
// placements after sweep 1, sweep 2 (if reached), and the FINAL sweep (whichever index the loop
// stops at, deduplicated against sweep 1/2 by sweep index -- NOT by placementKey content, since
// this file deliberately has no dependency on search.hpp/search.cpp; a caller wanting content-level
// dedup does its own placementKey pass, per design §2's "Rung ladder"). `base` is NEVER mutated;
// every returned Placement carries `base`'s own outline_w/outline_h stamp UNCHANGED (C1 never
// re-stamps the outline) and every block's w/h UNCHANGED (soft blocks are a pure position lever
// here, design §3) -- only x/y coordinates of movable blocks are ever touched. Anchored-edge blocks
// (isAnchoredEdge) are never moved but ARE included as fixed obstacles when computing every other
// block's feasible window (design §3, improves on P3's respace.cpp omission).
//
// Returns EMPTY when: `base.outline_w`/`outline_h` is unset (<=kEps -- "stamp unset"); `base` is
// already geometrically illegal (out-of-outline or pairwise-overlapping -- "base illegal");
// `problem.bundles` is empty or no block is movable ("no bundles/movable blocks" -- the primary
// objective and wireMedianTarget are both meaningless with nothing to move or nothing to align
// toward); or sweep 1 (the very first H+V pass) moved zero blocks ("sweep 1 moved nothing" --
// design's pre-gate, zero exact evals). Deterministic: no RNG anywhere in the sweep.
std::vector<Placement> wlRedistributeIterates(const Problem& problem, const Placement& base,
                                              int max_sweeps = 8, WlRedistributeStats* stats = nullptr);

}  // namespace efgr
