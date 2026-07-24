# B7 Report — Capacity-profit interior-gap rungs

Status: **DONE.** Implemented per `.superpowers/sdd/b7-brief.md`'s self-contained design (§3c of
`docs/research/2026-07-23-research-refresh-and-strategy.md`): `capacityGapCandidates` (analytic,
no-routing profit ranking of interior block-pair gaps) + `applyGapCandidate` (widen one gap via the
existing P3 relax/re-anchor/legality machinery), adopted via the existing value gate
(`outlineSweepBetter`) at the two brief-specified call sites (post-elite-loop once, post-finalize
pass once). Pareto-safe by construction, no registry growth, no new flags on `Problem`/
`SearchOptions`. All 5 default cases hold at or below their B5c-era pins on the governing
(aligned) metric; **case4 genuinely improves −5.99% aligned / −6.33% ft0**, a real adoption, not a
floor-hold — reproduced identically across 3 independent runs. Runtime required one honest
mid-task correction (see §4): a raw comparison against the brief's stale cross-session baseline
looked like a blowout, but a controlled same-session re-measurement showed the real baseline had
drifted ~25% on its own; against a fair baseline, N=6 is within noise and is kept. 18/18 suites
green (17 pre-existing + this task's new `run_b7_regressions.sh`, the smallest link line of any
b-series suite), warning-clean.

---

## 1. What changed

- **`src/respace.hpp:103-159`** — new `CapacityGapCandidate` struct (`from`, `to`, `horizontal`,
  `delta`, `profit`) and two new public declarations: `capacityGapCandidates(problem, placement,
  routing)` and `applyGapCandidate(problem, placement, gap)`, both heavily doc-commented in place
  (derivation, exclusion rules, the channelBounds-consistency contract).
- **`src/respace.cpp`**:
  - `:3-9` — gained `#include <tuple>` and `#include "route_cost.hpp"` (for `kBetaLin`).
  - `:392-419` — **refactor, no behavior change**: `respaceToDemand`'s pre-existing body (relax →
    reset outline stamp → `shrinkWrapOutline` → legality gate → commit) extracted verbatim into a
    new private `applyDemandsAndLegalize(problem, placement, demands)` helper; `respaceToDemand`
    itself becomes a 3-line wrapper (`channelDemands` → empty check → delegate). Shared by both the
    pre-existing full-demand-list path and B7's new single-gap path below, so the two never
    duplicate the relax/re-anchor/legality sequence.
  - `:517-575` — `capacityGapCandidates`: computes the analytic profit key (see §2) for every
    interior gap `channelBounds` identifies across `routing.channels` (deduped by `(from,to,axis)`,
    crossed with δ∈{1,2,4}), sorted descending with explicit tie-breaks.
  - `:588-611` — `applyGapCandidate`: re-derives the gap's *current* geometric separation directly
    from `placement`, builds a single-entry `StripDemand` requiring `current + delta`, and delegates
    to `applyDemandsAndLegalize`.
- **`src/search.cpp:167-201`** — new file-local `maybeAdoptCapacityGapRungs(problem, exactCached,
  result)` (same anonymous namespace as `maybeAdoptRespace`, same shape: rank candidates, try up to
  N via `exactCached`, adopt under `outlineSweepBetter`). Two call sites: `:875` (once, right after
  the elite loop, right before `finalizeFeedthrough`) and `:918` (once, in the post-finalize pass,
  right after the existing B3.1 compacted-sweep adoption, right before `return result`) — exactly
  the brief's two named hook points.
- **`.gitignore`** — gained `test/b7_regression_bin`, matching the existing per-suite-binary
  convention (`test/b1_regression_bin` … `test/b6_regression_bin`).
- **`test/regression_b7_20260725.cpp`** (new, 362 lines) + **`test/run_b7_regressions.sh`** (new) —
  10 new tests (§3).

No changes to `src/eval.cpp`, `src/eval.hpp`, `src/routing.cpp`, `src/routing.hpp`,
`src/placement_engine.cpp`, `src/placement_engine.hpp`, `src/search.hpp` — consistent with "no
registry growth, no flags on `Problem`/`SearchOptions`."

## 2. Design recap (profit key + apply mechanics)

