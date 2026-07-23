# P3 Task 3 Report — respaceToDemand single pass (integrate + legality gate)

**Commit:** `34ec8cf` feat: demand-driven respace single pass

## What changed

- `src/respace.hpp`: declared `bool respaceToDemand(const Problem&, Placement&, const RoutingResult&);`
- `src/respace.cpp`:
  - added `#include "placement_engine.hpp"` for `OutlineFit shrinkWrapOutline(...)`.
  - implemented `respaceToDemand`: `channelDemands` -> if empty return false -> work on a local
    `Placement candidate` copy -> `relaxToDemand` -> reset `candidate.outline_w/h = 0` -> fresh
    `shrinkWrapOutline(problem, candidate)` (re-anchors edges + stamps outline for the widened
    core) -> gate on `fit.legal`, an explicit local `rectsLegalWithinOutline` pairwise-overlap +
    containment check (mirrors `placementLegalAt`'s geometric semantics; that function is
    file-local to `placement_engine.cpp` so this is reproduced locally rather than widening that
    file's public surface, per the brief's "your choice, note it"), and a `<= problem.outline_w/h`
    MAX check. On success, `placement = candidate`, return true; on any failure, `placement`
    untouched, return false.
  - added a small file-local helper `rectsLegalWithinOutline`.
- `test/regression_p3_20260707.cpp`: added two T3 tests —
  - `testRespaceToDemandCase1BStarSeed1`: case1 bstar seed1 decode+exactScore, asserts
    `channel_overflow > 0` (confirms the overflow-prone fixture), then `respaceToDemand` returns
    true, stamped area is non-decreasing, stamped dims `<= MAX`, a fresh route + `validateSolution`
    against the new stamp passes, every core rect moved only right/up, and pairwise x/y relative
    order among core blocks is preserved (spot-checked, capped at 20 pairs).
  - `testRespaceToDemandNoDemandIsNoOp`: hand-built 2-block fixture with no bundles (zero
    `channel_overflow`, zero directional usage) -> `respaceToDemand` returns false and the
    placement is byte-identical to the input.
  - `main()` updated to call both.

## Tests

- `bash test/run_p3_regressions.sh` — all 14 tests pass (12 pre-existing T1/T2 + 2 new T3),
  "P3 regressions passed".
- `make` — warning-clean (`-Wall -Wextra -pedantic`).
- `bash test/run_p1_regressions.sh` — pass, "P1 regressions passed".
- `bash test/run_p0_regressions.sh` — pass, "P0 regressions passed".
- `bash test/run_case00.sh` — pass (validation=ok, open_routes=0 both runs).
- `bash test/run_placement_engine_regressions.sh` — pass ("placement_engine_regression: ok").
- `bash test/run_20260618_regressions.sh` — pass ("regression_20260618: ok").
- `bash test/run_20260608_regressions.sh` — pass ("regression_20260608: ok").

## Concerns / notes

- Edge-location legality (flush/thirds/corner-AND rules) is not re-verified by
  `rectsLegalWithinOutline` directly — it is already gated inside `shrinkWrapOutline`'s own
  `commitOutlineAt`/`placementLegalAt` call before `fit.legal` is set, so re-deriving it in
  `respace.cpp` would be redundant. `respaceToDemand`'s own explicit check is containment +
  pairwise overlap only, layered on top of `fit.legal` as a defense-in-depth check at the public
  entry point (matches the design brief's "explicit pairwise overlap check ... mirror
  placementLegalAt's semantics").
- `respaceToDemand` does not itself re-route; the caller (future T4/T5/T6 hooks) is expected to
  re-route and re-score the accepted result. The T3 test does this explicitly to validate the
  output.
- No production code outside `src/respace.*` was touched; `search.cpp` wiring is out of scope
  (Tasks 5/6).

Report path: `.superpowers/sdd/p3-task-3-report.md`
