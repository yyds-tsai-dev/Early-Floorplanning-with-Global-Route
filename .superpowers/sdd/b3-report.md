# B3 Report — Capacity micro-margin rungs in `outlineSweep`

Status: **DONE**. Implemented exactly per `.superpowers/sdd/b3-brief.md`; Pareto-safety holds on all
5 default cases (checker cost after-B3 ≤ before-B3 on every case, measured at HEAD `1326819`, the B1
commit); wall-time guard comfortably satisfied (no need to shrink the pad set); 14/14 suites green,
warning-clean. One honest, non-obvious finding carried forward (see "Concerns" below): the lever is
demonstrably real and larger than the brief's own single-strip estimate on 3 of 5 cases, but a
pipeline-sequencing gap (`maybeAdoptRespace`/`finalizeFeedthrough` running after the last
`outlineSweep` call, both out of B3's scope) currently prevents that value from reaching the
end-to-end checker cost on those cases.

## The change

`src/search.cpp`'s `outlineSweep` (`:157-298`): kept the existing 7-rung margin ladder
(`{0.0, 0.02, 0.05, 0.10, 0.20, 0.50, 1.0}`) byte-for-byte, and added a second ladder of integer-µm
pads `{1, 2, 4, 8, 16, 32, 64}` × 3 variants (W-only, H-only, both) added to the tight frontier
(`tight_w`/`tight_h`), each clamped to MAX per axis — 21 additional candidate targets per elite,
before dedupe. Every pad target goes through the same `anchorAtOutline` (→ `commitOutlineAt`) path
the existing margin>0 rungs already use; only the m=0.0 rung uses `shrinkWrapOutline`, unchanged.

Dedupe mechanism (brief: "extend that mechanism rather than inventing a new one"): the original
`last_target_w`/`last_target_h` (compare only to the immediately-preceding accepted rung) is
*provably* equivalent to a full-history check for the margin ladder alone — `target_w`/`target_h`
there is a non-decreasing step function of margin (ceil of a non-decreasing linear function, clamped
at MAX), so a repeat can only ever equal the last distinct value. I generalized it into a
`tried_targets` vector checked against the *entire* history so far, shared by both the margin loop
(unchanged behavior, per the equivalence above — confirmed empirically too, see Tests) and the new
pad loop (whose W-only/H-only/both targets are *not* monotonic in loop order, so they need the
full-history form). The per-rung body (construct variant → anchor → `exactCached` → adopt-if-better)
was factored into a shared `tryTarget` lambda used by both loops, rather than duplicated — this is
the one structural change to the existing loop; its target computation, dedupe semantics, and
adoption rule are otherwise untouched.

`include_pads` (`src/search.hpp:66-67`, default `true`): a test-only escape hatch the brief
explicitly sanctioned ("a temporary test-only parameter on outlineSweep is acceptable if defaulted to
'pads on'"), letting a test compare pads-on vs pads-off on the same base placement. Every production
call site (`search.cpp:578, 590` — the elite loop's raw + compacted sweeps) omits the 4th argument,
so the default applies and behavior there is unaffected by the parameter's mere existence.

Did **not** touch comparator functions, `respace.cpp`, `finalizeFeedthrough`, `routing.cpp`, or
`eval.cpp`, per the brief.

## Measured: 5-case default matrix (checker cost, golden rescorer, HEAD `1326819` before vs after)

Same invocation both times: `bin/efgr "test/2026 ICCAD_case.caseN.csv" out.cfg` (default flags:
hybrid, iters=1000, seed=1, budget off), rescored via `bin/efgr_rescore` (`tools/rescore.cpp`, the
`checker_cost`/`capacity` fields — same tool `test/run_cfg_score_regressions.sh` validates).
"Before" measured with B3's edits stashed (`git stash push -m b3-before-measurement --
src/search.cpp src/search.hpp`, rebuild, run, `git stash pop`, rebuild) — HEAD unchanged throughout,
matching the b1-report.md per-case table exactly (cross-checked: 5,781,379 / 14,031,473.20 /
65,625,655.42 / 36,615,637.24 / 91,530,188.37, Σ 213,584,333.22 — identical to the "hybrid@0" row of
B1's own sweep matrix).

| case | before checker_cost | after checker_cost | Δ | Δ% | capacity before | capacity after |
|---|---:|---:|---:|---:|---:|---:|
| 0 | 5,781,379.000 | 5,389,935.000 | −391,444.00 | **−6.77%** | 187,825 | 218,550 |
| 1 | 14,031,473.201 | 14,031,473.201 | 0.00 | 0.00% | 551,850 | 551,850 |
| 2 | 65,625,655.415 | 65,625,655.415 | 0.00 | 0.00% | 1,135,100 | 1,135,100 |
| 3 | 36,615,637.236 | 36,615,637.236 | 0.00 | 0.00% | 612,200 | 612,200 |
| 4 | 91,530,188.369 | 91,530,188.369 | 0.00 | 0.00% | 2,194,025 | 2,194,025 |
| **Σ** | **213,584,333.221** | **213,192,889.221** | **−391,444.00** | **−0.183%** | | |

**Pareto-safety check: PASS.** Every case satisfies after ≤ before (4 exact ties, 1 strict
improvement); no regression on any case; `test/run_b1_regressions.sh`'s pinned upper bounds
(`kPreB1DefaultUpperBounds`, which this same matrix must stay under) are still satisfied and the
suite is green.

### Why 4 of 5 cases show a byte-identical Σ, and why case0 moved

I did not stop at "no case regressed" — I wanted to know *why* the lever visibly fired on only one of
the five default cases, since the brief's own single-strip experiment measured −0.14%/−0.31% on
cases 1/4 specifically. Built two throwaway diagnostics (not committed; `/tmp/b3/rung_probe.cpp` and
`/tmp/b3/sweep_direct.cpp`, discarded after use) that (a) match a case's final committed outline
against every rung-target formula to identify which rung "won", and (b) call the real `outlineSweep`
directly (with the new `include_pads` parameter) on a case's final committed rects to see exactly
what pads-on vs pads-off produces there.

| case | rung that won (before AND after — see note) | pads-on vs pads-off, direct probe on the AFTER-B3 final rects (solver's internal `score.value`, includes FT pricing) |
|---|---|---|
| 0 | tight (m=0.0) | 5,389,935.000 both ways (R=0, no lever to exploit here — see below) |
| 1 | tight (m=0.0) | with=14,006,943.153 vs without=14,015,965.450 (**−0.064%**) |
| 2 | tight (m=0.0) | with=59,445,119.329 vs without=63,253,694.879 (**−6.02%**) |
| 3 | margin-ladder m=0.20 | 38,049,010.326 both ways (m=0.20 already dominates any tight-relative pad) |
| 4 | tight (m=0.0) | with=90,036,831.777 vs without=91,952,579.009 (**−2.08%**) |

This says two different things:

1. **Case0's improvement is *not* a pad winning directly.** Both before and after B3 land on the tight
   rung, but at genuinely different rects (before: tight=(2284,2206), area 5,038,504; after:
   tight=(2605,1803), area 4,696,815) — confirmed by `rung_probe` on the actual before/after `.cfg`
   files. Case0 has R=0 (no overflow) both before and after, so a pad can never win there on its own
   merits — the "pads-on vs pads-off" probe on case0's own final rects shows zero difference,
   confirming this directly. The explanation is the same **cascading-trajectory effect** B1's report
   already documented for a different change (comparator orderings): `outlineSweep` runs many times
   across the elite pass, `runHybridPlacementSearch` seeds its bstar legs from an earlier leg's
   `best_placement`, and adding 21 more per-call candidates can change *which* intermediate candidate
   becomes "best" at some point mid-run even when none of those candidates is a pad rung winning in
   isolation — which cascades into a different, here better, final basin. This is expected: the
   brief's own "Pareto-safe by construction" claim is a *local* property (any single `outlineSweep`
   call, given more candidates, can only match or improve *that call's* result) — it does not claim
   the *global* multi-iteration trajectory is pointwise identical, and it visibly isn't here.
2. **On cases 1, 2, and 4, the pad rungs are proven to help substantially when given the chance**
   (directly, via the real `outlineSweep` function, not a re-implementation) — case2's −6.02% is far
   larger than the brief's own single-strip estimate — **but this saving is not reaching the
   end-to-end checker cost**, because the geometry `outlineSweep` actually swept during the real run
   is not the same geometry that ends up committed: `maybeAdoptRespace`/`finalizeFeedthrough` run
   once, after the whole elite loop, and (given cases 2/4's heavy channel overflow, respace's whole
   purpose) evidently do further adjust `result.best_placement` on these cases without another sweep
   pass afterward to try the new pad rungs on *that* geometry. See "Concerns" below — this is a
   genuine opportunity, but fixing it means touching `respace`/`finalizeFeedthrough`/
   `runHybridPlacementSearch`'s call sequence, explicitly out of this brief's scope.
3. **Case3 shows the ladder working as designed**: margin=0.20 already reaches far past what any
   tight-relative pad (max +64µm) can reach, so the pads are correctly, harmlessly dominated there.

(Incidental finding while building this diagnostic, unrelated to B3 and not a regression: the m=0.0
rung's own branch re-invokes `shrinkWrapOutline` — a re-bisecting minimizer, given a ceiling of
`(tight_w,tight_h)` instead of true MAX — and this second, narrower-range bisection can converge to
an *even tighter* frontier than the single outer `shrinkWrapOutline(problem, ·)` call found, because
the bisection's tolerance is relative to the given range. Observed on case4:
outer tight=(5366,5065), but the m=0.0 rung's own reshrink lands at (5366,5050). This is 100%
pre-existing behavior — the m=0.0 branch is byte-identical to the pre-B3 code, just moved into the
shared `tryTarget` lambda verbatim — and it is what forced a fix in my own rung-generator test, see
Tests below. Not something this task should or does change.)

## Wall-time guard (brief item 4)

Case4 (slowest case), 3 repeats each:

| | before | after (3 repeats) |
|---|---|---|
| case4 alone | 8.421s | 11.219s / 11.255s / 11.257s (avg 11.244s) |
| **ratio** | | **1.34×** |

5-case total (sum of per-case wall time from the same before/after runs used for the checker-cost
matrix): before 19.465s → after 27.344s, ratio **1.40×**.

Both ratios are well under the brief's 2× threshold, so **no reduction of the pad set was needed**
— kept the full `{1, 2, 4, 8, 16, 32, 64}` × 3. `exact_evals` grew ~3.2–3.8× per case (e.g. case4
711→2692), consistent with up to 21 extra candidates/elite before dedupe; still single-digit seconds
per case against the contest's 2h/8-case budget.

## Tests

### New: `test/regression_b3_20260724.cpp` + `test/run_b3_regressions.sh`

Kept as its own suite rather than folding into `test/regression_b1_20260723.cpp` (the brief offered
either option): the two suites' g++ sibling lists are identical either way (extending b1's file would
add zero new lines to its own g++ command, a new file needs one new but byte-for-byte-identical g++
line), so the "keep the g++ line smallest" tie-break doesn't favor either option — I judged a separate
file cleaner given `run_b1_regressions.sh` is explicitly the STOP-condition tripwire for this task
("if that suite goes red your rungs leaked past the value gate somewhere"); mixing B3's own new test
code into it would blur that signal. Sibling set: `routing.cpp eval.cpp io.cpp validate.cpp
placement_engine.cpp search.cpp respace.cpp` — verified `floorplan.cpp` (present in B1's g++ line) is
not actually needed by this file and left it out.

1. **`testOutlineSweepPadRungsGenerated`** (rung-generator unit test): a cheap bstar decode (no SA
   loop) on case4 gives a real placement with genuine tight-to-MAX slack on both axes. A spy
   `ExactCachedFn` (same technique as `test/regression_p1_20260706.cpp`'s
   `testOutlineSweepProbesDistinctOutlinesCase0`) records every distinct `(W,H)` target
   `outlineSweep` actually tries. Asserts: every target integral; every target ≤ MAX per axis; no
   duplicate targets across the whole call (not just consecutively); every margin>0 rung target,
   recomputed independently via the same formula, is present unchanged; all 21 pad-derived targets
   are present (case4's asserted slack guarantees none clamp away). The m=0.0/tight rung is checked
   separately (tight-or-tighter than a single-pass estimate, not byte-equal) per the pre-existing
   double-bisection behavior found above — documented inline in the test with the same case4 numbers
   as this report.
   First run caught exactly this m=0.0 subtlety (assertion failure on the naive equality check),
   confirming the test is actually exercising real behavior, not vacuously passing — I fixed the
   test's expectation, not the production code, since the m=0.0 branch is untouched.
2. **`testOutlineSweepPadRungsNeverWorseCase2Default`** (mechanism test, brief item 2): runs the same
   default search bin/efgr's default flags use (`runHybridPlacementSearch`, `SearchOptions{}` —
   hybrid, iters=1000, seed=1) on case2 (nonzero overflow at default settings, R≈0.0072 per the
   research doc), then drives `outlineSweep` directly on `result.best_placement` with
   `include_pads=true` vs `false` and asserts pads-on's value is never worse — Pareto-safety by
   construction (pads-on tries a strict superset of pads-off's candidates). Measured/pinned in the
   test's own comment: with_pads=59,445,119 vs without_pads=63,253,695 on this exact seed — a real,
   substantial difference, not a marginal/flaky tie, matching the diagnostic table above exactly.

### Full battery: 14 suites, all green; warning-clean

```
PASS test/run_case00.sh
PASS test/run_placement_engine_regressions.sh
PASS test/run_20260618_regressions.sh
PASS test/run_20260608_regressions.sh
PASS test/run_p0_regressions.sh
PASS test/run_p1_regressions.sh
PASS test/run_p2_regressions.sh
PASS test/run_p3_regressions.sh
PASS test/run_testdata_regressions.sh
PASS test/run_cfg_score_regressions.sh
PASS test/run_selfconsistency_regressions.sh
PASS test/run_integer_output_regressions.sh
PASS test/run_b1_regressions.sh
PASS test/run_b3_regressions.sh
```
`make clean && make`: zero warnings (`-Wall -Wextra -pedantic`), confirmed on the final rebuild
alongside the full suite run.

## Files changed

- `src/search.cpp` — `outlineSweep` (`:157-298`): added `kPads`, generalized dedupe
  (`last_target_w/h` → `tried_targets` + `alreadyTried`), factored the per-rung body into a shared
  `tryTarget` lambda, added the pad loop, added the `include_pads` parameter.
- `src/search.hpp` — `outlineSweep` declaration (`:55-67`): added `include_pads = true`, updated doc
  comment.
- `test/regression_b3_20260724.cpp` (new), `test/run_b3_regressions.sh` (new).
- `.gitignore` — added `test/b3_regression_bin` (matches the existing per-suite pattern).
- `.superpowers/sdd/b3-report.md` (this file), `.superpowers/sdd/progress.md` (ledger line).

## Commit

One commit, staged exactly the files above (never `git add -A`); message:
`feat(search): capacity micro-margin rungs in outlineSweep (B3)` +
`Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>`.

## Concerns / carried forward

1. **The lever's realized value is currently gated by pipeline sequencing, not by the lever itself.**
   Directly measured (see table above): on cases 1/2/4, calling the real `outlineSweep` with pads on
   the case's own final committed rects finds a −0.06%/−6.02%/−2.08% improvement respectively — real
   and, for case2, an order of magnitude larger than the brief's own single-strip estimate. None of
   this reaches the end-to-end checker cost today because `maybeAdoptRespace`/`finalizeFeedthrough`
   run once, after the entire elite loop, and (at least on these overflow-heavy cases) evidently
   adjust the placement further without another `outlineSweep` pass afterward to try the pad rungs on
   the post-respace/post-finalize geometry. A follow-on task — re-running `outlineSweep` (with pads)
   once more after `maybeAdoptRespace`/`finalizeFeedthrough` settle `result.best_*`, gated by the same
   `outlineSweepBetter` value rule so it stays Pareto-safe — looks like a concrete, well-scoped,
   higher-value continuation of this lever. Out of scope here (`respace`/`finalizeFeedthrough` were
   explicitly off-limits for B3); flagged as a spawned background-task suggestion for the user to
   pick up separately.
2. **Case0's −6.77% improvement is a genuine, verified Pareto-safe result but is mechanistically a
   cascading-trajectory side effect** (more elite-pass candidates → a different intermediate "best" at
   some point → a different final basin), not the capacity-denominator mechanism the brief was
   written to harvest. Flagging this so it isn't mis-attributed in any future accounting of "how much
   did the capacity lever save" — the honest answer on these 5 default cases is "0% via the
   capacity mechanism directly; −0.18% Σ via an incidental, benign search-trajectory shift."
3. The m=0.0 rung's pre-existing double-bisection behavior (tighter-than-single-pass-estimate
   convergence) is unrelated to B3 and unchanged by it, but is worth knowing about for anyone reading
   `outlineSweep`'s trace in the future — documented inline in both `search.cpp`'s `tryTarget` comment
   context and the new test.
