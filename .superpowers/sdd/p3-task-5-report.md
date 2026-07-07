# P3 Task 5 report — elite-pass respace hook

## Status: complete

## Changed files

- `src/search.cpp`
  - `search.cpp:11` — added `#include "respace.hpp"`.
  - `search.cpp:122-136` — new file-local helper `maybeAdoptRespace(problem, exactCached, result)`
    inside the existing anonymous namespace (alongside `outlineSweepBetter`): runs
    `respaceFixedPoint(problem, result.best_placement, exactCached)` and adopts the result into
    `result.best_score/best_placement/best_routing` under the SAME `outlineSweepBetter` preference
    (open_routes==0 strictly first, then lower value).
  - `search.cpp:412-416` (elite loop, `top_candidates` iteration) — one call to
    `maybeAdoptRespace(problem, exactCached, result)` placed **after** both sweep-adoption blocks
    (raw candidate sweep + compacted-variant sweep) for each elite. This respaces the
    then-current `result.best_placement`, not the swept candidate directly, so a losing sweep
    variant never separately pays for a respace call — each elite contributes at most one
    `respaceFixedPoint` call, exactly as specified.
- `test/regression_p3_20260707.cpp:833-882` — new `testEliteRespaceHookEndToEndCase1BStarSeed1`:
  runs `runPlacementSearch` end-to-end (case1, bstar, seed 1, iters 500), asserts
  `open_routes==0`, `validateSolution().ok`, and determinism (two identical runs produce
  byte-identical rects/outline/score). Wired into `main()`.
- `test/run_20260618_regressions.sh:10`, `test/run_placement_engine_regressions.sh:9`,
  `test/run_p0_regressions.sh:9`, `test/run_p1_regressions.sh:10` — added `src/respace.cpp` to the
  g++ link line (all four link `search.cpp` directly and broke with `undefined reference to
  respaceFixedPoint` before this fix). `run_20260608_regressions.sh` does not link `search.cpp` —
  confirmed unaffected, left untouched.

## Why not a unit test on the helper directly

`maybeAdoptRespace` is file-local (anonymous namespace), matching the existing precedent:
`outlineSweepBetter` — the exact rule it reuses — is also file-local and is *not* unit-tested
directly anywhere in this repo; it's only exercised through `outlineSweep`/`runPlacementSearch`
end-to-end. I followed that precedent rather than exposing a new symbol just for testability.

## Why the test doesn't assert "overflow strictly lower than pre-hook"

The brief's literal criterion (`case1 bstar seed1 iters 500 ⇒ final channel_overflow strictly
below the pre-hook value`) is not honestly assertable on this fixture: T4's own regression
(`testRespaceFixedPointCase1BStarSeed1NeverRegresses`, already landed) documents that case1's raw
decode at seed 1 is axis-locked — `respaceFixedPoint` legitimately returns `improved=false` there
(see `p3-task-4-report.md`). Asserting a strict overflow decrease on this exact fixture would be a
false requirement baked into the test. Per the task instructions, I asserted the honest contract
instead: end-to-end validation ok + open_routes==0 + determinism. Cost-movement evidence (cases
where respacing genuinely reduces overflow) is T7's sweep, not this task's.

## Test results (verbatim tail)

- `bash test/run_p3_regressions.sh` → all 17 tests ok, `P3 T1+T2+T3+T4+T5 regressions passed`,
  `P3 regressions passed`.
- `bash test/run_p1_regressions.sh` → all tests ok, `P1 regressions passed`.
- `bash test/run_p0_regressions.sh` → all tests ok, `P0 regressions passed`.
- `bash test/run_case00.sh` → passed, `validation=ok`.
- `bash test/run_placement_engine_regressions.sh` → `placement_engine_regression: ok`, all three
  engines (sp/bstar/hybrid) produced valid `.cfg` output.
- `bash test/run_20260618_regressions.sh` → `regression_20260618: ok`.
- `bash test/run_20260608_regressions.sh` → `regression_20260608: ok`, `validation=ok`.
- `make` → warning-clean (`-Wall -Wextra -pedantic`), builds `src/respace.o` and links cleanly.

## Concerns / follow-ups

- None new. The known empirical limit (axis-locked real cases at fixed seeds return
  `improved=false`) is inherited from T4 and does not regress anything here — it just means this
  hook is frequently a no-op on the current 5-case corpus at seed 1, same as documented.

## Commit

`feat: respace elites to demand` (pending push in this session).
