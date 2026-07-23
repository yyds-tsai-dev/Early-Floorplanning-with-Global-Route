# Task 6 Report: Full verification + cost sweep + doc update

## Status: DONE (all suites GREEN, doc updated, committed) — WITH A FLAGGED FOLLOW-UP

All five regression suites pass and validation is `ok` on every one of the 15 AFTER runs (no suite failure, no validation failure — the brief's BLOCKED trigger conditions are not met). However, the cost sweep surfaced a real, previously-undetected defect in `finalizeFeedthrough`'s acceptance guard (`src/search.cpp:96-97`) that leaves `feedthrough_mismatch` nonzero on 2 of 15 engine/case combinations (`hybrid` on case1 and case3), contradicting the brief's "mismatch==0 everywhere" expectation. This is documented in detail below and in the strategy doc; no code fix was made (out of this task's doc-only scope) — flagging for the orchestrator to decide on a follow-up task.

## Commit
`b75e473` on branch `fable5` — "docs: record P0 scoring-fidelity completion and cost sweep results", plus a follow-up correction `748ab3d` — "docs: correct P0 cost-sweep finding on feedthrough finalize guard" (sharpened the case1/case3 hybrid anomaly write-up after a deep-reasoner investigation returned a more precise root cause than my initial read). HEAD is `748ab3d`.

## Step 1: Full rebuild + all regressions

```
$ make clean && make
... 8 files compiled, g++ -std=c++17 -O2 -Wall -Wextra -pedantic, zero warnings ...
$ echo BUILD EXIT: 0
```

All five suites, each rebuilding first via their own `make clean && make`:

```
$ bash test/run_case00.sh
engine=bstar iters=500 exact_evals=313 blocks=7 bundles=11 channels=11 routes=13 score=2.62562e+07 outline_area=6.47352e+06 wirelength=5.44387e+06 channel_overflow=0 feedthrough_mismatch=0 open_routes=0 validation=ok
engine=bstar iters=25  exact_evals=30  blocks=7 bundles=11 channels=10 routes=13 score=3.69164e+07 outline_area=6.47352e+06 wirelength=8.24991e+06 channel_overflow=0 feedthrough_mismatch=0 open_routes=0 validation=ok
EXIT: 0

$ bash test/run_placement_engine_regressions.sh
placement_engine_regression: ok
EXIT: 0

$ bash test/run_20260618_regressions.sh
regression_20260618: ok
EXIT: 0

$ bash test/run_20260608_regressions.sh
regression_20260608: ok
engine=bstar iters=50 exact_evals=13 blocks=3 bundles=1 channels=4 routes=3 score=3.63056e+09 outline_area=12000 wirelength=376952 channel_overflow=3230.48 feedthrough_mismatch=0 open_routes=0 validation=ok
EXIT: 0

$ bash test/run_p0_regressions.sh
testFtMathOfficialExamples ok
testRoutingMismatchUsesCeiledMath ok
testDirectAbutmentZeroWirelength ok
testOutlineConsistencyEvalVsCfg ok
testFeedthroughFinalizeWritesBackArea ok
testFinalizeFeedthroughDirect ok
p0 regressions passed
P0 regressions passed
EXIT: 0
```

**All five suites pass. Build is warning-clean.** HEAD at time of run: `15ad8ec1346b92ca9d6e04465f87fb61e73cb6b5` (branch `fable5`).

## Step 2: Before/after cost sweep

Ran `bin/efgr "test/2026 ICCAD_case.caseN.csv" out.cfg --engine ENGINE --iters 2000 --seed 1` for `caseN` in `case0..case4` × `ENGINE` in `sp,bstar,hybrid` (15 runs).

- **AFTER** = current HEAD `15ad8ec`, built in-place via `make clean && make`.
- **BEFORE** = pre-P0 baseline `81a4e27f5928` ("docs: add cost-reduction strategy..."), built in an isolated worktree (`git worktree add /tmp/p0_before 81a4e27f5928` + `make -C /tmp/p0_before`... actually `make` run with cwd set to the worktree), then torn down (`git worktree remove --force /tmp/p0_before`).

### Cost table

