# B2 Report — Portfolio generalization: seed dimension, budget policy, anytime output, submission hardening

Status: **DONE.** Implemented per `.superpowers/sdd/b2-brief.md`, on top of HEAD `a066d73` (post-B7,
18 suites). Phase 2 of `runPricingPortfolio`'s budget path is now a **seed sweep** (independent
full-registry restarts at `options.seed + 1000*round`), REPLACING B5a.2's continued-SA phase 2;
`writeCfg` is now **atomic** (temp-write + rename) and `runPricingPortfolio` takes an optional
**anytime-write callback**, invoked after every leg (both phases) that improves the running
fold-best, so a killed/overrun run always leaves a valid best-so-far `.cfg` on disk; `SearchResult`
gained `picked_seed`/`legs_run` telemetry, surfaced in `main.cpp`'s stderr summary. Budget-off is
**byte-identical to pre-B2** (hard gate, md5-verified 3 independent times across this session,
including once after adding-then-reverting a temporary analysis trace). Budget-on is a real,
substantial win: the required `{600s}×{hybrid}` column improves 4 of 5 cases (aggregate **−8.51%**
aligned), and the `{0,15,45}×{hybrid,bstar}` sweep gate holds on all 30 cells (several cells
genuinely improve, contrary to the brief's own "phase-2 never fires at 15/45s" prediction for
`bstar`'s cheaper legs — a one-directional gate, so improvements are not violations). 19/19 suites
green (18 pre-existing + this task's new `test/run_b2_regressions.sh`), warning-clean, clean tree.

---

## 1. What changed

- **`src/search.hpp`**:
  - `SearchResult` gained `picked_seed` (`:172`, default 0) and `legs_run` (`:180`, default 1) —
    telemetry only, same precedent as `pricing_official`/`budget_phase2`/etc.
  - `:358` — `using AnytimeWriteFn = std::function<void(const Problem&, const Placement&, const
    RoutingResult&)>` — the anytime-write callback type.
  - `:373-386` — new `PortfolioFold` struct (`result`, `winner`, `picked_seed`, `legs_run`,
    `total_iterations`, `total_exact_evaluations`, `have_result`) + `admit(...)` declaration — the
    streaming value-gated fold, extracted so the "write on improvement" logic is directly
    unit-testable without running a real 8-leg search.
  - `:395` — `bool shouldRunPhase2Leg(double remaining_sec, double leg_phase1_wall_time_sec)` — the
    brief's per-leg skip predicate (`remaining < 1.5 * leg_phase1_wall_time_sec` → skip), hoisted as
    a free, pure function for the same reason.
  - `:450-451` — `runPricingPortfolio` gained a 4th parameter, `const AnytimeWriteFn& on_improved =
    AnytimeWriteFn()` — default-empty, so every existing 3-arg call site (all pre-existing regression
    files) is source-compatible unchanged.
  - `SearchResult.budget_phase2`'s doc comment and `runPricingPortfolio`'s own doc comment rewritten
    in place to describe the seed-sweep mechanism (see the doc comments themselves for the full
    T5/Cicirello rationale) — no observable-field renames, just accurate documentation.
- **`src/search.cpp`**:
  - `:1037-1040` — `shouldRunPhase2Leg` implementation (`kPhase2LegMargin = 1.5`, local constant).
  - `:1043-1057` — `PortfolioFold::admit` implementation: folds one leg via the pre-existing
    `paydownBetter`, updates `legs_run`/`picked_seed`/totals, calls `on_improved` iff this leg became
    the new best.
  - `:1075-1163` — `runPricingPortfolio` rebuilt: phase 1 now times EACH leg individually
    (`leg_wall_times`, alongside the pre-existing aggregate `t0`) and folds through
    `PortfolioFold::admit` inside the same loop (interleaved, not batched-then-folded — proven
    behavior-preserving, see §2). Phase 2 is the new seed sweep (`kPhase2SeedStep = 1000`): for
    `round = 1, 2, 3, ...`, try the full registry again at `options.seed + 1000*round`, checking
    `shouldRunPhase2Leg` against each leg's own phase-1 wall time BEFORE that leg runs (not once per
    round); a round that starts zero legs ends the sweep. The old `kPhase2Floor = 2.0` global-floor
    constant is gone — subsumed by `shouldRunPhase2Leg` itself (round 1 admits nothing when remaining
    can't clear even the cheapest leg, the identical net effect).
- **`src/io.cpp`** (`:199-247`): `writeCfg` now writes to `path + ".tmp"`, then atomically
  `std::rename`s over `path` — every caller (including `main.cpp`'s own final write) is anytime-safe
  with no separate "unsafe" writer to keep in sync. Gained `#include <cstdio>` for `std::rename`.
  Byte content written is unchanged (same field-by-field emission, just wrapped in temp+rename).
- **`src/main.cpp`**:
  - `:113-115` — `runPricingPortfolio` call now passes a 4th-arg lambda that calls `writeCfg(
    output_path, ...)` — the anytime-write wiring. The pre-existing unconditional final `writeCfg`
    call (a few lines later) is untouched, so a normal-exit run's on-disk file is unaffected by the
    callback's existence (see §3).
  - stderr summary gained ` seed=<s> legs_run=<n>` immediately after `budget_phase2=`.
- **`.gitignore`** — gained `test/b2_regression_bin`.
- **`test/regression_b2_20260725.cpp`** (new, 227 lines) + **`test/run_b2_regressions.sh`** (new) —
  10 new tests (§4).

No changes to `src/eval.cpp`, `src/routing.cpp`, `src/placement_engine.cpp`, `src/respace.cpp`,
`src/validate.cpp`, `src/cfg_score.cpp` — consistent with the brief's scope ("no new search behavior
outside the portfolio driver").

## 2. Design notes

**Streaming fold is provably behavior-preserving.** The pre-B2 code collected all 8 phase-1 results
into a vector, then folded them in a *separate* loop (`result = phase1_results[0]`, then `for i=1..7:
if paydownBetter(...) result = phase1_results[i]`). B2's `PortfolioFold::admit` folds each leg
**as it completes**, inside the same loop that runs it. These are the same sequence of comparisons:
`admit`'s first call always returns `true` (`!have_result` short-circuits, matching
`phase1_results[0]`'s unconditional seed), and every subsequent call compares the new leg against the
**running** `result` via the identical `paydownBetter` call — exactly what the old loop's `if
paydownBetter(phase1_results[i], result)` already did against its own running `result`. Re-verified
empirically (§3): all 5 default cases stay byte-identical.

**Phase 2 termination is structural, not capped.** A seed round that cannot start even its cheapest
leg ends the sweep, because remaining budget only shrinks in later rounds (`shouldRunPhase2Leg`'s
`remaining_sec` argument is `options.time_budget_sec - t0 - elapsed_phase2`, monotonically
non-increasing) — so no artificial round cap was needed, and none was added.

**Why phase 1 can itself exceed the requested `--time-budget`** (pre-existing B5a.2 behavior,
unchanged by B2, but visible throughout this task's own measurements — e.g. `hybrid case4 @45s`
below actually ran ~118s): phase 1 is *unconditionally* the full default run
(`phase1_options.time_budget_sec = 0.0`), regardless of what budget the caller requested — the
budget only controls phase 2. A budget smaller than phase 1's own cost simply means phase 2 never
starts; it never truncates phase 1 itself. This is what makes the "never worse than default"
guarantee structural rather than empirical.

**Anytime output is unconditional (not gated on `--time-budget`).** The brief's own motivation (A42:
the *aggregate* 8-case/2h contest limit, not any single invocation's own flag) can kill a run
regardless of whether that invocation happened to pass `--time-budget`. `runPricingPortfolio`'s
`on_improved` therefore fires on every improving leg in **both** phase 1 and phase 2, budget on or
off — main.cpp always wires it. This does not threaten the byte-identity gate: the pre-existing
unconditional final `writeCfg` call (unchanged) always re-publishes the exact bytes the *last*
anytime write already produced (since `result` never changes after the last `admit` that improved
it), so the on-disk file when the process exits normally is identical with or without the callback.

## 3. Acceptance

### 3.1 Budget-off byte-identity (hard gate) — PASSES, verified 3×

md5 of the 5 default-flag `.cfg` outputs, identical across: (a) a pre-change baseline captured before
any B2 edit, (b) immediately after landing the core feature, (c) after adding-and-reverting the
temporary β_ft/runtime-projection trace (§5/§6) — same hashes every time:

```
case0.cfg  ed7c374b43f06c7c0d133fb16de250e4
case1.cfg  6eb0015d45f182bfcc8a4b9818e3c688
case2.cfg  c530d3b081a884058bab4894b5824d86
case3.cfg  3388a0a277109fd7fb28469e22e4b8b7
case4.cfg  47dbb5f3e667f38fe5fc47c110732f98
```

### 3.2 Budget-on smoke (`--time-budget 420`, case1 and case4) — PASSES

| case | budget_phase2 | seed (picked) | legs_run | value (budget-off) | value (@420s) | Δ% | runtime |
|---|---|---:|---:|---:|---:|---:|---:|
| 1 | yes | 1 (phase-1 winner still wins) | 98 | 8,652,032.63083 | 8,652,032.63083 | 0.00% | 415.99s |
| 4 | yes | 2001 (phase-2 round 2) | 27 | 64,706,340.5273 | 59,220,469.6963 | **−8.48%** | 405.93s |

Both: phase-2 ran ≥1 extra seed, final value ≤ phase-1's (monotone, as required), runtime ≤ budget +
one-leg overshoot (both finished *under* 420s here — the next candidate leg's margin check failed
before the budget ran out). case4 is a genuine, substantial win, not a tie.

**Anytime file validity mid-run**: `test/regression_b2_20260725.cpp`'s
`testAnytimeWriteFinalFileMatchesFinalBestOnRealCase` captures every intermediate write via the
callback and byte-compares the last one against a fresh `writeCfg` of the final returned result (see
§4). As a live, human-observable supplement, a separate real run (case2, `--time-budget 40`,
untracked scratch invocation, not part of the regression suite) was polled by `ls -la
--time-style=full-iso` / `wc -c` while running:

```
t+~0s   6624 bytes  (leg 0's write — file exists almost immediately)
t+~13s  6297 bytes  (an improving leg found a different, better candidate)
t+~19s..38s  6297 bytes (stable — no further improvement in this window)
t+~38s  6310 bytes  (one more improvement near the end)
final   6310 bytes, score=51,351,665.57 — matches case2's actual reported final score exactly
```

The file is present, well-formed (`head -1` → `OUTLINE 5332.000 5939.000`), and non-decreasing in
correctness (each write is a `paydownBetter`-admitted improvement) throughout the run.

### 3.3 `{600s}×{hybrid}` column (5 cases) — first multi-seed evidence

| case | budget-off value | @600s value | Δ% | budget_phase2 | seed (picked) | legs_run |
|---|---:|---:|---:|---|---:|---:|
| 0 | 4,653,040.63 | 4,194,977.63 | **−9.84%** | yes | 123001 (round 123) | 1754 |
| 1 | 8,652,032.63083 | 8,652,032.63083 | 0.00% | yes | 1 (phase-1 still wins) | 149 |
| 2 | 51,351,665.57 | 45,896,745.27 | **−10.62%** | yes | 1001 (round 1) | 80 |
| 3 | 38,049,010.3264 | 35,201,487.00 | **−7.48%** | yes | 7001 (round 7) | 65 |
| 4 | 64,706,340.5273 | 59,220,469.6963 | **−8.48%** | yes | 2001 (round 2, same winner as the 420s smoke) | 41 |
| **Σ** | **167,412,089.68** | **153,165,712.23** | **−8.51%** | | | |

**4 of 5 cases win outright** from the seed sweep (case1 explores extensively — 141 extra legs
across ~18 rounds — but its phase-1 basin is not beaten even at 600s). Every winning leg's `open_
routes=0`, `validation=ok`. Runtime stayed at or under 600s for every case (588–600s, no per-case
overshoot beyond the budget itself in this batch — the largest observed overshoot in this task was
~0s here; the 420s smoke cases similarly finished under budget). This is exactly the "first
multi-seed evidence" item 4 of the brief calls for: extra seeds win 4/5 cells, by margins from
−7.48% to −10.62%.

### 3.4 Budget sweep gate `{0,15,45}×{hybrid,bstar}` (aligned metric) — HOLDS, all 30 cells

All cells `≤ budget-0 + 1%` (a one-directional ceiling — genuine improvements are not violations).

**hybrid** (budget-0 = the pins in §3.1/§3.3):

| case | @0 | @15 | Δ%@15 | @45 | Δ%@45 |
|---|---:|---:|---:|---:|---:|
| 0 | 4,653,040.63 | 4,558,446 | **−2.03%** | 4,558,446 | **−2.03%** |
| 1 | 8,652,032.63083 | 8,652,032.63083 | 0.00% | 8,652,032.63083 | 0.00% |
| 2 | 51,351,665.57 | 51,351,665.57 | 0.00% | 51,351,665.57 | 0.00% |
| 3 | 38,049,010.3264 | 38,049,010.3264 | 0.00% | 38,049,010.3264 | 0.00% |
| 4 | 64,706,340.5273 | 64,706,340.5273 | 0.00% | 64,706,340.5273 | 0.00% |

**bstar**:

| case | @0 | @15 | Δ%@15 | @45 | Δ%@45 |
|---|---:|---:|---:|---:|---:|
| 0 | 5,049,567.79 | 4,726,717.00 | **−6.39%** | 4,654,000.63 | **−7.83%** |
| 1 | 11,953,948.6719 | 10,383,726.4661 | **−13.14%** | 9,417,669.97058 | **−21.21%** |
| 2 | 53,339,889.3626 | 53,339,889.3626 | 0.00% | 53,339,889.3626 | 0.00% |
| 3 | 35,248,942.0816 | 35,248,942.0816 | 0.00% | 35,248,942.0816 | 0.00% |
| 4 | 66,640,348.2904 | 66,640,348.2904 | 0.00% | 66,640,348.2904 | 0.00% |

**Zero regressions across all 30 cells.** The brief predicted "at 15/45s ... phase-2 never fires and
cells equal budget-0" — this holds for `hybrid` on 4/5 cases and for `bstar` on 3/5 cases (their
phase-1 cost alone already exceeds or nearly exhausts 15/45s), but it does **not** hold for `hybrid`
case0 or `bstar` cases 0/1: those cases' phase-1 cost is cheap enough (`bstar` case0 phase-1 ≈0.83s,
case1 ≈11.23s — see §6) that phase 2 gets real room even at 15s, and wins outright (`bstar` case1
@45s: **−21.21%**, the single biggest cell in the whole sweep). This is a **better-than-predicted**
outcome, not a gate violation, since the gate is one-directional; documented honestly rather than
silently matching the brief's own prediction where it happened to be wrong.

### 3.5 Suite battery — 19/19 green

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
PASS test/run_b1_regressions.sh
PASS test/run_b3_regressions.sh
PASS test/run_b4_regressions.sh
PASS test/run_b5_regressions.sh
PASS test/run_b6_regressions.sh
PASS test/run_b7_regressions.sh
PASS test/run_selfconsistency_regressions.sh (solver == golden re-scorer to <=1e-6 rel, 0 open routes)
PASS test/run_b2_regressions.sh (10 new tests)

=== 19/19 passed ===
```

Ran in foreground chunks (machine was under heavy, *unpredictable* contention this session — one
suite, `run_b4_regressions.sh`, took 1238s vs its ~59s historical figure at one point, while others
in the same battery ran near-normal speed; re-confirmed clean afterward). Build warning-clean
(`-Wall -Wextra -pedantic`, zero warnings/errors) on every rebuild, including the final one after
reverting the temporary trace.

## 4. Unit tests (10, `test/regression_b2_20260725.cpp`)

- **`shouldRunPhase2Leg` boundary** (`testShouldRunPhase2LegMarginBoundary`): `<` vs `>=` at exactly
  `1.5×`, negative remaining, zero-cost leg always fits.
- **`PortfolioFold::admit` synthetic folds** (the brief's "2-leg synthetic" ask):
  `testFirstLegAlwaysImprovesEvenIfIllegal` (empty fold has no incumbent — even an illegal first leg
  is admitted, explaining why phase 1's leg 0 always triggers the first anytime write with no
  special-casing), `testSecondLegWorseDoesNotImproveWriteCountMatchesImprovements` (1 write for 1
  improvement out of 2 legs), `testSecondLegBetterImprovesWriteCountMatchesImprovements` (2 writes
  for 2 improvements, `picked_seed` tracks the last *improving* leg's seed, not the last admitted
  one), `testLegalityBeatsLowerValue` (fewer `open_routes` wins outright regardless of value,
  preserving `paydownBetter`'s pre-existing rule).
- **Real `writeCfg` path** (`testAdmitWritesViaRealWriteCfgPath`): the `on_improved` callback calls
  the actual `io.cpp` `writeCfg` (not a mock) against a tiny hand-built Problem/Placement/
  RoutingResult, asserting well-formed output and byte-match against an independent second write of
  the same triple.
- **Registry sanity** (`testDefaultVariantsFloorLegsUnchanged`): still 8 legs, indices 0/1 are the
  two floor legs the pruning policy (§6) names as never-droppable.
- **Real-case coverage on case0** (7 blocks, the same fast fixture `test/regression_b6_20260724.cpp`
  already validated for full `runPricingPortfolio` calls): `testBudgetOffIsPhase1Only`
  (`legs_run==8`, `budget_phase2==false`, `picked_seed==options.seed`);
  `testBudgetOnNeverRegressesBudgetOffAndFiresSeedSweep` (20s budget fires the sweep, `legs_run>8`,
  final value monotonically `<=` the budget-off value, `picked_seed` is never `options.seed+1` — the
  offset reserved for hybrid's own internal bstar leg); `testAnytimeWriteFinalFileMatchesFinalBestOnRealCase`
  (the brief's explicit "verify the final file equals the final best (byte compare) on one real
  case" ask — 15s budget, every intermediate write captured, last one byte-compared against a fresh
  `writeCfg` of the actually-returned final result).

Kill-safety itself (an external process kill mid-run) is not portably testable in a suite, per the
brief's own acknowledgment — the write-on-improvement mechanism and the final-file-matches-final-
best property are what is tested instead, plus §3.2's live mtime-progression observation.

## 5. β_ft sensitivity flip table (analysis only — no code default change)

**Method**: `checker_cost` (ft0) and `score.value` (aligned) satisfy `score.value = checker_cost +
kBetaFt × mismatch` with `kBetaFt = 3.0` (`route_cost.hpp`) — verified algebraically in the code and
empirically (every `β=0` value computed below matches `b7-report.md §5`'s own "ft0 measured" column
exactly, e.g. case4 `64,135,564.58` here vs `64,135,564.577` there). So for any hypothetical `β'`,
`hypothetical_value(β') = value_at_3 + (β' − 3.0) × mismatch` — no re-runs needed, exactly as the
brief specifies. Per-leg `(open_routes, value_at_3, mismatch)` for all 8 legs × 5 default cases was
captured via a temporary `stderr` trace (added and reverted in this session — not part of the
committed diff, see §3.1's 3rd byte-identity re-verification, taken *after* the revert). Winner =
lexicographically-least `(open_routes, hypothetical_value)`, matching `paydownBetter`'s own rule; all
80 legs here have `open_routes=0`, so it reduces to plain value comparison.

| case | β=0 (ft0) | β=3 (current, aligned) | β=10 | β=30 | flips in [0,30]? |
|---|---|---|---|---|---|
| 0 | leg4 = 4,624,846.00 | leg4 = 4,653,040.63 | leg4 = 4,718,828.10 | **leg6** = 4,835,581.00 | **yes**, leg4→leg6 near **β≈22.5** |
| 1 | leg6 = 8,437,960.88 | leg6 = 8,652,032.63 | leg6 = 9,151,533.38 | leg6 = 10,578,678.38 | no (leg6 dominant throughout) |
| 2 | leg6 = 50,069,327.60 | leg6 = 51,351,665.57 | leg6 = 54,343,787.50 | **leg1** = 59,665,318.37 | **yes**, leg6→leg1 near **β≈22.4** |
| 3 | leg1 = 36,615,637.24 | leg1 = 38,049,010.33 | **leg0** = 40,695,587.84 | leg0 = 44,138,214.24 | **yes**, leg1→leg0 near **β≈7.8** |
| 4 | leg6 = 64,135,564.58 | leg6 = 64,706,340.53 | leg6 = 66,038,151.08 | leg6 = 69,843,324.08 | no (leg6 dominant throughout) |

(leg indices are `defaultVariants()` registry order: leg0=(official,v1,off,off), leg1=(legacy,v1,off,
off) [the floor pair], leg2=(official,v2,off,off), leg3=(legacy,v2,off,off),
leg4=(official,v2,credit-on,off), leg5=(official,v1,credit-on,off), leg6=(official,v2,credit-on,
moves-on), leg7=(official,v1,credit-on,moves-on).)

**Reading**: at the actual `β_ft=3.0`, every flip-table winner matches the real solver's reported
winner exactly (cross-validated). 3 of 5 cases (0, 2, 3) are NOT robust to a large enough β_ft
increase, but only case3 flips anywhere near the current operating point (**β≈7.8**, roughly 2.6×
today's value) — cases 0/2 don't flip until **β≈22-23**, roughly 7.5× today's value. case3's flip is
towards `leg0` (official pricing, v1-admission, credit-off), i.e. AWAY from the credit/moves
machinery entirely — a reminder that the abutment-credit and move-portfolio wins (B5b/B5c) are
themselves contingent on the current β_ft; if the official β_ft were ever revised upward materially,
case3 would be the first case to want a different leg. Cases 1 and 4 are fully stable across the
entire tested range. This is exploratory input for the submission decision, per the brief — no
default changed.

## 6. Runtime projection & pruning policy (documented, not enacted)

**Per-leg wall time per case** (phase 1, hybrid, from the same trace as §5; sums match the traced
run's own measured total, 278.850s, to within 0.08s):

| case | leg0 | leg1 | leg2 | leg3 | leg4 | leg5 | leg6 | leg7 | **case total** |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 0 | 0.334 | 0.229 | 0.363 | 0.234 | 0.351 | 0.292 | 0.318 | 0.360 | **2.481s** |
| 1 | 4.538 | 3.865 | 4.708 | 4.435 | 4.104 | 3.565 | 4.396 | 3.637 | **33.246s** |
| 2 | 6.653 | 5.966 | 7.142 | 6.941 | 7.087 | 6.827 | 8.459 | 8.209 | **57.284s** |
| 3 | 8.373 | 7.387 | 9.082 | 7.966 | 8.510 | 7.754 | 9.760 | 8.894 | **67.725s** |
| 4 | 13.934 | 9.918 | 14.030 | 13.194 | 14.452 | 13.830 | 18.970 | 19.708 | **118.035s** |

**Projection**: this session's own clean (no-trace) 5-case default measurements were **268.621s,
272.802s, 270.653s** (avg **≈270.7s**) — the task brief's own reference point of ~339s is a stale
cross-session figure (per this task's briefing note); machine load clearly varies run-to-run in this
environment (§3.5's 21× `run_b4_regressions.sh` outlier is the extreme case), consistent with
`b7-report.md §4`'s own finding. Projecting an 8-case total by scaling with **case4's cost as the
worst-case proxy** (per the brief): `270.7s (5 known cases) + 3 × 118.035s (3 hypothetical
case4-equivalent cases) ≈ 624.8s ≈ 10.4 minutes`. Against the contest's own **"8 cases / 2h TOTAL"**
aggregate budget (7200s), this is **~11.5× headroom** even under this deliberately conservative
worst-case scaling (real unknown cases are more likely to resemble the *median* of the 5 known cases,
not uniformly the most expensive one). This matches the repo's own prior "45×-300× unused budget"
framing (`docs/research/2026-07-07-direction-review.md`) and confirms the default (budget-off)
submission path has ample margin without needing any pruning.

**Pruning policy (documented, NOT enacted today, per the brief)**: if a future testcase drop ever
made the projection stop fitting the aggregate budget, the rule is: drop non-floor legs, ranked by
"never won AND never within 2% of that case's own winner" (computed at the current β_ft=3.0
operating point across the known default cases), until the projection fits. The two **floor legs**
— `leg0=(official,v1,credit-off,moves-off)` and `leg1=(legacy,v1,credit-off,moves-off)`, the original
B4.1/B5a.1 floor pair — are **never** dropped regardless of this ranking.

Applying the rule to the current 5-case sample (non-floor legs only, `%` = distance above that
case's own winner):

| leg | won any case? | closest miss (case, %) | verdict |
|---|---|---|---|
| leg2 (official,v2,off,off) | no | case4, 9.61% | **pruning candidate** |
| leg3 (legacy,v2,off,off) | no | case2, 18.38% | **pruning candidate** |
| leg4 (official,v2,credit-on,off) | **yes** (case0) | — | protected |
| leg5 (official,v1,credit-on,off) | no | case1, 2.65% (just over the 2% line) | **pruning candidate** |
| leg6 (official,v2,credit-on,moves-on) | **yes** (case1, 2, 4 — the most valuable leg) | — | protected |
| leg7 (official,v1,credit-on,moves-on) | no | case1, 0.00% (exact tie with leg6!); case2, 0.67% | protected (within 2% twice) |

So, if pruning were ever triggered, `leg2`/`leg3`/`leg5` are the current drop candidates, leaving a
5-leg registry (`leg0, leg1, leg4, leg6, leg7`) that still covers every default case's actual winner
and its near-misses. Not acted on — the runtime headroom above makes this unnecessary today, and the
brief explicitly scopes this item as "documented, not enacted."

## 7. Deviations / honest findings

- **`kPhase2Floor` removed, not just replaced**: the pre-B2 code's explicit 2-second global floor on
  "is there enough budget for phase 2 to be worth starting" is gone; `shouldRunPhase2Leg`'s per-leg
  check subsumes it exactly (round 1 simply admits zero legs when nothing clears the margin, ending
  the sweep immediately — the same observable outcome, one fewer named constant).
- **The sweep gate's "phase-2 never fires at 15/45s" prediction was wrong for `bstar`** (§3.4) — not
  a defect, but worth flagging: the brief's own expectation was calibrated to `hybrid`'s ~54s/case
  average phase-1 cost, not `bstar`'s much cheaper cost (`bstar` case0 phase-1 ≈0.83s, case1
  ≈11.23s). The gate (`≤ budget-0 + 1%`) holds regardless, since it is one-directional, but the
  *reported* cell values differ materially from what the brief predicted — surfaced plainly rather
  than smoothed into "as expected."
- **case1 explores extensively without winning, at both 420s and 600s** (98 and 149 legs
  respectively) — an honest non-result, not hidden: this case's phase-1 basin is evidently a strong
  local optimum for this registry/engine combination that seed diversity alone does not beat within
  these budgets. Worth noting for any future seed-sweep tuning discussion, though out of this task's
  own scope to chase further.
- **Machine load was severely non-uniform this session** (§3.5): one suite ran 21× its historical
  time while others ran near-normal or even faster than historical figures in the very same battery.
  All runtime figures in this report are this session's own in-session measurements, explicitly not
  compared against stale cross-session baselines where that would be misleading (matching
  `b7-report.md §4`'s own precedent for handling this kind of drift).
- **No architectural changes beyond spec**: `PlacementEngine`'s interface, `runPlacementSearch`'s SA
  loop, the elite loop, `outlineSweep`, `finalizeFeedthrough`, `respaceFixedPoint`,
  `capacityGapCandidates`/`applyGapCandidate` (B7) are all untouched. `eval.cpp`, `routing.cpp`,
  `placement_engine.cpp`, `respace.cpp`, `validate.cpp`, `cfg_score.cpp` are read-only this task.

## 8. Files changed

- `src/search.hpp` — `SearchResult.picked_seed`/`legs_run`, `AnytimeWriteFn`, `PortfolioFold`,
  `shouldRunPhase2Leg`, `runPricingPortfolio`'s new 4th parameter + rewritten doc comments.
- `src/search.cpp` — `shouldRunPhase2Leg`/`PortfolioFold::admit` implementations, `runPricingPortfolio`
  rebuilt (per-leg timing, streaming fold, seed-sweep phase 2).
- `src/io.cpp` — `writeCfg` now atomic (temp-write + rename), `#include <cstdio>`.
- `src/main.cpp` — anytime-write callback wiring, `seed=`/`legs_run=` stderr fields.
- `.gitignore` — `test/b2_regression_bin`.
- `test/regression_b2_20260725.cpp` (new), `test/run_b2_regressions.sh` (new).
- `.superpowers/sdd/b2-report.md` — this file.
- `.superpowers/sdd/progress.md` — ledger line appended.

No changes to `src/eval.cpp`, `src/routing.cpp`, `src/placement_engine.cpp`, `src/respace.cpp`,
`src/validate.cpp`, `src/cfg_score.cpp`, `src/model.hpp`.

## 9. Commit

One commit, staging: `src/search.hpp`, `src/search.cpp`, `src/io.cpp`, `src/main.cpp`, `.gitignore`,
`test/regression_b2_20260725.cpp`, `test/run_b2_regressions.sh`, `.superpowers/sdd/b2-report.md`,
`.superpowers/sdd/progress.md`; message: `feat(search): seed-sweep budget phase, anytime output,
submission hardening (B2)` + `Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>`.
