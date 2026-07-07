# P3 Task 6 report — finalizeFeedthrough respace hook

## Status: complete

## Changed files

- `src/search.cpp` — inside `finalizeFeedthrough`'s per-pass loop (`search.cpp:227-` region):
  - After the inflated re-decode's `exactScore` call (raw `placement`), added a plain
    `ExactCachedFn inflatedExact` lambda that scores against `inflated` (not the original
    `problem`), then `RespaceResult respaced = respaceFixedPoint(inflated, placement,
    inflatedExact)`, and `tryCommit(respaced.placement, respaced.score, respaced.routing)` —
    called right after the existing `tryCommit(placement, exact, new_routing)`, so the pre-T6
    commit path is untouched and the respaced candidate gets an *additional* shot at the same
    gate.
  - Same pattern applied to the compacted-variant branch: after `compacted`'s own `tryCommit`,
    added `respaceFixedPoint(inflated, compacted, inflatedExact)` + a second `tryCommit` for that
    result.
  - `tryCommit` itself is **unchanged** — still gates on `acceptFeedthroughPaydown` against
    `result.best_routing`/`result.best_score` and `validateSolution(problem, ...)` against the
    **original** `problem`, exactly as the brief specified ("respace only supplies a possibly-
    better candidate to the same gate").
- `test/regression_p3_20260707.cpp:883-970` — new `congestedFtProblem()`/`congestedFtPlacement()`
  fixture + `testFinalizeFeedthroughRespaceCongestedVariant`, wired into `main()`.

## Fixture construction (why it looks like this)

The brief pointed at the FT-forced fixture family, which actually lives in
`test/regression_p0_20260706.cpp` (`forcedFtProblem`/`rowPlacement`/`testFinalizeFeedthroughDirect`
— the P1 test file has no such family; the p0 file is the real "FT-forced" origin). I kept its
row layout (BLK01 macro | BLK02 soft | BLK03 macro) and its top-wall trick (BLK04 blocks channel
routing over the row, forcing BLK01<->BLK03 through the soft block, `feedthrough_mismatch>0`), and
added a **congestion** block BLK05 to the right of BLK03 with a real x-gap (so `extractChannels`
carves a genuine channel there) and a short shared y-band, so a heavy BLK03->BLK05 bundle (3200
nets) produces a real, empirically-verified `channel_overflow>0` (vcap=25×40=1000 on that channel;
usage 3200 overflows by 2200) *simultaneously* with the FT precondition. I verified this
empirically with a scratch harness before writing the real test (both preconditions hold:
`feedthrough_mismatch=2416`, `channel_overflow=2200`, `open_routes=0`).

Earlier attempts (placing the congestion target above the wall) failed because `routeProblem`'s
`allowed()` predicate (`routing.cpp:108-112`) never lets a route pass *through* a non-soft,
non-endpoint block — a hard-macro wall blocks transit entirely regardless of channels beyond it,
so that geometry could never route at all (`open_routes>0`, not a congestion scenario).

## Why the test doesn't assert "overflow reduced vs no-respace"

Same honesty concern as T5: this specific fixture's *existing* (pre-T6) `finalizeFeedthrough`
already drives both mismatch and overflow to 0 via its bounded repair-jiggle loop (32 random
mutations per pass), so a literal "overflow reduced vs no-respace" comparison on this exact
fixture would show no numeric difference — not because the hook is inert, but because this
fixture's repair-jiggle already happens to find a legal zero-overflow arrangement. I verified this
directly: re-running the identical fixture through the pre-T6 and post-T6 `finalizeFeedthrough`
produces the same final numbers (mismatch=0, overflow=0, validation ok). Rather than tune the
fixture further to force a fragile numeric gap (which risks becoming a flaky/over-fit test as
engine internals change), I asserted the acceptance criteria the brief actually named: mismatch
reaches 0, `open_routes==0`, `validateSolution().ok`, and determinism — the same never-worse
contract `respaceFixedPoint` already guarantees, exercised end-to-end through
`finalizeFeedthrough`. Overflow-reduction evidence across the real 5-case corpus is T7's sweep.

## Test results (verbatim tail)

- `bash test/run_p3_regressions.sh` → all 18 tests ok, `P3 T1+T2+T3+T4+T5+T6 regressions passed`,
  `P3 regressions passed`.
- `bash test/run_p1_regressions.sh` → all tests ok, `P1 regressions passed`.
- `bash test/run_p0_regressions.sh` → all tests ok, `P0 regressions passed`.
- `bash test/run_case00.sh` → passed, `validation=ok`.
- `bash test/run_placement_engine_regressions.sh` → `placement_engine_regression: ok`.
- `bash test/run_20260618_regressions.sh` → `regression_20260618: ok`.
- `bash test/run_20260608_regressions.sh` → `regression_20260608: ok`, `validation=ok`.
- `make` → warning-clean (`-Wall -Wextra -pedantic`).

## Concerns / follow-ups

- The added `respaceFixedPoint` calls run against `inflated` (the FT-grown problem) at up to 3
  iterations each, for both the raw and compacted variants, per finalize pass (up to 3 passes) —
  a bounded but real increase in exact-route calls inside `finalizeFeedthrough`. No timeout issue
  observed on any of the seven suites, but noted for the P4 profiling item already tracked in
  `progress.md` ("36-evals=accept-till-P4-profiling").
- On the corpus tested here, this hook did not change the final committed numbers (see above) —
  consistent with T5's finding that respace is frequently a no-op on cases where the existing
  machinery (repair-jiggle / outlineSweep) already reaches overflow 0. Genuine overflow-reduction
  evidence belongs to T7's sweep, not this task.

## Commit

`feat: respace inside feedthrough finalize` (pending push in this session).
