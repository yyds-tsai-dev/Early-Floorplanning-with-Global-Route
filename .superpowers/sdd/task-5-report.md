# Task 5 Report: Feedthrough finalize write-back pass

## Status: DONE (GREEN, committed)

## Commit
`15ad8ec` on branch `fable5` — "feat: feedthrough finalize pass writes required FT area back into output dims"

Files changed (exactly the three the brief scoped):
- `src/search.hpp`
- `src/search.cpp`
- `test/regression_p0_20260706.cpp`

## TDD evidence

### RED
Added both tests (`testFeedthroughFinalizeWritesBackArea`, `testFinalizeFeedthroughDirect`) plus the four required includes (`<memory>`, `../src/search.hpp`, `../src/placement_engine.hpp`, `../src/validate.hpp`), called from `main()`. Compiling before implementing `finalizeFeedthrough` gave the expected error:

```
test/regression_p0_20260706.cpp: In function 'void {anonymous}::testFinalizeFeedthroughDirect()':
test/regression_p0_20260706.cpp:207:5: error: 'finalizeFeedthrough' was not declared in this scope
  207 |     finalizeFeedthrough(p, *engine, result);
      |     ^~~~~~~~~~~~~~~~~~~
```

### Implementation
- `src/search.hpp`: added `void finalizeFeedthrough(const Problem& problem, const PlacementEngine& engine, SearchResult& result);` after `runHybridPlacementSearch`.
- `src/search.cpp`: added `#include "ft.hpp"`; implemented `finalizeFeedthrough` verbatim per the brief (file scope, above `runPlacementSearch`); hooked `finalizeFeedthrough(problem, engine, result);` immediately before `return result;` in the 4-arg `runPlacementSearch` (was line 178, after the top-candidates loop).

### GREEN
```
$ bash test/run_p0_regressions.sh
testFtMathOfficialExamples ok
testRoutingMismatchUsesCeiledMath ok
testDirectAbutmentZeroWirelength ok
testOutlineConsistencyEvalVsCfg ok
testFeedthroughFinalizeWritesBackArea ok
testFinalizeFeedthroughDirect ok
p0 regressions passed
P0 regressions passed
```

## Fixture-geometry adjustment (called out per brief Step 4)

`testFinalizeFeedthroughDirect` as originally specified (outline_h = 340, exactly flush with the wall block's top edge) failed — not at the precondition assert, but at the final assert after calling `finalizeFeedthrough`. Root cause, confirmed via a standalone debug harness:

- The precondition passed as expected: `exactScore` on the un-inflated row placement reported `feedthrough_mismatch = 2416 > 0` (FT forced through BLK02, correctly under-provisioned).
- Inside `finalizeFeedthrough`'s loop: `required = ftRequiredArea(90000, ftExtend(900, 0.2)) = 92416`, correctly detected as growth, `inflated.blocks[1].area` set to 92416.
- `engine.seedFromPlacement(inflated, placement, rng)` + `engine.decode(...)` (bstar) does **not** preserve the original row layout — it sorts movable blocks by original `(x, y)` and rebuilds a fresh contour-packed B*-tree from scratch. In this fixture, that re-decode stacked the inflated BLK02 **on top of** the wall block (BLK04, occupying y=300..340) instead of widening it in place within the row, pushing BLK02's top to 644 — outside `outline_h = 340`.
- `exactScore` on that re-decoded placement then reported the correct `feedthrough_mismatch = 0` but `exact.value = 1e12` (out-of-outline penalty), which is **not** `< result.best_score.value` (2.4189e9), so the finalize loop's acceptance guard correctly rejected the candidate — leaving `result.best_routing.feedthrough_mismatch` at the original 2416, failing the test's final assert.

This is a property of `BStarEngine::seedFromPlacement`/`decode` (out of this task's scope to modify per the brief's Interfaces section — it's a consumed, existing interface), triggered specifically because the fixture had zero headroom above the wall (`outline_h` exactly equal to the wall's top edge).

**Fix applied (fixture only, no source change):** bumped `p.outline_h` from `340.0` to `700.0` in `testFinalizeFeedthroughDirect`. This gives the bstar re-decode's restack enough room to land legally in-outline, while the wall (still at y=300..340, unchanged) continues to block channel routing over the top, keeping FT forced as required (`soft_feedthrough_nets[1] = 900 > 0` still holds). Verified via debug harness before applying: with `outline_h = 700`, the re-decode produces `exact.value = 399280 < best.value = 2.4189e9`, mismatch = 0 → accepted. Confirmed in the actual test run above. No other fixture geometry (wall position/size, row layout) changed. Documented inline in the test file at the point of the `outline_h` assignment.

## Validator relaxation: NOT needed

The brief flagged a possible need to relax `validateSolution`'s dimension check for soft blocks (`>= input area` semantics instead of strict equality). Checked `src/validate.cpp:79-83`:

```cpp
if (!isSoft(problem.blocks[i].type)) {
    if (!near(r.w, problem.blocks[i].width) || !near(r.h, problem.blocks[i].height)) {
        fail("fixed block dimension violation: " + problem.blocks[i].name);
    }
}
```

This check already skips soft blocks entirely (`!isSoft(...)` guard), so inflated soft-block output dims pass `validateSolution` unmodified — no validator change was made or needed. `testFeedthroughFinalizeWritesBackArea`'s `assert(validation.ok)` passes as-is.

## Full verification (Step 4)

```
$ make clean && make
... (8 files compiled, -Wall -Wextra -pedantic, zero warnings) ...
g++ ... -o bin/efgr ...
cp bin/efgr bin/EarlyFloorplanning_with_GlobalRoute

$ bash test/run_case00.sh
engine=bstar iters=500 ... feedthrough_mismatch=0 open_routes=0 validation=ok
engine=bstar iters=25  ... feedthrough_mismatch=0 open_routes=0 validation=ok

$ bash test/run_placement_engine_regressions.sh
placement_engine_regression: ok

$ bash test/run_20260618_regressions.sh
regression_20260618: ok

$ bash test/run_20260608_regressions.sh
regression_20260608: ok
engine=bstar iters=50 ... feedthrough_mismatch=0 open_routes=0 validation=ok
```

All warning-clean, all passing.

## Concerns / notes for follow-up

1. **`BStarEngine::seedFromPlacement`/`decode` do not preserve prior layout on re-seed** — they only use the input placement's `(x,y)` to determine a *sort order*, then rebuild geometry entirely from a fresh contour-packed tree. This means `finalizeFeedthrough`'s re-seed-and-decode step can produce arbitrarily different geometry from the original placement (not just "grow one block in place"), which can cause legitimate inflate-and-fix attempts to be rejected as out-of-outline in tightly-packed solutions (zero slack near the used outline edges). The current guard (`exact.value < result.best_score.value` AND `mismatch <= kEps` AND `open_routes == 0`) is safe — it never accepts a worse or illegal candidate — but means finalize may sometimes fail to fix a mismatch it detects, silently leaving the pre-existing mismatch penalty in place (same as if finalize didn't run for that pass; loop tries up to 3 passes but geometry that doesn't fit won't be fixed by retrying with the same seed strategy). This is inherent to the specified design (brief explicitly puts `seedFromPlacement`/`decode` out of scope), not a bug introduced by this task, but worth flagging for a future task if tight-outline cases show residual `feedthrough_mismatch` in practice.
2. No other source files were touched; `main.cpp`'s existing 4-arg `runPlacementSearch` call sites (sp, bstar, hybrid) all get the finalize pass automatically since it's hooked inside that one function.
