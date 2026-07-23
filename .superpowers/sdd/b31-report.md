# B3.1 Report — Post-finalize outline sweep pass recovers pad gains

Status: **DONE**. Implemented exactly per the coordinator's B3.1 brief; every case improves or ties
(no regression); `run_b1_regressions.sh` stays green; wall-time guard comfortably satisfied against
the original pre-B3 baseline; 14/14 suites green, warning-clean.

## Motivation

`b3-report.md`'s "Concerns" #1 (honest finding, carried forward as a flagged follow-on): B3's pad
rungs measurably help (`-0.06%`/`-6.02%`/`-2.08%` on cases 1/2/4, probed directly on each case's final
committed rects) but that saving never reached the end-to-end checker cost, because
`maybeAdoptRespace`/`finalizeFeedthrough` run once, AFTER the elite loop's last `outlineSweep` call,
and can move `result.best_placement` afterward with no further sweep pass to try the pad rungs on that
final geometry.

## The change

`src/search.cpp`'s `runPlacementSearch` (the 4-arg, `initial_state`-seeded overload): after
`finalizeFeedthrough(problem, engine, result);` and before `return result;`, added ONE additional
sweep pass — `outlineSweep(problem, result.best_placement, exactCached)` (raw) and a
`compactTowardOrigin` variant of the same, each adopted under the existing `outlineSweepBetter` gate
(legality-first, then value) exactly like the elite loop's own two adoptions. No further
`respaceFixedPoint` or `finalizeFeedthrough` call after it — a single extra pass, not a fixed-point
loop, per the brief's explicit scope. `legacy_paydown_*` is left untouched.

**FT-inflated-dims safety, verified (not assumed) by reading the legalization code paths**: at this
point `result.best_placement`'s soft blocks may carry FT-inflated dims (`finalizeFeedthrough`'s grow).
Read `placement_engine.cpp`'s `commitOutlineAt` (`:905-935`) and `shrinkWrapOutline`
(`:1036-1080`)/`anchorAtOutline` (`:1083-1087`, which shares `commitOutlineAt`): both only ever
re-derive a rect from `problem.blocks[i]` via `dimensionFor` for **edge** blocks
(`if (isEdge(problem.blocks[i].type)) trial.rects[i] = dimensionFor(...)`) — every other block's rect
(soft/macro) is carried through from the input placement exactly as given, never resized from
`problem.blocks[i].area`. So an already-inflated soft block sweeps like any other placement's rect
(the legalizer just fits an outline/repositions edge blocks around whatever is already there), and
`exactScore`'s own FT-mismatch accounting can only look the same or better against a larger existing
placed area, never worse. No hard incompatibility found; no STOP triggered.

## Measured: 5-case default matrix (checker cost, golden rescorer, HEAD `8e82204` = post-B1.1 before vs after)

Same invocation both times: `bin/efgr "test/2026 ICCAD_case.caseN.csv" out.cfg` (default flags), rescored
via a freshly-built `bin/efgr_rescore_b31` (`tools/rescore.cpp`, same tool `run_cfg_score_regressions.sh`
validates). "Before" measured with the B3.1 edit stashed (`git stash push -- src/search.cpp`, rebuild,
run, `git stash pop`, rebuild) — confirms B1.1 made no difference either (matches `b3-report.md`'s own
row exactly).

| case | before checker_cost | after checker_cost | Δ | Δ% | before overflow_nets | after overflow_nets |
|---|---:|---:|---:|---:|---:|---:|
| 0 | 5,389,935.000 | 5,389,935.000 | 0 | 0.00% | 0 | 0 |
| 1 | 14,031,473.201 | 13,246,805.650 | −784,667.55 | **−5.59%** | 918 | **0** |
| 2 | 65,625,655.415 | 59,420,652.769 | −6,205,002.65 | **−9.46%** | 9,739 | 1,831 |
| 3 | 36,615,637.236 | 36,615,637.236 | 0 | 0.00% | 6,703 | 6,703 |
| 4 | 91,530,188.369 | 89,523,552.207 | −2,006,636.16 | **−2.19%** | 60,112 | 48,825 |
| **Σ** | **213,192,889.221** | **204,196,582.862** | **−8,996,306.36** | **−4.22%** | | |

**Every case ≤ before — no regression, STOP condition not triggered.** All 3 improving cases *beat*
`b3-report.md`'s own direct-probe estimates (case1 −5.59% vs its −0.06% probe; case2 −9.46% vs −6.02%;
case4 −2.19% vs −2.08%, closely matching) — see "Adoption telemetry" below for why case1 in particular
did much better than the original probe suggested. `run_b1_regressions.sh`'s pinned upper bounds
(`kPreB1DefaultUpperBounds`, set against the much looser pre-B1 baseline) are still satisfied by a wide
margin, and the suite is green.

## Adoption telemetry — which cases adopted what

Captured via a temporary, non-committed `getenv`-gated trace in the two adoption `if` blocks (reverted
before the final build/commit — the committed diff has no trace code). `runHybridPlacementSearch` calls
`runPlacementSearch` per leg (sp, bstar_a, and conditionally bstar_b), so each leg independently gets its
own post-finalize sweep pass; the trace below is per-leg, and the FINAL reported value is whichever leg
wins the hybrid pick (`paydownBetter`) afterward.

