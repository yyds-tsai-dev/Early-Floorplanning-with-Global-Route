# P1 Task 1 Report — Placement outline fields + placementKey + p1 test harness

**Status:** DONE

**Commit:** `06a830b` — feat: add declared outline fields to Placement and placementKey

## Changes
- `src/model.hpp:59-63` — appended `double outline_w = 0.0;` / `double outline_h = 0.0;` to `struct Placement`, after `rects`/`aspect`.
- `src/search.cpp:60-70` — `placementKey` now appends `':' << llround(outline_w*1000) << 'x' << llround(outline_h*1000)`.
- `test/regression_p1_20260706.cpp` (new) — `testPlacementKeyDiffersWithOutline` (same rects, one placement with outline_w/h set vs unset ⇒ keys differ) and `testPlacementKeyUnsetMatchesTodaysFormat` (unset fields default to 0.0, key format asserted against explicit expected string).
- `test/run_p1_regressions.sh` (new) — mirrors `run_p0_regressions.sh`'s g++ src list exactly, outputs `test/p1_regression_bin`.
- `.gitignore` — added `test/p1_regression_bin`.

## Test summary
- RED confirmed first: compile failed with "has no member named 'outline_w'/'outline_h'" before implementation.
- GREEN after implementation, all six suites pass:
  - `bash test/run_p1_regressions.sh` → both new tests ok, "P1 regressions passed"
  - `bash test/run_p0_regressions.sh` → all 6 existing tests ok, "P0 regressions passed"
  - `bash test/run_case00.sh` → validation=ok
  - `bash test/run_placement_engine_regressions.sh` → ok
  - `bash test/run_20260618_regressions.sh` → ok
  - `bash test/run_20260608_regressions.sh` → ok
- `make clean && make` rebuilt warning-clean under `-Wall -Wextra -pedantic`.

## Concerns
- `src/eval.cpp` has an unstaged, uncommitted change in the working tree (`cheapScore`'s `score.value` composition now uses `score.penalty` instead of `penalty`) from a concurrent session, exactly as flagged in the plan's Global Constraints. Left untouched and unstaged — not part of this commit.
- Numerous other untracked `.superpowers/sdd/*.md` and `*.diff` files exist from prior/parallel task work; none touched or added here.

**Report path:** `.superpowers/sdd/p1-task-1-report.md`
