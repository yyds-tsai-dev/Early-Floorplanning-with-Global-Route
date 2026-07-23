# P1 Task 7.1 Report — Sweep/corrective-grow anchor fix

## Status
Done. All 6 required test suites green, build warning-clean, 4 smokes pass, case0 hybrid deterministic.

## Change summary
- `src/placement_engine.hpp`: declared `OutlineFit anchorAtOutline(const Problem& scoped, Placement& placement);`
- `src/placement_engine.cpp`: factored shared commit logic (reset edge rects via `dimensionFor`, `legalizeEdgeBlocks`, `placementLegalAt` gate, stamp) into a new `commitOutlineAt` helper placed after `placementLegalAt`'s definition; `shrinkWrapOutline`'s own bisection/search is untouched, its commit tail now just calls `commitOutlineAt`. `anchorAtOutline` calls `commitOutlineAt` directly at the caller's exact `(scoped.outline_w, scoped.outline_h)` — no bisection — and only writes back to the caller's `placement` if legal.
- `src/search.cpp` `outlineSweep`: for `margin == 0.0` still calls `shrinkWrapOutline` (the tight rung); for `margin > 0.0` calls `anchorAtOutline(ceiling, variant)` and skips the rung (`continue`) if `legal == false`.
- `src/eval.cpp` corrective grow: replaced `shrinkWrapOutline(ceiling, regrow)` with `anchorAtOutline(ceiling, regrow)`, same legality gate/preference logic kept.
- `test/regression_p1_20260706.cpp`: added `testAnchorAtOutlineStampsExactTargetNotTightCase0` (RED before fix — `anchorAtOutline` undeclared; verifies stamped outline == 10%-toward-MAX target, not tight, core rects unmoved, all rects contained, R/T edge blocks flush at target border) and `testOutlineSweepProbesDistinctOutlinesCase0` (probe `ExactCachedFn` recording each rung's stamped area; asserts ≥2 distinct areas probed).

## Tests
- `test/run_p1_regressions.sh`: all 19 cases pass, including the 2 new ones.
- `test/run_case00.sh`: pass (validation=ok).
- `test/run_placement_engine_regressions.sh`: pass.
- `test/run_20260618_regressions.sh`: pass.
- `test/run_20260608_regressions.sh`: pass.
- `test/run_p0_regressions.sh`: pass.
- `make`: clean, no warnings (`-Wall -Wextra -pedantic`).

## Smokes (iters 2000, seed 1)
```
engine=bstar iters=2000 exact_evals=530 blocks=7 bundles=11 channels=8 routes=13 score=9.20647e+06 outline_area=6.46288e+06 wirelength=7.30996e+06 channel_overflow=0 feedthrough_mismatch=0 open_routes=0 validation=ok
engine=hybrid iters=2000 exact_evals=1382 blocks=7 bundles=11 channels=11 routes=13 score=1.80179e+07 outline_area=5.54531e+06 wirelength=1.09677e+07 channel_overflow=0 feedthrough_mismatch=0 open_routes=0 validation=ok
engine=bstar iters=2000 exact_evals=1210 blocks=14 bundles=82 channels=28 routes=118 score=3.93908e+10 outline_area=7.55343e+06 wirelength=3.99828e+07 channel_overflow=38875.1 feedthrough_mismatch=0 open_routes=0 validation=ok
engine=hybrid iters=2000 exact_evals=1435 blocks=14 bundles=82 channels=26 routes=118 score=6.0819e+10 outline_area=7.06371e+06 wirelength=5.45152e+07 channel_overflow=59986.7 feedthrough_mismatch=0 open_routes=0 validation=ok
```
Determinism: case0 hybrid run 1 vs run 2 `.cfg` byte-identical (`diff` empty).

## Concerns
- case1 still shows nonzero `channel_overflow` on both engines (open_routes=0 throughout, so still legal/valid) — the sweep is now real but this repro didn't land on a lower-overflow rung; not a regression vs. pre-fix (pre-fix numbers weren't compared here, but legality is preserved and the mechanism now does real work, confirmed by the new distinct-outlines test).
- `commitOutlineAt` intentionally preserves `shrinkWrapOutline`'s pre-existing "always stamp `placement`, report legality separately" quirk; `anchorAtOutline` builds on a local `trial` copy and only writes back on success, per the brief's "placement untouched on legal=false" contract.

Report path: `\\wsl.localhost\ubuntu-24.04\home\yyds\Early-Floorplanning-with-Global-Route\.superpowers\sdd\p1-task-71-report.md`