| case | legs where a sweep adopted | winning mechanism |
|---|---|---|
| 0 | none | no adoption on any leg — case0 already sits at its true tight/margin optimum post-finalize |
| 1 | sp leg: raw sweep 14,031,473→14,006,943 (−0.06%, matches `b3-report.md`'s original probe exactly); a **bstar leg** (separately): raw sweep 16,271,400→16,266,700, then **compacted sweep** 16,266,700→**13,283,600** | the bstar leg's own post-finalize sweep found a much better basin (via the compacted variant) that the sp leg's own sweep never could reach — this is *new* value B3.1 unlocks by sweeping every leg, not just probing the final hybrid pick as `b3-report.md`'s diagnostic did |
| 2 | winning leg: raw sweep 65,650,100→**59,445,100** (matches final `score.value` exactly); a losing leg also improved (67,139,100→66,917,200; 77,788,800→77,230,200) but stayed non-competitive | raw sweep alone; compacted variant never fired for this case |
| 3 | a losing leg: raw sweep 61,756,700→58,917,900 | did **not** change the final result — the winning leg's margin=0.20 rung already dominates (per `b3-report.md`'s case3 finding), so this leg's own improvement was never competitive either; net 0.00% |
| 4 | winning leg: raw sweep 91,952,600→**90,036,800** (matches final `score.value` exactly); a losing leg also improved slightly (96,971,100→96,773,300) | raw sweep alone; compacted variant never fired for this case |

## Wall-time guard (re-measured against the ORIGINAL pre-B3 baseline, per the brief)

`b3-report.md`'s own pre-B3 numbers (HEAD `1326819`): case4 alone 8.421s; 5-case total 19.465s.

| | pre-B3 baseline | post-B3.1 (this task) | ratio |
|---|---:|---:|---:|
| case4 alone | 8.421s | 11.706s | **1.39×** |
| 5-case total | 19.465s | 30.132s | **1.55×** |

Both comfortably under the brief's 2× threshold (this is the *cumulative* ratio across B3 + B3.1; B3.1's
own marginal cost on top of B3 alone is small — B3's own post-B3/pre-B3.1 numbers were 11.244s/27.344s).
`exact_evals` grew modestly per case from the one extra sweep pass per leg (e.g. case1 1506→2300, case4
2692→2831) — still single-digit-to-low-double-digit seconds per case against the contest's 2h/8-case
budget.

## Tests

### Extended: `test/regression_b3_20260724.cpp` + `test/run_b3_regressions.sh`

New `testPostFinalizeSweepNeverExceedsPreB31Case2Default`: runs the same default search `bin/efgr`'s
default flags use on case2, writes the resulting cfg, and re-scores it through the golden rescorer
(`cfg_score.hpp` — the same oracle `bin/efgr_rescore`/`run_cfg_score_regressions.sh` use), asserting
`checker_cost <= 65,625,655.42 + 1.0` (the pre-B3.1 baseline, full precision measured at HEAD `8e82204`,
plus a small absolute headroom for 3-decimal `.cfg`-rounding noise — the same rounding-awareness
`run_selfconsistency_regressions.sh` documents). Measured: `59,420,652.77`, comfortably under the bound.
`run_b3_regressions.sh` gained `src/cfg_score.cpp` to its g++ sibling list for this.

### Full battery: 14 suites, all green; warning-clean

```
PASS run_case00
PASS run_placement_engine_regressions
PASS run_20260618_regressions
PASS run_20260608_regressions
PASS run_p0_regressions
PASS run_p1_regressions
PASS run_p2_regressions
PASS run_p3_regressions
PASS run_testdata_regressions
PASS run_cfg_score_regressions
PASS run_selfconsistency_regressions
PASS run_integer_output_regressions
PASS run_b1_regressions
PASS run_b3_regressions
```
`make clean && make`: zero warnings, confirmed on the final (post-revert-of-temporary-trace) rebuild
alongside the full suite run.

**Process note**: an earlier battery run was accidentally contaminated mid-run by a temporary trace
edit made to `src/search.cpp` while that battery was still executing in the background (a
synchronization mistake, not a code defect) — it was discarded and the full battery re-run cleanly,
uninterrupted, after the trace code was fully reverted and the revert verified byte-for-byte against
the originally-tested B3.1 diff.

## Files changed

- `src/search.cpp` — `runPlacementSearch` (4-arg overload): post-finalize sweep pass inserted between
  `finalizeFeedthrough(...)` and `return result;`.
- `test/regression_b3_20260724.cpp` — new end-to-end pin test + `cfg_score.hpp`/`<cstdio>` includes.
- `test/run_b3_regressions.sh` — added `src/cfg_score.cpp` to the g++ sibling list.
- `.superpowers/sdd/b31-report.md` (this file), `.superpowers/sdd/progress.md` (ledger line).

## Commit

One commit, staged exactly the files above; message:
`feat(search): post-finalize outline sweep pass recovers pad gains (B3.1)`.

## Concerns / carried forward

1. Case3's own improvable leg (61.76M→58.92M via the sweep) never wins the hybrid pick because a
   different leg's margin=0.20 rung already dominates — consistent with `b3-report.md`'s existing
   case3 finding, not a new issue.
2. The adoption telemetry above was captured via a temporary, non-committed instrumentation pass
   (`getenv`-gated trace prints), then fully reverted — the committed diff is exactly the sweep pass
   itself, no trace code.
