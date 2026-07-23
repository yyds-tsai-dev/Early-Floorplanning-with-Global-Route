# P1 Task 2 Report — shrinkWrapEstimate

## Status: DONE

## Commit
`b269688` — `feat: add cheap shrink-wrap outline estimate`
Files: `src/placement_engine.hpp`, `src/placement_engine.cpp`, `test/regression_p1_20260706.cpp`
`src/eval.cpp` left untouched/unstaged (concurrent session's change preserved).

## What was built
- `struct OutlineFit { double w; double h; bool legal; }` and
  `OutlineFit shrinkWrapEstimate(const Problem&, const Placement&)` declared in
  `placement_engine.hpp`, implemented in `placement_engine.cpp` (below the existing
  file-local statics it reuses: `dimensionFor`, `hasMultipleBoundarySides`,
  `firstLegalEdgeSlot`, `edgeRectLegal`, `collidesWithPlaced`).
- Per-axis lower bounds (`lowerBoundNeeds`): `W_core`/`H_core` from non-edge rect extents,
  `W_edge`/`H_edge` from edge block widths/heights (corner blocks force both).
- Feasibility probe (`edgeBlocksFeasibleAt`): builds a scoped `Problem` copy at candidate
  `(W,H)`, mirrors `legalizeEdgeBlocks`' sort order, and calls the existing
  `firstLegalEdgeSlot`/`edgeRectLegal` per edge block on a local trial `rects`/`placed`
  copy — never mutates the caller's `Placement`. Grows the candidate `(W,H)` by `×1.05 + kEps`
  up to 6 times, falls back to problem MAX (legal by construction, since that reproduces
  `decode`'s original legalization).
- **Bug found and fixed during TDD**: `firstLegalEdgeSlot`'s multi-side (corner) branch
  defers straight to `placeEdge`, which is a pure function of outline dims + block dims +
  locations with **no** collision awareness — unlike the single-side branch, it never calls
  `collidesWithPlaced`. This is a pre-existing gap in `placement_engine.cpp` (confirmed via a
  `deep-reasoner` peer check), not something introduced here. My synthetic two-corner test
  caught it: two opposite-corner blocks whose true footprints overlap were being accepted as
  "feasible" at a too-small `(W,H)`. Fixed by adding one line —
  `if (collidesWithPlaced(rects, placed, idx, slot)) return false;` — inside my own new
  feasibility probe (reuses the existing helper; no new geometry predicate). This restores the
  design's §3.1 "no shrink state emits overlap" invariant for the estimate. Left the underlying
  `firstLegalEdgeSlot`/`legalizeEdgeBlocks` corner short-circuit itself unchanged (out of this
  task's scope; worth carrying into T3's exact `shrinkWrapOutline`, which shares the same
  blind spot, per the design doc's §3.2 cache-consistency concern).

## Tests (test/regression_p1_20260706.cpp)
1. `testShrinkWrapEstimateCase0BStarSeed1` — parses `test/2026 ICCAD_case.case0.csv`,
   decodes with `bstar` seed 1: estimate area `< MAX area` and `>= core bbox area`, both axes
   `<= MAX`.
2. `testShrinkWrapEstimateCornerBlocksForceMax` — synthetic `TL,LT` (500x500) +
   `RB,BR` (500x300) corner blocks on a 1000x800 outline, sized so their footprints overlap at
   any `(W,H)` short of MAX but are exactly disjoint at MAX ⇒ estimate == MAX dims.
3. `testShrinkWrapEstimateNoEdgeBlocksEqualsCoreBbox` — two MACRO blocks, no edge blocks ⇒
   estimate == core bbox exactly.

RED confirmed first (link error: `undefined reference to shrinkWrapEstimate`), then GREEN
after implementation + the collision-check fix.

## Build / test results (all green)
- `make`: warning-clean (`-Wall -Wextra -pedantic`).
- `test/run_p1_regressions.sh`: PASS (5/5 including the 3 new tests).
- `test/run_p0_regressions.sh`: PASS (6/6).
- `test/run_case00.sh`: PASS, `validation=ok`, `open_routes=0`, exit 0.
- `test/run_placement_engine_regressions.sh`: PASS.
- `test/run_20260618_regressions.sh`: PASS.
- `test/run_20260608_regressions.sh`: PASS.

## Concerns / notes for later tasks
- The corner-block collision gap in `firstLegalEdgeSlot`/`legalizeEdgeBlocks` is pre-existing
  and unrelated to this task, but it means **today's production code can already emit real
  overlap** for two opposite corner-locked edge blocks whose footprints intersect even at MAX.
  T3 (`shrinkWrapOutline`, exact) reuses the same `legalizeEdgeBlocks` and will inherit this gap
  unless it applies the same `collidesWithPlaced` gate on corner slots (or the underlying
  `legalizeEdgeBlocks` is fixed directly) — flagging per the design's own §3.2 requirement that
  cheap and exact stay consistent.
- No design ambiguity encountered that blocked implementation; the one judgement call (how to
  treat the corner-block collision gap) was resolved via `deep-reasoner` peer confirmation
  before applying the fix, and stayed within "reuse existing predicates, no new geometry
  primitive."

Report path: `.superpowers/sdd/p1-task-2-report.md`
