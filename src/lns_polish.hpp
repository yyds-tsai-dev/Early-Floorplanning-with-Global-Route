#pragma once

#include <utility>
#include <vector>

#include "model.hpp"

namespace efgr {

// C3 (exact local-group LNS re-placement, .superpowers/sdd/c3-wlrec-briefs-20260817.md BRIEF A):
// picks a small (kLnsGroupSize) connectivity-driven group of MOVABLE (!isAnchoredEdge) blocks, then
// runs a node-capped exact branch-and-bound over the group's joint position assignment (candidate
// positions per member, A2's discrete-but-complete grid) against a lexicographic proxy objective
// (A3: primary = bundle-weighted facing-edge gap, secondary = bundle-weighted center Manhattan,
// same two-term proxy C1a's wl_redistribute.cpp already uses). Pure module: no dependency on
// routing.cpp/eval.cpp/search.cpp (RoutingResult is a model.hpp value type, not a routing.cpp
// function, so lnsSelectGroup can read routed wirelength/openness without linking routing.cpp) --
// links standalone with only placement_engine.cpp, same "new module" scope C1a's wl_redistribute.cpp
// established (test/run_lns_regressions.sh).

// A5: group size (kLnsGroupSize), round count (kLnsRounds), per-member candidate cap after A2's
// own[i][c]+crossMin ranking (kLnsCandCap), and the B&B's node budget (kLnsNodeBudget, A3 -- anytime,
// deliberately NO wall-clock guard: a node cap bounds work exactly and stays machine-independent,
// unlike a timer).
constexpr int kLnsGroupSize = 4;
constexpr int kLnsRounds = 2;
constexpr int kLnsCandCap = 32;
constexpr int kLnsNodeBudget = 200000;

// Observation-only counters from one lnsPolishIterates call -- `nodes_expanded` counts every
// (depth, candidate) trial the B&B actually visited (whether or not that candidate turned out
// feasible against already-decided group members), the same "node" unit kLnsNodeBudget bounds.
// Never consulted by the search's own control flow beyond the budget check itself.
struct LnsPolishStats {
    int nodes_expanded = 0;
};

// A1: connectivity-driven group selection. Movable = !isAnchoredEdge(problem, i); a block with zero
// total bundle nets (A7 "bundle-less blocks never selected") is never a seed or grow candidate.
// `round` (0-based) selects the seed as the round-th ranked movable-and-bundle-connected block by
// (has_open_route desc, wl_share desc, index asc) -- wl_share[b] = Sum over `routing`.routes with
// source/target==b of Route.wirelength (route_geometry.hpp's real routed-WL accounting, already
// nets * Sum|delta guiding point|). The group then grows by repeatedly adding the not-yet-member
// movable-and-bundle-connected block with the largest Sum of bundle nets connecting it to the
// CURRENT group (ties: that candidate's own wl_share desc, then index asc) until
// |G| == min(group_size, movable-and-bundle-connected count). Returns {} when fewer than 2 such
// blocks exist, or when `round` has no distinct ranked seed to offer. Deterministic: no RNG.
std::vector<int> lnsSelectGroup(const Problem& problem, const RoutingResult& routing, int round,
                                int group_size = kLnsGroupSize);

// A2: the RAW (uncapped) discrete-but-complete candidate (x,y) position grid for one group member,
// exposed standalone so its completeness (the design's "for any maximal empty rectangle R of the
// fixed geometry, (clamp(mx,R), clamp(my,R)) is in the set" theorem) is directly unit-testable.
// `member` must be one of `group`'s block indices (or is treated as such regardless -- always
// excluded from its own "fixed" obstacle set). FIXED rects = every `base.rects[j]` with `j` not in
// `group` and `j != member` -- i.e. every block NOT jointly being re-placed by this group's B&B,
// edge blocks included (A7: soft-block dims are frozen, so `member`'s own w/h -- `base.rects[member]`
// -- never changes; only x/y are searched). Xs/Ys are built from {0, W-w}/{0, H-h}, every fixed
// rect's facing edge, and llround(wireMedianTarget(...) - half own size), clipped into
// [0, W-w]/[0, H-h] and deduped within kEps, then Xs x Ys is filtered to positions inside the
// outline with no overlap() against any fixed rect -- PLUS the member's own CURRENT position,
// force-included regardless of that filter (A2's own "current position survives" rule, needed so a
// complete assignment at the current placement is always a valid, always-attainable B&B incumbent
// seed). Returns {} when `base.outline_w`/`outline_h` is unset (<=kEps) or `member`'s own w/h
// exceeds the outline on either axis (A7: no candidates -> the caller drops it from the group).
std::vector<std::pair<double, double>> lnsCandidatePositions(const Problem& problem, const Placement& base,
                                                              const std::vector<int>& group, int member);

// A3-A5: the pure B&B driver. `base` is NEVER mutated; every returned Placement carries `base`'s own
// outline_w/outline_h stamp UNCHANGED (this module never re-stamps the outline -- the caller's own
// shrink-wrap rung, mirroring maybeAdoptWlRedistribute's, does that) and every block's w/h UNCHANGED
// (soft dims frozen, A7) -- only x/y of `group`'s own members are ever touched, and only to one of
// that member's own A2 candidate positions. Group members with zero total bundle nets or whose own
// w/h exceeds the outline are silently dropped from the group first (A7); if fewer than 2 members
// survive, or `base.outline_w`/`outline_h` is unset, or `base` is already geometrically illegal
// (out-of-outline or pairwise-overlapping, mirroring wl_redistribute.cpp's own gate), returns {}.
// Otherwise runs the node-capped (`node_budget`, anytime -- exhausting it simply stops expanding
// further and returns whatever was already found, never wall-clock-gated) exact B&B described in A3:
// DFS members by their own total bundle nets descending (ties: index ascending), candidates within
// each member ascending by A2's own[i][c]+crossMin ranking, a candidate overlapping any
// already-decided group member is rejected, and the lexicographic (primary: bundle-weighted
// facing-edge gap; secondary: bundle-weighted center Manhattan, kEps-banded) lower bound prunes any
// subtree that cannot beat the running incumbent. The incumbent starts at the CURRENT assignment
// (every member at its own base position -- always a valid candidate per A2's force-include), so the
// search can only match or IMPROVE it under this proxy, never regress it (A3's own "never worse
// under proxy" invariant) -- returns {} when the search never finds anything strictly better.
// Otherwise returns up to the last 3 DISTINCT complete assignments that strictly improved the
// running incumbent, in the chronological order the DFS found them (earliest first = smallest
// relocation from the current placement, the same "early iterate = channel-destruction hedge" ladder
// discipline C1a's wlRedistributeIterates already established) -- iterates.back() is always the
// single BEST (final) incumbent found. Deterministic: no RNG anywhere in the search.
std::vector<Placement> lnsPolishIterates(const Problem& problem, const Placement& base,
                                         const std::vector<int>& group, LnsPolishStats* stats = nullptr,
                                         int node_budget = kLnsNodeBudget);

}  // namespace efgr