| Case | Engine | score (before → after) | outline_area (before → after) | wirelength (before → after) | channel_overflow (before → after) | ft_mismatch (before → after) |
|---|---|---|---|---|---|---|
| case0 | sp     | 1.32981e+07 → 1.38054e+07 | 6.47352e+06 (same) | 1.17437e+07 → 1.17418e+07 | 0 (same) | 0 → 0 |
| case0 | bstar  | 3.31217e+07 → 3.49438e+07 | 6.47352e+06 (same) | 7.12544e+06 → 7.12304e+06 | 0 (same) | 0 → 0 |
| case0 | hybrid | 1.32981e+07 → 1.38054e+07 | 6.47352e+06 (same) | 1.17437e+07 → 1.17418e+07 | 0 (same) | 0 → 0 |
| case1 | sp     | 5.28117e+10 → 5.31373e+10 | 7.61914e+06 (same) | 4.37130e+07 → 4.37117e+07 | 51967.7 (same) | 0 → 0 |
| case1 | bstar  | 3.41863e+10 → 3.44200e+10 | 7.81099e+06 (same) | 4.67091e+07 → 4.66932e+07 | 33744.4 (same) | 0 → 0 |
| case1 | hybrid | 3.95763e+10 → 4.52637e+10 | 7.70811e+06 (same) | 1.97570e+07 → 1.97366e+07 | 30204.7 (same) | **9034.12 → 14479.2** |
| case2 | sp     | 8.96237e+10 → 9.32960e+10 | 4.20000e+07 (same) | 2.11233e+08 → 2.11225e+08 | 87570.9 (same) | **101.451 → 0** |
| case2 | bstar  | 8.84452e+10 → 8.86481e+10 | 4.20000e+07 (same) | 2.22937e+08 → 2.22922e+08 | 86736.2 (same) | 0 → 0 |
| case2 | hybrid | 7.06610e+10 → 7.08288e+10 | 4.20000e+07 (same) | 2.69260e+08 → 2.69253e+08 | 68936.4 (same) | 0 → 0 |
| case3 | sp     | 9.67545e+10 → 9.69316e+10 | 2.34157e+07 (same) | 1.40098e+08 (same) | 96188.5 (same) | 0 → 0 |
| case3 | bstar  | 7.85292e+10 → 7.87006e+10 | 2.34157e+07 (same) | 1.25993e+08 (same) | 78048.1 (same) | 0 → 0 |
| case3 | hybrid | 6.07492e+10 → 6.39076e+10 | 2.34157e+07 (same) | 1.14642e+08 → 1.14620e+08 | 26182.2 (same) | **34006.2 → 36987.5** |
| case4 | sp     | 1.90885e+11 → 1.90924e+11 | 3.77819e+07 (same) | 3.33606e+08 → 3.33605e+08 | 190350 (same) | 0 → 0 |
| case4 | bstar  | 2.05348e+11 → 2.05397e+11 | 3.77819e+07 (same) | 3.41655e+08 (same) | 204501 (same) | 0 → 0 |
| case4 | hybrid | 1.90885e+11 → 1.90924e+11 | 3.77819e+07 (same) | 3.33606e+08 → 3.33605e+08 | 190350 (same) | 0 → 0 |

All 15 BEFORE and 15 AFTER runs reported `open_routes=0 validation=ok`.

### Anomaly investigation: residual `feedthrough_mismatch` in case1/hybrid and case3/hybrid — REAL DEFECT, follow-up needed

The brief's expectation was `feedthrough_mismatch == 0 everywhere AFTER`. That held (as *reported*) for 13 of 15 rows, and case2/sp improved from 101.451 → 0. case1/hybrid (9034.12 → 14479.2) and case3/hybrid (34006.2 → 36987.5) remained nonzero and got slightly larger.

I dispatched a `deep-reasoner` investigation (read-only) to root-cause this before writing it up. Its finding sharpens (and partially corrects) my own initial read of the code: **this is a real, fixable defect in `finalizeFeedthrough`'s acceptance guard, not merely an inherent convergence limitation.**

Root cause (instrumented run by the investigating agent, `src/search.cpp:71-102` `finalizeFeedthrough`; guard at `:96-97`; call sites `:213` inside every `runPlacementSearch`, `:227,230,232` both hybrid legs):

