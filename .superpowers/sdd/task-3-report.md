# Task 3 Report: Direct abutment = zero wirelength

## Status: DONE

## Commit
`4a9cf72` — "fix: direct block abutment routes have zero wirelength per cost spec"
(branch `fable5`)

## Files changed
- `src/routing.cpp` — deleted line `if (guides.size() == 1) route.wirelength += nets;`
  (was at line 180, immediately after the HPWL-sum loop over consecutive guide pairs,
  inside `routeChunk` in `routeProblem`).
- `test/regression_p0_20260706.cpp` — added `testDirectAbutmentZeroWirelength()`
  (verbatim from the brief) and a call to it from `main()`.

## TDD evidence

**RED** (before deleting the line, running `bash test/run_p0_regressions.sh`):
```
testFtMathOfficialExamples ok
p0_regression_bin: test/regression_p0_20260706.cpp:120: void {anonymous}::testDirectAbutmentZeroWirelength(): Assertion `near(r.routes[0].wirelength, 0.0)' failed.
```
(Aborted, exit 134 — core dump from failed assert.) Confirms the current code was
adding `nets` (100) to wirelength for a direct 2-node route where the guide list has
exactly one point, contradicting the spec that the HPWL sum over consecutive
guiding-point *pairs* is empty when there's only one guide.

**GREEN** (after deleting the line):
```
testFtMathOfficialExamples ok
testRoutingMismatchUsesCeiledMath ok
testDirectAbutmentZeroWirelength ok
p0 regressions passed
P0 regressions passed
```

## Full verification run
- `bash test/run_p0_regressions.sh` — pass (output above).
- `bash test/run_case00.sh` — pass, `validation=ok`, `open_routes=0` (two engine runs, both ok).
- `bash test/run_placement_engine_regressions.sh` — `placement_engine_regression: ok`.
- `bash test/run_20260618_regressions.sh` — `regression_20260618: ok`.
- `bash test/run_20260608_regressions.sh` — `regression_20260608: ok`, `validation=ok`, `open_routes=0`.
- `make clean && make` — full rebuild, zero warnings under `-Wall -Wextra -pedantic`.

No existing regression asserted the old `+= nets` single-guide behavior, so no other
test files required updates.

## Self-review / concerns
- The diff is a single-line deletion plus a new, isolated unit test — minimal and
  scoped exactly to the brief. `git diff --stat`: `src/routing.cpp | 1 -`,
  `test/regression_p0_20260706.cpp | 39 +++++++++++++++++++++++++++++++++++++++`.
- Behavioral impact: any route whose path is a direct block-to-block (or
  block-to-channel) touch with exactly one guiding point now contributes 0 to
  `route.wirelength` and `result.total_wirelength`, instead of `+nets`. This lowers
  `exactScore`'s wirelength term for such routes across the solver (search.cpp/eval.cpp
  consumers were not touched — they just consume the corrected `RoutingResult`).
  This is the intended fidelity fix per the P0 scoring-fidelity plan (Task 3) and
  matches the contest's HPWL-over-guiding-points cost definition.
- No other call sites reference `guides.size() == 1` or add a flat `nets` fallback,
  confirmed via `grep -rn` over `src/` and `test/`.
- `.superpowers/` shows as untracked in `git status` but was not touched or committed
  by this task (pre-existing planning artifacts from prior tasks in this same
  directory tree).

## Report path
`\\wsl.localhost\ubuntu-24.04\home\yyds\Early-Floorplanning-with-Global-Route\.superpowers\sdd\task-3-report.md`