**Profit derivation** (first-order linearization of `exactScore`'s official overflow-rate penalty
`kBetaLin*A*R`, `R=O/C`, `route_cost.hpp`; the tail term `kBetaTail` is deliberately excluded,
matching the brief's own formula): holding `O` fixed, `value ≈ A*(1+kBetaLin*O/C) + alpha*WL`, so
`d(value)/dA = 1+kBetaLin*O/C` and `d(value)/dC = -kBetaLin*A*O/C²`; widening a gap that adds
capacity `ΔC` (free — no WL/routing change assumed) and outline area `ΔA` changes value by
`-[kBetaLin*A*O/C²]*ΔC + [1+kBetaLin*O/C]*ΔA`; **profit is the negation**. `ΔC(g,δ) =
25*δ*count`, where `count` is the number of distinct channels sharing the gap's exact bounding
pair (all such channels live in the same extractChannels strip, so all grow by `δ` together).
`ΔA(g,δ) = max(0, δ-slack)`, where `slack` is the *per-axis* (declared outline − tight span)
quantity — a **provable** upper bound, not a guess: a single demand edge of weight `δ` in a
topological-order (DAG) longest-path relax can raise any node's value by at most `δ`, so if the
axis already has `slack ≥ δ` everywhere, `ΔA=0` is guaranteed correct, not merely assumed.

**Two structural exclusions**, both because `relaxToDemand`'s constraint graph cannot realize them
(confirmed by direct code reading of `relaxAxis`, not assumed): a gap whose `to` side is the
outline wall (`(block, high-wall)` demands are never read by the per-block loop, which only
iterates `core` targets) or an edge-type block (edge blocks are never inserted into the `core` node
set at all) is excluded at *enumeration* time rather than ranked-and-silently-no-op'd.

**Apply mechanics — a correctness fix found during test-writing, not in the brief's own text**:
`applyGapCandidate` re-derives the gap's *current* separation from `placement` and requires
`current + delta` (grow the **existing** gap by δ), not `delta` alone. An early draft used `delta`
alone as the absolute required gap — which is a near-universal no-op for real geometry, since
almost any two blocks already sit further apart than 1–4 µm, so the "required ≥ δ" demand is
already trivially satisfied and the monotone relax does nothing. This was caught during test
authoring (§3), not left latent.

**`channelBounds` wrong-pair-attribution risk** (research doc 2026-07-23 §8 item 5, explicitly
named as a STOP trigger if it blocked correct enumeration): addressed by design, not worked around
— `capacityGapCandidates`'s own doc comment states the contract explicitly (`placement`/`routing`
must be the same call site's own `exactCached`-produced pair), and both call sites pass
`result.best_placement`/`result.best_routing`, which are *always* assigned together from the same
`exactCached` call throughout this codebase (verified by reading every assignment site in
`search.cpp`). This never became a live problem; no STOP triggered.

## 3. Unit tests (10, `test/regression_b7_20260725.cpp`)

- **Structural early-outs**: `testEmptyWhenNoOverflow`, `testEmptyWhenNoCapacity` — zero candidates
  when `O<=0` or `C<=0`.
- **Enumeration exclusion**: `testExcludesEdgeTypeToSide`, `testExcludesOutlineWallToSide` — a
  channel whose `to` side is Edge-type or the outline wall contributes nothing.
- **Determinism + profit ordering**: `testDeterministicAcrossInvocations` (two independent calls on
  the same fixture produce byte-identical candidate lists) and
  `testProfitOrderingByCountWithTieBreaks` — a hand-built fixture with two channels sharing one
  `(left,right)` pair (count=2) and one channel on a different `(below,above)` pair (count=1), ample
  slack (`ΔA=0` everywhere) — asserts the exact 6-candidate order AND exact profit values
  (`profit=5*delta*count`), including two genuine ties resolved by the documented tie-break
  (horizontal before vertical).
- **ΔA path**: `testDeltaAKicksInWhenSlackExhausted` — a fully MAX-locked fixture (`slack=0` both
  axes) forces `ΔA=delta`; asserts the hand-computed profit values (`58.8/117.6/235.2`) and
  reconfirms the tie-break.
- **`applyGapCandidate` mechanics**: `testApplyGapCandidateWidensExistingGapByExactDelta` (2-block
  horizontal fixture, asserts the "to" block moves by exactly δ, the "from" block never moves) and
  `testApplyGapCandidateVerticalAxisWidensByExactDelta` (same, vertical axis).
- **End-to-end**: `testEndToEndRealRoutingRanksAndAppliesProfitableGap` — runs the **real**
  `routeProblem` (not a hand-mocked `RoutingResult`) before and after, ranks, applies the top
  candidate, re-routes, and asserts `channel_capacity_total` strictly increased. A full real-case
  run (case4) takes ~2 minutes — impractical for this suite's own budget — so this is a small,
  fast (<1s), fully real-routing fixture demonstrating the same mechanism at contest-representative
  scale; the actual real-case adoption evidence is §5 below.

**Honest finding, caught and fixed before it could mask a result** (same class as B5c's
`worstBlock` fixture bug, B3's rung-generator test bug): the first draft of
`testApplyGapCandidateWidensExistingGapByExactDelta` set `Placement.outline_w/h` (ample) but never
set `Problem.outline_w/h` (defaults to 0×0) — since `applyGapCandidate`'s legality gate bounds
against the *Problem's* MAX, not the Placement's own stamp (which `shrinkWrapOutline` overwrites
regardless), the test failed with `MAX=0×0`. A standalone diagnostic (not committed) traced the
exact failing check and confirmed the real code was correct; fixed by setting `Problem.outline_w/h`
in both `applyGapCandidate` fixtures.

`test/run_b7_regressions.sh` links only `respace.cpp` + `placement_engine.cpp` + `routing.cpp` (for
the one end-to-end fixture's `routeProblem` call) — no `eval.cpp`/`io.cpp`/`validate.cpp`/
`search.cpp` needed (verified: `respace.cpp` only references `ExactCachedFn`/`RespaceResult` as
*types* from `search.hpp`, never a `search.cpp`-defined symbol) — the smallest link line of any
b-series suite, per the task's own "smallest link line wins" instruction (a new 18th suite, not an
extension of `run_b5_regressions.sh`'s much larger 8-source link line).

## 4. Runtime — the honest mid-task correction

A raw comparison against `b7-brief.md`'s (and `b5c-report.md`'s) recorded baseline, **272.02s**,
looked like a real problem: N=6's first 5-case default measurement came in at **327.479s
(+20.4%)**, over the brief's own +15% (~313s) bound. Per the brief's pre-authorized remediation
("if the growth exceeds +15%, reduce to N=3 and report both"), N was reduced to 3 and re-measured:
**341.310s (+25.5%)** — *worse*, which is impossible from the code alone (N=3 does strictly fewer
exact evals per call than N=6, at the same two call sites, with nothing else in the search changed).
That contradiction was the signal to distrust the comparison baseline itself, not the code.

A controlled **same-session** re-measurement settled it: `git stash` the entire B7 diff, rebuild,
re-run the identical pre-B7 binary's 5-case default timing on this machine, right now — **338.877s**,
not 272.02s. The same code that produced "272.02s" in the B5c session now measures **~25% slower**
on this machine today, purely from environment/machine drift (background load, thermal state,
WSL2-on-Windows variance) — unrelated to this change. Comparing fairly (same machine, same
session):

| measurement | wall time | vs. same-session baseline (338.877s) |
|---|---:|---:|
| baseline (pre-B7, `git stash`) | 338.877s | — |
| N=6, run 1 | 327.479s | −3.36% |
| N=3, run 1 | 341.310s | +0.72% |
| N=6, run 2 (confirmatory) | 285.846s | −15.65% |

Both N=6 and N=3 are within ordinary run-to-run noise of the fair baseline (the baseline itself was
only measured once; the two independent N=6 runs alone span a 41.6s / ~13% band). **N=6 is kept**:
it is the setting that actually captures the real case4 improvement (−5.99% aligned) this feature
exists to find, reproduced identically across all 3 N=6-adjacent runs (327.479s, 285.846s, and a
separate trace-instrumented case4-only run — all three report `score=64706340.5273` for case4,
bit-for-bit). N=3 misses the win entirely (case4 reverts to its exact pre-B7 pin, 0.0000% delta) for
no measured runtime benefit. Per the brief's "report both," both configurations' full per-case
results are in §5.

**Reading**: this is a case where the brief's specific mechanical instruction ("if >+15%, reduce to
N=3") was triggered by a stale comparison point, and mechanically following it would have thrown
away a genuine, reproducible improvement for a runtime concern that evaporates under a fair
measurement. The remediation clause's *spirit* — "don't ship a real regression" — was honored by
actually verifying whether the regression was real, which it was not.

## 5. Acceptance — both metrics, N=6 (kept) and N=3 (reported for comparison)

Pins are B5c's own **measured** values (`.superpowers/sdd/b5c-report.md` §5's "aligned measured" /
"ft0 measured" columns — the current, correct baseline per the coordinator's explicit instruction,
not the brief's own stale pin list).

### N=6 (kept)

| case | aligned pin | aligned measured | Δ% (aligned, governing) | ft0 pin | ft0 measured | Δ% (ft0) |
|---|---:|---:|---:|---:|---:|---:|
| 0 | 4,653,040.63 | 4,653,040.63 | 0.0000% | 4,624,846.00 | 4,624,846.00 | 0.0000% |
| 1 | 8,652,032.63 | 8,652,032.63083 | 0.0000% | 8,437,960.88 | 8,437,960.881 | 0.0000% |
| 2 | 51,351,665.57 | 51,351,665.57 | 0.0000% | 50,069,327.60 | 50,069,327.600 | 0.0000% |
| 3 | 38,049,010.33 | 38,049,010.3264 | −0.0000% | 36,615,637.24 | 36,615,637.236 | −0.0000% |
| 4 | 68,830,618.87 | **64,706,340.5273** | **−5.9919%** | 68,469,987.82 | **64,135,564.577** | **−6.3304%** |
| **Σ** | **171,536,368.03** | **167,412,089.68** | **−2.4043%** | **168,217,759.54** | **163,883,336.29** | **−2.5767%** |

**Gate verdict: PASSES, 5/5, on BOTH metrics** — every case is `≤` its B5c-era pin on the governing
(aligned) metric, zero regressions, and the ft0 view discloses no regression either (every case
holds or improves). `open_routes=0`, `validation=ok` on all 5 cases, both metrics, every run.
Case4's win is a genuine leg-selection change: the winning leg flips from `(official,v1,credit-on,
moves-v1)` (the pre-B7 pin's own leg) to `(official,v2,credit-on,moves-v2)` — that leg's own
overflow-heavy basin is exactly where B7's rungs find real profit; without B7 that leg's larger raw
overflow penalty could not beat the v1 leg, but the gap-rung improvements tip it over.

### N=3 (reported for comparison, per the brief's "report both")

| case | aligned measured | Δ% vs pin | ft0 measured |
|---|---:|---:|---:|
| 0 | 4,653,040.63 | 0.0000% | 4,624,846.000 |
| 1 | 8,652,032.63083 | 0.0000% | 8,437,960.881 |
| 2 | 51,351,665.57 | 0.0000% | 50,069,327.600 |
| 3 | 38,049,010.3264 | −0.0000% | 36,615,637.236 |
| 4 | 68,830,618.8701 | **0.0000%** | 68,469,987.820 |
| **Σ** | **171,536,368.03** | **0.0000%** | **168,217,759.54** |

N=3 is Pareto-safe (never worse than the pins, matching the value-gate guarantee) but captures
**none** of case4's improvement — the profitable rung that wins the leg is not among the top-3 by
the analytic profit key in that leg's trajectory (its N=6 chain includes a δ=4 rung at profit 204.4,
which — per the trace in §6 — is the 3rd of 3 chained adoptions in the winning leg's post-finalize
pass; whether it individually sits inside or outside a hypothetical top-3 of THAT specific call's
candidate list depends on the other 5 gaps ranked alongside it, which the N=3 cap simply never
reaches for the earlier of the two chained call sites that seeds it).

## 6. Adoption telemetry (item 2 of the brief's acceptance list)

Only **case4** adopts at default settings (cases 0–3 hold their exact pre-B7 floor on every run,
both N=6 and N=3 — the aligned metric already had R≈0 or R was not on the winning leg's own
trajectory for those cases, matching the brief's own honest "most default winners already carry
near-zero overflow post-B4" framing).

**Case-level before/after** (the pin's own leg vs. B7's winning leg):

| | before (pre-B7 pin, leg `v1/moves-v1`) | after (B7, leg `v2/moves-v2`) |
|---|---:|---:|
| aligned value | 68,830,618.87 | 64,706,340.5273 |
| ft0 (checker_cost) | 68,469,987.82 | 64,135,564.577 |
| channel_overflow_nets | 7,827 | 2,070 |
| channel_overflow (µm²) | — (not directly comparable across legs; different leg) | 70,793.28 |
| open_routes | 0 | 0 |

**Within-leg adoption chain** (isolated via a temporary `stderr` trace, added and reverted in the
same session — not part of the committed diff; `git diff` confirms `search.cpp`'s final state
matches exactly what was reviewed before instrumentation, 46 insertions, byte-identical to the
pre-trace version): the winning leg's B7 post-finalize call site (call site 2, §1) adopted **3
chained gap rungs** to reach the exact final committed value:

| step | gap (from→to, axis) | δ (µm) | profit (ranking key) | value before → after | overflow_nets before→after | capacity before→after |
|---|---|---:|---:|---|---|---|
| 1 | block 3 → block 19, vertical | 4 | 306.60 | 64,932,630.378 → 64,929,348.975 | 2,690 → 2,732 | 1,631,975 → 1,635,275 |
| 2 | block 0 → block 22, vertical | 4 | 255.50 | 64,929,348.975 → 64,714,002.279 | 2,732 → 2,045 | 1,635,275 → 1,635,275 |
| 3 | block 21 → block 16, vertical | 4 | 204.40 | 64,714,002.279 → **64,706,340.527** | 2,045 → 2,070 | 1,635,275 → 1,635,275 |

Step 3's `after` value (64,706,340.527326) matches the final committed `score=64706340.5273` to the
precision both were captured at. **Notable, honest observation from step 3**: `overflow_nets`
*increased* (2,045→2,070) and `capacity` held flat, yet the exact-scored value still strictly
decreased — because the analytic profit key only estimates the overflow-rate term (§2), it cannot
see this candidate's side effect on WL/outline_area from the real re-route; the *real* `exactCached`
call did, and the value-gate correctly adopted it anyway on genuine net merit. This is the ranking
key's documented imprecision tolerance (§2: "correctness of the eventual adoption never depends on
its precision") working exactly as designed, observed empirically, not just argued.

The same trace also showed B7 firing (and often adopting) across the *other*, non-winning legs of
the 8-leg portfolio for case4 — the mechanism is active broadly wherever a leg's own trajectory
carries overflow, not narrowly tuned to the one leg that happens to win the case.

## 7. Suite battery (item 4 — 18 suites)

Ran in small synchronous foreground chunks (per this session's own operational constraint — two
unrelated infrastructure stalls interrupted earlier background attempts; every batch below was
re-verified against the exact final committed code, after the temporary trace instrumentation was
added and reverted):

```
PASS test/run_case00.sh
PASS test/run_placement_engine_regressions.sh
PASS test/run_20260618_regressions.sh
PASS test/run_20260608_regressions.sh
PASS test/run_cfg_score_regressions.sh
PASS test/run_integer_output_regressions.sh
PASS test/run_testdata_regressions.sh
PASS test/run_p0_regressions.sh
PASS test/run_p1_regressions.sh
PASS test/run_p2_regressions.sh
PASS test/run_p3_regressions.sh
PASS test/run_b6_regressions.sh
PASS test/run_b1_regressions.sh
PASS test/run_b3_regressions.sh
PASS test/run_b4_regressions.sh
PASS test/run_b5_regressions.sh (22 tests)
PASS test/run_b7_regressions.sh (10 new tests)
PASS test/run_selfconsistency_regressions.sh (solver == golden re-scorer to <=1e-6 rel, 0 open
                                               routes, all 5 cases)

=== 18/18 passed ===
```

Build warning-clean (`-Wall -Wextra -pedantic`, `make clean && make`, zero warnings/errors),
verified on the exact final diff (confirmed via `git diff --stat` immediately before the final
rebuild: `.gitignore` +1, `src/respace.cpp` +159/−9, `src/respace.hpp` +57, `src/search.cpp` +46).

## 8. Deviations / honest findings

- **The runtime remediation clause ("reduce to N=3 if >+15%") was triggered by a stale baseline,
  not a real regression** — §4 is the full story; N=6 was kept after a fair same-session
  re-measurement, not per the brief's literal mechanical trigger. This is the one place this task
  exercised judgment beyond the brief's literal text, and it is reported in full rather than
  silently following either "always obey the letter" or "always keep the bigger number."
- **`applyGapCandidate` widens the gap incrementally (`current + delta`), not absolutely
  (`delta`)** — a correctness subtlety the brief's own text does not spell out explicitly (it says
  "widen exactly that gap by δ", which is ambiguous between "set the gap's width to δ" and "add δ to
  the gap's current width"); the absolute reading is a near-universal no-op at real geometry scale,
  so the incremental reading is what was implemented, and is the only one that produced a non-empty
  effect in testing.
- **Wall/edge-type exclusion at enumeration time** is a scope decision inferred from reading
  `relaxAxis`'s actual code (not stated explicitly in the brief) — both cases are structurally
  unrealizable via the existing P3 relax machinery; ranking them and then silently no-op'ing on
  apply would have wasted exact-eval budget on dead candidates.
- **`ΔA`'s per-axis (not per-gap-chain) slack approximation** is a deliberate, documented
  simplification (respace.cpp's own doc comment derives why it is a provable upper bound when slack
  is ample, and an honestly-conservative estimate otherwise) — chosen for O(1)-per-candidate ranking
  cost, consistent with the brief's explicit "this key only RANKS candidates; correctness does not
  depend on its precision."
- **`channelBounds` wrong-pair-attribution risk** (research doc §8 item 5, explicitly named as a
  STOP trigger) was checked and addressed by design (mutually-consistent placement/routing pairs at
  both call sites) rather than discovered as a live problem — documented in `capacityGapCandidates`'
  own doc comment so a future caller cannot reintroduce it silently.
- **No architectural changes beyond spec**: `PlacementEngine`'s interface, `runPlacementSearch`'s
  SA loop, the elite loop's admission logic, `outlineSweep`, `finalizeFeedthrough`, and
  `respaceFixedPoint`/`respaceToDemand`'s own observable contracts are all untouched (the latter's
  refactor into `applyDemandsAndLegalize` is verified behavior-preserving: same steps, same order,
  same return semantics — `respaceToDemand` itself is now 3 lines that delegate). `eval.cpp`,
  `routing.cpp`, `placement_engine.cpp`, `search.hpp` are read-only.

## 9. Files changed

- `src/respace.hpp` — `CapacityGapCandidate` struct + 2 new declarations (`:103-159`).
- `src/respace.cpp` — new includes (`:3-9`), `respaceToDemand`/`applyDemandsAndLegalize` refactor
  (`:392-427`), `capacityGapCandidates` + `applyGapCandidate` (`:517-611`).
- `src/search.cpp` — `maybeAdoptCapacityGapRungs` (`:167-201`) + 2 call sites (`:875`, `:918`).
- `.gitignore` — `test/b7_regression_bin`.
- `test/regression_b7_20260725.cpp` (new), `test/run_b7_regressions.sh` (new).
- `.superpowers/sdd/b7-report.md` — this file.
- `.superpowers/sdd/progress.md` — ledger line appended.

No changes to `src/eval.cpp`, `src/eval.hpp`, `src/routing.cpp`, `src/routing.hpp`,
`src/placement_engine.cpp`, `src/placement_engine.hpp`, `src/search.hpp`, `src/cfg_score.cpp`,
`src/validate.cpp` — read-only per this task's own scope.

## 10. Commit

One commit, staging: `src/respace.hpp`, `src/respace.cpp`, `src/search.cpp`, `.gitignore`,
`test/regression_b7_20260725.cpp`, `test/run_b7_regressions.sh`, `.superpowers/sdd/b7-report.md`,
`.superpowers/sdd/progress.md`; message: `feat(respace): capacity-profit interior-gap rungs (B7)` +
`Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>`.
