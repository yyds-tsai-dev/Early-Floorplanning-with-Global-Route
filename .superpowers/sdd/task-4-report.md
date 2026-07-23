# Task 4 Report: One shared usedOutline for eval + writeCfg

## Status: DONE

## Commit
`10d7615` — `fix: single shared usedOutline for scoring and cfg output`
(branch `fable5`)

## Files changed
- `src/eval.hpp` — added two `usedOutline` declarations (2-arg and 3-arg overloads), before `cheapScore`/`exactScore`.
- `src/eval.cpp:7-33` — moved the existing 2-arg `usedOutline` out of the anonymous namespace (now a plain `efgr::` function); added the 3-arg overload with body moved verbatim from `src/io.cpp` (maxes over `placement.rects`, then over `routing.channels` rects, then clamps to `problem.outline_w/h` with a `max(1.0, ...)` floor). Confirmed both call sites inside `exactScore`/`cheapScore` (`eval.cpp:79`, `eval.cpp:102`) still use the 2-arg version unchanged.
- `src/io.cpp` — added `#include "eval.hpp"` after existing includes; deleted the local 3-arg `usedOutline` definition (former lines ~120-135) from the anonymous namespace. `writeCfg` (`io.cpp:~197`, `auto [used_w, used_h] = usedOutline(problem, placement, routing);`) now resolves to the shared `eval.cpp` definition via the new include.
- `test/regression_p0_20260706.cpp` — added `#include <cstdio>`, `#include <fstream>`, `#include "../src/eval.hpp"`, `#include "../src/io.hpp"`; added `testOutlineConsistencyEvalVsCfg()` (verbatim per brief) and wired it into `main()`.
- `test/run_20260608_regressions.sh` — added `src/eval.cpp` to that script's standalone g++ link line. Not listed in the brief's commit set, but required: this script links `test/regression_20260608.cpp src/io.cpp src/floorplan.cpp src/routing.cpp` directly (bypassing the Makefile), and once `io.cpp` calls the now-exported `efgr::usedOutline` defined in `eval.cpp`, the link failed with `undefined reference to efgr::usedOutline(...)` until `src/eval.cpp` was added as a link input. This is a mechanical fix to keep a pre-existing regression script consistent with the new dependency, not a design change.

## RED evidence (Step 2)
Before implementing, `bash test/run_p0_regressions.sh` failed at compile time as expected:
```
test/regression_p0_20260706.cpp: In function 'void {anonymous}::testOutlineConsistencyEvalVsCfg()':
test/regression_p0_20260706.cpp:133:19: error: 'usedOutline' was not declared in this scope
  133 |     auto [w, h] = usedOutline(p, pl, routing);
      |                   ^~~~~~~~~~~
```

## GREEN evidence (Step 4)
`bash test/run_p0_regressions.sh`:
```
testFtMathOfficialExamples ok
testRoutingMismatchUsesCeiledMath ok
testDirectAbutmentZeroWirelength ok
testOutlineConsistencyEvalVsCfg ok
p0 regressions passed
P0 regressions passed
```

Full clean sweep (`make clean && make`, all warning-clean under `-Wall -Wextra -pedantic`) followed by all five regression scripts, all passed:
- `test/run_case00.sh` — ok (7 blocks, OUTLINE/BLOCK/CHANNEL/PATH present)
- `test/run_placement_engine_regressions.sh` — `placement_engine_regression: ok`
- `test/run_20260618_regressions.sh` — `regression_20260618: ok`
- `test/run_20260608_regressions.sh` — `regression_20260608: ok` (after the link-line fix above), plus `bin/efgr` run: `validation=ok`, `open_routes=0`
- `test/run_p0_regressions.sh` — all 4 tests ok, `P0 regressions passed`

## Self-review / concerns
- The 3-arg body is byte-for-byte what was in `io.cpp`'s old local copy (max over rects, then over `routing.channels`, then clamp with `max(1.0, ...)` floor) — no semantic drift.
- Confirmed `exactScore` and `cheapScore` in `eval.cpp` both still call the 2-arg overload (pre-routing scope), matching the brief's note that post-routing the 3-arg result equals the 2-arg result once channels don't extend the bbox — the new test's `assert(near(exact.outline_area, w*h, 1e-6))` and the cfg-round-trip assert both hold, confirming this equality in practice for the `forcedFtProblem`/`rowPlacement` fixture (no channels needed — outline is exactly the row bbox per the fixture's comment).
- One scope decision I made without checking back: extending `test/run_20260608_regressions.sh`'s link line. This is the smallest possible fix (one filename added) and necessary for that pre-existing script to keep passing after `io.cpp` gained a real dependency on `eval.cpp`; flagging it explicitly here in case the orchestrator wants it split into its own commit or wants to reconsider whether that script should instead link against the Makefile's object files.
- No other call sites of the old anonymous-namespace `usedOutline` existed outside `eval.cpp`/`io.cpp` (verified via grep before editing).