- The commit guard is `mismatch <= kEps && open_routes == 0 && exact.value < result.best_score.value`. The third clause is fatal in practice: inflating a soft block's area and re-decoding via `seedFromPlacement`/`decode` (a full re-pack, not an in-place grow) shifts every downstream block and usually spills something out-of-outline or overflows a channel, both penalized at `1e12`/`1e6` in `exactScore`. So the FT-corrected candidate's *total* score is almost always far worse than the current best, and the guard discards it — **even on passes where `feedthrough_mismatch` itself legitimately reached 0.**
- Confirmed by instrumentation: case1/hybrid's `bstar` leg never reaches mismatch 0 in any of its 3 passes (per-pass scores ~1.7e12/1.96e12/4.49e12 vs. best 4.53e10 — all rejected). case3/hybrid's `sp` leg *does* reach `new_mismatch = 0` on pass 0, but at `new_score = 2.19e11 > best = 6.39e10`, so that mismatch-free fix is discarded too.
- case2/sp's apparent "genuine fix to 0" is not actually the guard succeeding: hybrid picks whichever of its `sp`/`bstar` legs scores better, and the `bstar` leg's own SA search happened to land on a mismatch-0 placement directly (before finalize was even needed). So rows reporting `feedthrough_mismatch = 0` reflect the search landing there on its own, not `finalizeFeedthrough` correcting a nonzero one.
- Not a call-order/wiring bug — finalize runs inside every `runPlacementSearch` call, both hybrid legs included.
- All affected rows still report `open_routes=0` and `validation=ok` — the residual mismatch is a scoring penalty, not a legality violation, so nothing here is unsafe to ship as-is. But the P0 roadmap's "write-back to a fixed point" goal (§4 item 1) is **not fully met**: the guard needs to accept whenever `feedthrough_mismatch` strictly decreases (pay down the mismatch penalty first, independent of total score), then attempt to re-legalize/compact afterward to reclaim outline/overflow, instead of gating acceptance on total `exact.value`.

Both the strategy doc (§4 item 1, now marked "PARTIALLY DONE" instead of "DONE", plus the Appendix) and this report reflect the corrected finding. **No code fix was applied in this task** (out of scope — Task 6 brief is verification + doc update only); this is reported as a follow-up item, not silently downgraded to "known limitation."

### Other observations
- `outline_area` and `channel_overflow` are unchanged before vs. after in every row — expected, since P0 items 1-3 only touch FT math/write-back and outline unification, not the placement search itself (identical blocks/bundles/channels/routes counts per row confirm the SA landed on the same topology both times).
- `wirelength` is flat to 5 significant figures everywhere — the abutment fix (`4a9cf72`) didn't change these particular runs' chosen topology at `--iters 2000`/`--seed 1`; it's covered by its own regression (`testDirectAbutmentZeroWirelength`) but wasn't exercised differently here.
- No outline growth was observed in this sweep — write-back didn't trigger area growth large enough to move `outline_area` for these five cases at these seeds; score deltas are dominated by the ceil'd FT math changing mismatch-penalty magnitude and minor floating-point differences in route geometry.

## Step 3: Strategy doc updated

`docs/research/2026-07-06-cost-reduction-strategy.md`:
- §4 P0 items 1-3 marked done with commit hashes (`3a04376`, `262e77c`, `15ad8ec` for item 1; `3a04376`, `262e77c` for item 2; `10d7615` for item 3), each annotated with the regression test name and (for item 1) the residual-mismatch limitation found in this sweep.
- Item 4 annotated: "Deferred to P4: exact overflow penalty is already amount-proportional (1e6 × overflow um); only the SA weight *schedule* remains, which is search-engineering, not scoring fidelity."
- New `## Appendix: P0 before/after cost sweep (2026-07-06)` section appended at the end of the doc with the full cost table and the anomaly write-up above.

## Step 4: Commit
`b75e473` — "docs: record P0 scoring-fidelity completion and cost sweep results" (doc-only change; `docs/research/2026-07-06-cost-reduction-strategy.md`, 42 insertions / 4 deletions).

## Note: reverted stray debug instrumentation before committing

A background investigation agent (dispatched to root-cause the residual `feedthrough_mismatch`, read-only by instruction) left `fprintf` debug tracing in `src/search.cpp` (`finalizeFeedthrough`, `runHybridPlacementSearch`) plus an untracked `hyb_probe.sh` probe script in the repo root. Neither was requested by this task's brief (doc-only scope). Both were reverted/removed (`git checkout -- src/search.cpp`, `rm hyb_probe.sh`) before the full rebuild + regressions were re-verified and the commit was made, so the committed tree contains only the doc change.
