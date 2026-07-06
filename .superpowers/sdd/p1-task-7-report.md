# P1 Task 7 Report — Elite-pass outline sweep

## Status: DONE

## Files changed
- `src/search.hpp` — added `ExactCachedFn` (std::function callback alias), `OutlineSweepResult`,
  declared `OutlineSweepResult outlineSweep(const Problem&, const Placement&, const ExactCachedFn&)`.
- `src/search.cpp` — implemented `outlineSweep` (deterministic margin ladder
  `{0.0, 0.02, 0.05, 0.10, 0.20}` between the tight shrink-wrap and MAX, using the same
  scoped-ceiling trick as `exactScore`'s corrective grow; duplicate rungs skipped) and a file-local
  `outlineSweepBetter` preference (open_routes==0 strictly first, then lower value). Wired into the
  elite loop in `runPlacementSearch`: both the top-16 cheap candidates and their
  `compactTowardOrigin` variants now go through `outlineSweep(problem, ·, exactCached)` instead of
  a single tight-stamp `exactCached` call; `result.best_*` updated via `outlineSweepBetter` instead
  of a bare `value <` comparison.
- `test/regression_p1_20260706.cpp` — added `testOutlineSweepPrefersLowerScoreOverTighterArea`
  (mocked-scoring RED test isolating `outlineSweep`'s own selection logic: real geometry via two
  opposite-corner edge blocks, mocked `exactCached` charges a large penalty only to the tight
  (margin-0) rung, forcing the sweep to pick a grown rung), `testOutlineSweepEndToEndCase1BStarSeed1OpenRoutesZero`
  (case1, bstar, seed 1, 300 iters: `open_routes==0`, `validateSolution` ok, and
  `best_score.value <= tight-stamp-only exactScore + kEps`, the sweep's Pareto guarantee), and
  `testCompactedCandidateRestampValidatesEndToEnd` (task's small permanent-regression follow-up:
  case0 decode → `compactTowardOrigin` → `shrinkWrapOutline` restamp → real `routeProblem` →
  `validateSolution` ok).

## Why the geometry-only test didn't work as the primary RED
Empirically probed (throwaway program) that `shrinkWrapOutline` always converges toward the
*true minimal legal frontier* regardless of the ceiling passed in (it searches strictly downward),
so margin variants built from realistic legality constraints mostly collapse to the same tight
(W,H) — the only variation is small bisection-tolerance noise (tolerance is ceiling-relative).
That's real but too fragile/non-monotonic to assert "margin X strictly wins" against real routing.
Per the task's own escape hatch, the primary RED instead hand-builds real distinct-outline geometry
(two opposite-corner edge blocks) but mocks the exact-scoring callback to charge an overflow-style
penalty only at the tight rung — isolating `outlineSweep`'s selection/preference logic, which is
the actual unit under test.

## Tests
- `bash test/run_p1_regressions.sh` — 18/18 assertions pass (~9s), including the 3 new ones.
- `bash test/run_p0_regressions.sh` — pass (8/8).
- `bash test/run_case00.sh` — pass (smoke + one extra iters=25 run, both `validation=ok`).
- `bash test/run_placement_engine_regressions.sh` — pass.
- `bash test/run_20260618_regressions.sh` — pass.
- `bash test/run_20260608_regressions.sh` — pass.
- `make` — warning-clean (`-Wall -Wextra -pedantic`).
- Determinism: case0 hybrid seed 1 run twice → byte-identical `.cfg` (confirmed via `diff`).

## Smoke lines (iters=2000, seed=1)
```
engine=bstar  case0 iters=2000 exact_evals=466  blocks=7  bundles=11 channels=8  routes=13  score=9.20647e+06 outline_area=6.46288e+06 wirelength=7.30996e+06 channel_overflow=0     feedthrough_mismatch=0 open_routes=0 validation=ok
engine=hybrid case0 iters=2000 exact_evals=1439 blocks=7  bundles=11 channels=8  routes=13  score=1.26215e+07 outline_area=5.53447e+06 wirelength=4.77539e+06 channel_overflow=0     feedthrough_mismatch=0 open_routes=0 validation=ok
engine=bstar  case1 iters=2000 exact_evals=1082 blocks=14 bundles=82 channels=28 routes=118 score=3.93908e+10 outline_area=7.55343e+06 wirelength=3.99828e+07 channel_overflow=38875.1 feedthrough_mismatch=0 open_routes=0 validation=ok
engine=hybrid case1 iters=2000 exact_evals=1191 blocks=14 bundles=82 channels=26 routes=118 score=6.0819e+10  outline_area=7.06371e+06 wirelength=5.45152e+07 channel_overflow=59986.7 feedthrough_mismatch=0 open_routes=0 validation=ok
```
All four: `open_routes=0`, `feedthrough_mismatch=0`, `validation=ok`.

## Concerns
- case1's `channel_overflow` is still large (expected per design §6 R1/R2 — full resolution is
  gated on P2/P3 demand-driven routing/spacing landing; the sweep's job here is only to not make
  overflow worse while chasing area, and legality (`open_routes==0`) is preserved absolutely).
- The elite loop's preference rule changed from a bare `value <` comparison to
  `outlineSweepBetter` (open_routes==0 strictly first). This is a deliberate, task-specified
  improvement, not a silent behavior drift, but it does mean a hypothetical pre-T7 run that had
  accepted an open-route elite candidate for a lower raw value would no longer do so — strictly a
  legality fix, never a regression.

## Commit
`feat: elite outline sweep finds min-cost declared outline` (pending — see below).
