# P1 Task 6 Report: validation + writeCfg use the declared outline

## Status: DONE (GREEN, committed)

## Commit
`853bd51` — `fix: validate and emit against the declared outline`
(3 files changed, 120 insertions(+), 4 deletions(-))

## Files changed
- `src/validate.cpp` — `validateSolution` now builds a scoped `Problem` copy from
  `placement.outline_w/h` (used when > 0, else falls back to `problem.outline_w/h` = MAX) and
  checks containment + `satisfiesEdgeLocations` (flush/thirds/corner-AND) against that scoped
  outline instead of the raw `problem` outline.
- `src/io.cpp` — `writeCfg` now emits `placement.outline_w/h` directly when stamped (> 0),
  falling back to the existing `usedOutline(problem, placement, routing)` 3-arg path when unset.
- `test/regression_p1_20260706.cpp` — added
  `testValidateAndWriteCfgUseDeclaredOutlineOnShrunkCase0` (RED before the fix: false
  "edge location violation: BLK01" on a genuinely shrunk case0/bstar/seed1 placement; asserts
  `validateSolution(problem, ...)` called with the *original* MAX problem — exactly as
  `main.cpp` does — now returns ok, and the written `.cfg`'s `OUTLINE` tokens match the stamped
  declared W,H to 2-decimal tolerance) and `testOutlineConsistencyEvalVsCfgShrunkCase` (extends
  the P0 outline-consistency invariant to the shrunk case: `cheapScore(scoped, placement)
  .outline_area == placement.outline_w * placement.outline_h == emitted OUTLINE w,h`; exact-score
  outline handling is out of scope, T5, so it hand-builds the same scoped-routing +
  `cheapScore` combination).

## Note on tolerance
Per-dimension comparison (`|cfg_w - placement.outline_w| < 1e-2`, same for h), not an area-product
comparison at `1e-2` — case0's magnitude (~2916 x ~2185) means `setprecision(3)` truncation alone
produces an area delta of ~0.7 (`w·Δh + h·Δw`), unrelated to any real inconsistency. Kept the
per-dimension check the brief actually asked for ("parsed OUTLINE tokens == stamped declared W,H,
2-decimal tolerance").

## Tests / build
- `make` — warning-clean (`-Wall -Wextra -pedantic`).
- `bash test/run_p1_regressions.sh` — all 10 tests pass, including the two new T6 tests (RED
  confirmed first: `Assertion 'vr.ok' failed` with `validate: edge location violation: BLK01`).
- `bash test/run_p0_regressions.sh` — pass (8/8, unaffected).
- `bash test/run_case00.sh`, `run_placement_engine_regressions.sh`, `run_20260618_regressions.sh`,
  `run_20260608_regressions.sh` — all pass.

## Concurrent-session guard
`src/eval.cpp`, `src/search.cpp`, `src/search.hpp` (and `test/regression_p0_20260706.cpp`,
`test/run_placement_engine_regressions.sh`) remained unstaged/untouched throughout — confirmed via
`git status --porcelain` before and after the commit, and `git diff --stat` on those paths showed
no change from my session. Only `src/validate.cpp`, `src/io.cpp`,
`test/regression_p1_20260706.cpp` were staged and committed.

## Concerns
None. Shrink is Pareto-safe by construction (T3): the scoped outline is always ≤ MAX, so
containment/thirds checks against it are strictly not looser than a MAX check would be for a
correctly-placed rect, and the fallback path (unset outline) reproduces byte-identical prior
behavior. No architectural decisions were made; this was a direct application of design §1.6.
