# P1 Task 3 Report — shrinkWrapOutline (exact, re-anchor + grow loop)

## Status: DONE

## Commit
`2a7c04b` — `feat: add exact shrink-wrap with edge re-anchoring`
(3 files changed, 201 insertions: `src/placement_engine.hpp`, `src/placement_engine.cpp`,
`test/regression_p1_20260706.cpp`)

## What was implemented
- `src/placement_engine.hpp`: declared
  `OutlineFit shrinkWrapOutline(const Problem&, Placement&, int max_grow_steps = 6);`
- `src/placement_engine.cpp`:
  - `placementLegalAt(problem, placement, w, h)` (new file-local helper, in the T2 anonymous
    namespace block): checks every rect is within `[0,w]x[0,h]`, no pairwise overlap among ALL
    rects, and every edge block satisfies `edgeRectLegal` at a scoped `(w,h)` Problem. This is the
    explicit carry-over gate from the T2 review ledger — `legalizeEdgeBlocks`'s corner/multi-side
    branch (`firstLegalEdgeSlot` → `placeEdge`) places blindly with no self-collision check, so a
    colliding corner block must be caught here, not assumed legal just because legalization ran.
  - `shrinkWrapOutline(...)`: implements design §1.2.4 verbatim — lower bound via the existing
    `lowerBoundNeeds`, then per guard step: build a scoped `Problem` at candidate `(W,H)`, copy
    `placement` into a trial with edge rects reset to `dimensionFor`, run `legalizeEdgeBlocks`,
    verify with `placementLegalAt`; on success copy only the edge rects back into `placement`,
    stamp `outline_w/h`, return `{W,H,true}`; on failure grow both axes `×1.05 + kEps` toward MAX
    (capped at `max_grow_steps`). Fallback re-legalizes at MAX and stamps MAX (today's layout),
    reporting `legal` from the same gate (always true in practice since MAX reproduces what
    `decode` already produced legally).
  - Core (non-edge) rects are never written back — only `trial.rects[i]` for edge blocks is copied
    into the caller's `placement`.

## Tests (test/regression_p1_20260706.cpp, all new)
- `testShrinkWrapOutlineAllCasesBStarSeed1` — case0..case4, bstar decode seed 1: `shrinkWrapOutline`
  legal, stamped `0 < W ≤ MAX_w`, `0 < H ≤ MAX_h`, no core rect moved (byte-compare), and
  `validateSolution` against a scoped Problem (outline = stamped W,H) passes.
- `testShrinkWrapOutlineCase0FuzzSeeds` — same invariants at seeds 2 and 3 on case0.
- `testShrinkWrapOutlineMidSideBlockLongerThanShrunkThird` — synthetic problem with a TM edge
  block wider than the shrunk middle third; asserts legal + validates (A19/A20 regression, design
  Q3), and that the untouched core block's coordinates are exactly preserved.

RED confirmed first (compile error: `shrinkWrapOutline` not declared), then implemented, then
GREEN.

## Verification (sequential, to avoid concurrent `make clean` races across scripts)
- `make` — warning-clean (`-Wall -Wextra -pedantic`).
- `bash test/run_case00.sh` — ok, `validation=ok`.
- `bash test/run_placement_engine_regressions.sh` — ok.
- `bash test/run_20260618_regressions.sh` — ok.
- `bash test/run_20260608_regressions.sh` — ok.
- `bash test/run_p0_regressions.sh` — ok (7/7 tests).
- `bash test/run_p1_regressions.sh` — ok (8/8 tests, 3 new for T3).

(Note: running all four legacy scripts in parallel in one message caused a transient failure in
`run_20260608_regressions.sh` — `bin/efgr: No such file or directory` — purely because each script
does `make clean && make` against the shared `bin/`/`src/*.o`, so concurrent scripts stomped on
each other's build artifacts. Re-running sequentially, every suite passed. Not a code defect.)

## Files not touched (per instructions)
`src/eval.cpp`, `src/search.cpp`, `src/search.hpp` — left exactly as found (unstaged, owned by a
concurrent session); nothing from them was staged or committed.

## Concerns
- None blocking. `shrinkWrapOutline`'s grow loop is intentionally simple (scales both axes
  together rather than "bump only the deficient axis" as the design's parenthetical prefers) —
  this matches the pseudocode's primary branch verbatim and is Pareto-safe (monotone toward the
  always-legal MAX fixpoint within `max_grow_steps`), but a future task could tighten it to bump
  only the failing axis for slightly better results. Left as-is since the brief said implement the
  §1.2.4 pseudocode verbatim, which scales both.
- `shrinkWrapOutline` is not yet wired into `decode()` or called anywhere in the search hot path —
  that is T4 (stamp in decode + cheapScore area gradient), explicitly out of scope here.

Report path: `.superpowers/sdd/p1-task-3-report.md`
