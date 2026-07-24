# B5a / B5a.1 / B5a.2 Report — Elite-pool rework, its regression, the flag-gated variant-portfolio
# fix, and the structural budget-gate closure

Status: **B5a.1 DONE** (flag-gated variant portfolio, §1–§9, historical record, kept as-is below);
**B5a.2 DONE, budget gate now CLOSED** (§10) — `runPricingPortfolio` rebuilt as a variant registry
with a default-first, two-phase budget mode, closing the gate **structurally**: every budgeted run
folds in the full deterministic default run (phase 1) alongside a wall-clock-bounded continuation
(phase 2), so the result can never be worse than the default under the metric the fold actually
optimizes (`score.value`). Re-measured under that aligned metric, 30/30 sweep cells pass (§10.6);
under the golden rescorer's `checker_cost` alone, 29/30 pass, with one cell (`bstar case3@45`,
+1.0785%) disclosed rather than hidden (§10.4/§10.7) — see §10.5 for the metric-alignment ruling
that resolves this in favor of `score.value` as the gate metric. Byte-identity (item 1) and all 17
suites (item 4) re-verified green against the B5a.2 code.

---

## 1. What happened: B5a's regression (the reason B5a.1 exists)

B5a (`.superpowers/sdd/b5-design.md` §2) was implemented exactly as specified: `kTopCheapCandidates`
16→48, an exact-value archive (K_arch=8), an insurance reservoir (R_ins=4, dedicated
`mt19937(seed^0x5eedu)`), and `buildEliteFullSet` (rung-0 tight-exact re-rank, legality-first, forced
running-best + insurance). Unit-tested directly (7 tests, all green) and confirmed to implement the
spec's own described contract precisely.

Wired in **unconditionally**, the end-to-end default-flag (`bin/efgr`, i.e. `runPricingPortfolio`)
result was a **STOP-worthy regression**:

| case | current pin | B5a (unconditional v2) | Δ% |
|---|---:|---:|---:|
| 0 | 5,233,375.00 | 5,233,375.000 | 0.00% |
| 1 | 9,100,697.98 | 10,656,490.498 | **+17.10%** |
| 2 | 56,045,213.30 | 56,007,181.850 | −0.07% |
| 3 | 36,615,637.24 | 48,549,702.543 | **+32.59%** |
| 4 | 76,369,838.93 | 66,198,069.443 | −13.32% |

Two pre-existing hard-pinned regression suites caught this independently: `run_b1_regressions.sh`
(`testHybridDefaultFlagsNeverExceedPreB1Baseline`, case3 `result.best_score.value` exceeding the
pre-B1 baseline) and `run_b4_regressions.sh` (`testPricingPortfolioRestoresCase3PreB4Floor`, case3
`checker_cost` exceeding the pre-B4 floor of 36,615,637.236).

**Root cause, confirmed by direct probe** (not guesswork): B5a's admission rework lives in shared
`search.cpp` code that **both** pricing legs of `runPricingPortfolio` (and all three hybrid sub-legs)
run through unconditionally. This breaks B4.1's own premise — "the legacy leg reproduces pre-B4
(HEAD 207d08c) behavior BIT-EXACTLY" — because that bit-exactness required *nothing* in `search.cpp`'s
elite-admission path to change, a premise B5a necessarily violates. Isolating case3's legacy-pricing
leg alone (`route_pricing_official=false`, `runHybridPlacementSearch` directly) under unconditional
B5a gave checker_cost ≈ **52,288,000** — worse than *both* its old bit-exact-replay floor (36.6M)
*and* the official leg's own (also-regressed) 48.5M. Both legs of the portfolio had degraded; there
was no floor left. Case1's regression was a same-leg (official, both before/after) cascading
trajectory shift through the hybrid sp→bstar_a/bstar_b seed chain — a member of the same risk family
`b5-design.md` §9 risk **R7** named explicitly: *"B5a cascades into hybrid's bstar-leg seed →
cross-case trajectory shift ... caught by the per-case ≤ current gate (empirical, not assumed)."* The
gate did exactly what R7 said it would.

This was reported back as a STOP (not fixed in place), matching precedent (B1 Part 1, B4's original
case3 stop) and this agent's own mandate to hand judgment calls back rather than guess.

## 2. The coordinator's fix: B5a.1 — elite admission v2 as a portfolio dimension

Adjudication: convert B5a into a **flag-gated portfolio variant**, the same pattern B4.1 already
established for router pricing, one dimension richer.

### 2.1 `SearchOptions.elite_admission_v2` (default `false`)

`src/search.hpp:44` (doc comment) / used throughout `src/search.cpp`. `false` (v1) reproduces
pre-B5a/HEAD 408a297 **exactly**; `true` (v2) is B5a's own tight-exact-reranked admission.

### 2.2 `runPlacementSearch` — v1/v2 branches (`src/search.cpp`)

- `:607` `const std::size_t kTopCheapCandidates = options.elite_admission_v2 ? 48 : 16;` (was an
  unconditional `constexpr ... = 48`) — the cheap-pool cap is now a runtime choice.
- `:620-628` `rememberExactArchive`: `if (!options.elite_admission_v2) return;` at its top — v1
  performs **no archive maintenance at all** (the vector stays permanently empty).
- `:640-649` insurance: `ins_rng` is now `std::optional<std::mt19937>`, constructed **only** when
  `options.elite_admission_v2` (`:645`) — v1 never even allocates the generator, let alone draws from
  it (`rememberInsurance` also early-returns at `:647`).
- `:770-834` (approx) the elite-loop admission itself is now an explicit `if (!options.elite_admission_v2) { ... } else { ... }`
  (`:774` is the `if`): the **v1 branch is the pre-B5a loop verbatim**, iterating `top_candidates`
  directly; the **v2 branch** builds `full_set` via `buildEliteFullSet` exactly as B5a did. Both
  branches share an textually-identical loop body (T7 sweep raw + compacted + `maybeAdoptRespace`) —
  duplicated rather than unified, deliberately, so the v1 branch is byte-for-byte auditable against
  pre-B5a history.
- The B5a free functions (`rememberBounded`, `reservoirSample`, `buildEliteFullSet`,
  `CheapCandidate`, `EliteCandidate`) are **unchanged** — only called conditionally now.

### 2.3 `runPricingPortfolio` — 4-leg variant portfolio (`src/search.cpp:989`)

Cross product `{route_pricing_official, elite_admission_v2}`, leg order **(official,v1), (legacy,v1)**
(the floor pair) **then (official,v2), (legacy,v2)**, folded pairwise by the existing value-gated
`paydownBetter` (legality-first, then value — a transitive total preorder, so fold order doesn't
change the winner). `time_budget_sec`, if set, now splits **/4** (was /2); budget-off (0.0) divides to
0.0 regardless. `SearchResult.pricing_official`/`.elite_admission_v2` are stamped fresh from this
function's own local fold bookkeeping (never read from any individual leg's own copy of those
fields) — the exact same precedent `pricing_official` alone already established in B4.1.

### 2.4 Telemetry (`src/main.cpp`)

stderr summary gained `admission=v1|v2` immediately after `pricing=`.

## 3. Byte-identity gate (item 1 — HARD, verified before proceeding)

Built with `elite_admission_v2` defaulted false; a throwaway probe reproduced the **old 2-leg**
(official,legacy) fold using *only* the v1 code path (`runHybridPlacementSearch` directly, no
`buildEliteFullSet` involved at all) and md5-compared the 5 resulting `.cfg` files against the
pre-B5a hashes recorded in `b6-report.md`:

```
case0: MATCH (8814c8771d96eb0d63ffe8ca797c2c43)
case1: MATCH (bb738dc520417e0a60412dab0a2687b5)
case2: MATCH (8d82ef67153147ec3a8a39426dcea9c3)
case3: MATCH (3388a0a277109fd7fb28469e22e4b8b7)
case4: MATCH (30c60ab3097c625bbf06019d84727c62)
=== RESULT: ALL 5 CASES BYTE-IDENTICAL (v1-only vs pre-B5a) ===
```

**All 5 MATCH.** The v1 path is provably byte-identical to pre-B5a/HEAD 408a297. Gate passes.

## 4. Per-case default-flag acceptance (item 3 — HARD)

`bin/efgr` (i.e. the real 4-leg `runPricingPortfolio`), default flags, golden-rescored:

| case | pin (current, hard ceiling) | measured (post-B5a.1) | Δ vs pin | winning leg |
|---|---:|---:|---:|---|
| 0 | 5,233,375.00 | 5,233,375.000 | 0.00% | (official, v1) |
| 1 | 9,100,697.98 | 9,100,697.980 | 0.00% | (official, v1) |
| 2 | 56,045,213.30 | 56,007,181.850 | **−0.068%** | (official, **v2**) |
| 3 | 36,615,637.24 | 36,615,637.236 | 0.00% | (legacy, v1) |
| 4 | 76,369,838.93 | 66,198,069.443 | **−13.32%** | (official, **v2**) |
| **Σ** | **183,364,762.45** | **173,154,961.51** | **−5.57%** | |

**Every case ≤ its pin — hard gate PASSES.** Two cases (2, 4) strictly improve via the new v2
admission winning the fold on merit; the other three land exactly on their pre-B5a floor via the
v1-legs (case3 specifically via **(legacy, v1)** — the exact leg/admission pair that reproduces the
pre-B4/pre-B5a trajectory bit-for-bit, restoring the floor B5a alone had broken). This matches the
coordinator's own prediction almost exactly (predicted case2≈56.01M/case4≈66.20M/Σ≈173.2M; measured
56.007M/66.198M/173.155M).

Per-case solver telemetry (pool diagnostics, like b6-report's table — now split by which admission
scheme won each leg):

| case | leg (pricing, admission) | tau | pool_tau | exact_best_cheap_rank/K |
|---|---|---:|---:|---|
| 0 | official, v1 | 0.067 | 0.828 | 1/16 |
| 1 | official, v1 | 0.689 | −0.433 | 9/16 |
| 2 | official, v2 | 0.689 | −0.294 | 39/48 |
| 3 | legacy, v1 | 0.600 | −0.067 | 16/16 |
| 4 | official, v2 | 0.200 | −0.475 | 45/48 |

Field-semantics note (documented in code too, `search.hpp`'s `SearchResult.elite_pairs` doc comment
and the B6-snapshot-loop comment in `search.cpp`): `pool_tau`/`exact_best_cheap_rank` now report over
**K=16 when the winning leg is v1, K=48 when it is v2** — the pool size is whichever admission scheme
the *picked* leg actually used, not a fixed constant. Case3's exact_best_cheap_rank=16/16 (dead last)
is unchanged from B6's own original finding — expected, since case3's winning leg is v1 (the pre-B5a
mechanism B6 measured in the first place).

## 5. Suite battery (item 4 — HARD, all 17 must pass)

```
PASS test/run_case00.sh (13s)
PASS test/run_placement_engine_regressions.sh (23s)
PASS test/run_20260618_regressions.sh (20s)
PASS test/run_20260608_regressions.sh (18s)
PASS test/run_cfg_score_regressions.sh (11s)
PASS test/run_selfconsistency_regressions.sh (144s)
PASS test/run_integer_output_regressions.sh (31s)
PASS test/run_testdata_regressions.sh (10s)
PASS test/run_p0_regressions.sh (11s)
PASS test/run_p1_regressions.sh (30s)
PASS test/run_p2_regressions.sh (11s)
PASS test/run_p3_regressions.sh (13s)
PASS test/run_b1_regressions.sh (48s)
PASS test/run_b3_regressions.sh (30s)
PASS test/run_b4_regressions.sh (67s)
PASS test/run_b6_regressions.sh (13s)
PASS test/run_b5_regressions.sh (54s)

=== SUMMARY: 17 passed, 0 failed (of 17) ===
```

`run_b1_regressions.sh`/`run_b4_regressions.sh` (the two suites B5a's unconditional wiring had broken)
now pass again: their pinned floors hold via the (official,v1)/(legacy,v1) pair reproducing exactly
what those tests expect.

`test/run_b5_regressions.sh` gained an 8th test, **`testPortfolioRestoresCase3FloorPostB5a1`**
(portfolio-level pin, mirrors `regression_b4_20260724.cpp`'s own `testPricingPortfolioRestoresCase3PreB4Floor`
pattern but against the new 4-leg `runPricingPortfolio`): asserts case3's default-path checker_cost ≤
the pre-B4 floor (36,615,637.236) via the real 4-leg fold — confirmed passing
(`checker_cost=3.66156e+07 <= floor 3.66156e+07, pricing_official=0, elite_admission_v2=0`, i.e.
legacy+v1, exactly as predicted). The 6 pre-existing B5a unit tests (`buildEliteFullSet` etc.) are
unchanged — they test the free functions directly, independent of the new flag-gating in
`runPlacementSearch`.

Build: warning-clean (`-Wall -Wextra -pedantic`, verified via `make clean && make`, zero
warnings/errors).

## 6. Budget sweep matrix (item 5) — gate does NOT pass; reporting only, no backstop

`ENGINES="bstar hybrid" BUDGETS="0 15 45" SEEDS="1" CASES="0 1 2 3 4" REF_ITERS=1000
bash scripts/sweep.sh`, golden-rescored:

| engine | case | @0 (base) | @15 | Δ%@15 | @45 | Δ%@45 |
|---|---|---:|---:|---:|---:|---:|
| bstar | 0 | 5,202,230 | 5,602,165 | **+7.688%** | 5,602,165 | **+7.688%** |
| bstar | 1 | 12,509,317 | 10,021,093 | −19.891% | 10,021,093 | −19.891% |
| bstar | 2 | 52,962,158 | 49,217,166 | −7.071% | 49,217,166 | −7.071% |
| bstar | 3 | 52,353,205 | 52,353,205 | 0.000% | 52,917,851 | **+1.079%** |
| bstar | 4 | 71,490,185 | 67,109,932 | −6.127% | 66,949,648 | −6.351% |
| hybrid | 0 | 5,233,375 | 4,860,186 | −7.131% | 4,860,186 | −7.131% |
| hybrid | 1 | 9,100,698 | 9,386,187 | **+3.137%** | 9,979,192 | **+9.653%** |
| hybrid | 2 | 56,007,182 | 54,359,329 | −2.942% | 51,843,974 | −7.433% |
| hybrid | 3 | 36,615,637 | 39,346,074 | **+7.457%** | 39,346,074 | **+7.457%** |
| hybrid | 4 | 66,198,069 | 66,198,069 | 0.000% | 68,418,787 | **+3.355%** |

All 30 cells: `open_routes=0`, `validation=ok`.

**Gate verdict: DOES NOT PASS.** 8 of 20 non-baseline cells exceed +1%: bstar-case0@{15,45}
(+7.69% each), bstar-case3@45 (+1.08%, barely), hybrid-case1@{15,45} (+3.14%/+9.65%),
hybrid-case3@{15,45} (+7.46% each), hybrid-case4@45 (+3.36%).

**But this is a materially better shape than before B5a.1, not a wash:**

| cell | pre-B5a (b1-report.md sweep) | post-B5a.1 | change |
|---|---|---|---|
| bstar case0 @15/@45 | +7.69% (both) | +7.69% (both) | **unchanged** — see note below |
| hybrid case2 @15/@45 | +1.68% / −4.3% | −2.94% / −7.43% | **FIXED** (both now pass) |
| hybrid case3 @15/@45 | +39.58% / +21.13% (worst cell) | +7.46% / +7.46% | **hugely improved** (still fails, but 1/3 to 1/5 the magnitude, and now *stable* instead of worsening) |

New cells that were passing before and now (marginally) fail: bstar case3 @45 (+1.08%, right at the
boundary), hybrid case1 @15/@45 (+3.14%/+9.65%, and — unlike case3 — this one gets *worse* with more
budget, the same worsening-with-budget pattern the original diagnosis flagged as the concerning
signature), hybrid case4 @45 (+3.36%).

**Honest read:** B5a.1's floor legs (v1) structurally guarantee the *default-flags* per-case gate
(§4, confirmed) — that gate does not depend on the sweep passing. The sweep gate is a *stronger*,
budget-dimension property that B5a.1 was not specifically designed to close (the coordinator's own
framing: "if the gate fails, report the matrix and STOP there — backstop decision remains mine").
Per that instruction: **not attempting the §5 structural-snapshot backstop, and not tuning constants
or re-ranking heuristics to chase the remaining 8 cells** — reporting the matrix as measured and
stopping at this decision point, as instructed.

**Runtime:** 5-case default total = **136.760s** (well under the 180s bound; close to the
coordinator's own ~135-140s estimate). Sweep matrix total wall time ≈ 15.5 min (30 cells; budget-0
cells run full `--iters 1000` per leg × 4 legs, budget-15/45 cells are wall-clock-bounded per the
`--time-budget` split so scale with the budget value, not case difficulty).

## 7. Files changed

- `src/search.hpp` — `SearchOptions.elite_admission_v2` (`:44` area), `SearchResult.elite_admission_v2`
  (`:78` area, next to `pricing_official`), `runPricingPortfolio`'s doc comment rewritten for the
  4-leg contract. B5a's own hoisted declarations (`CheapCandidate`, `EliteCandidate`, `rememberBounded`,
  `reservoirSample`, `buildEliteFullSet`) unchanged.
- `src/search.cpp` — `runPlacementSearch`: `kTopCheapCandidates` runtime-conditional (`:607`),
  `rememberExactArchive`/insurance-reservoir construction gated behind the flag (`:620-649`,
  `ins_rng` now `std::optional<std::mt19937>`), elite admission loop split into v1 (verbatim
  pre-B5a)/v2 (`buildEliteFullSet`) branches (`:774` onward). `runPricingPortfolio` rewritten to the
  4-leg fold (`:989`). B5a's free functions (`rememberBounded`, `reservoirSample`,
  `buildEliteFullSet`) unchanged, called conditionally now.
- `src/main.cpp` — stderr summary gained `admission=v1|v2`.
- `src/rank_stats.hpp` — B5a's `<=48 elite-pool entries` comment fix, unchanged by B5a.1.
- `.gitignore` — `test/b5_regression_bin` (from B5a, unchanged by B5a.1).
- `test/regression_b5_20260724.cpp` — B5a's 7 unit tests (unchanged) + new
  `testPortfolioRestoresCase3FloorPostB5a1` (8th test, portfolio-level floor pin).
- `test/run_b5_regressions.sh` — gained `src/cfg_score.cpp` link (needed for the new test's golden
  rescoring).
- `.superpowers/sdd/b5-design.md` — untracked design doc, staged with this commit per instruction.
- `.superpowers/sdd/b5a-report.md` — this file.
- `.superpowers/sdd/progress.md` — ledger line appended.

## 8. Deviations / honest findings

- **Sweep gate does not pass** (§6) — reported prominently per instruction, not silently buried.
  Explicitly did NOT build the §5 structural-snapshot backstop, tune `K_full`/`K_arch`/`R_ins`, or
  change the ranking heuristic to chase the remaining 8 cells — all reserved as the coordinator's
  decision.
- **bstar case0 @15/@45 is completely unchanged by B5a.1** (+7.69% both before and after) — this cell
  is a pure single-engine (no hybrid sub-leg cascade) SA-trajectory/basin-cooling effect on a trivial
  R=0 (pure area/WL) case; B5a.1's admission-portfolio dimension does not touch it because bstar's
  own 4-leg fold apparently converges on the same admission choice regardless of budget, and the
  degradation enters through the SA loop's own longer-budget trajectory, not elite-pool composition.
  Flagging honestly rather than claiming credit B5a.1 doesn't have here.
- **Two cells regressed past +1% that were NOT in the original (pre-B5a) failing list**: bstar
  case3@45 (+1.08%, essentially at the threshold — could plausibly be seed-level noise given the
  margin) and hybrid case1@{15,45} (+3.14%/+9.65%, and notably *worsens* with more budget the same way
  the original worst offenders did). Neither violates the item-3 default-flags hard gate (case1's
  default-flags result, §4, sits exactly on its pin via the v1 floor leg) — these are budget-dimension
  findings only, surfaced for the coordinator's next decision.
- No architectural changes beyond the coordinator's own spec: B5a's free functions
  (`rememberBounded`/`reservoirSample`/`buildEliteFullSet`) untouched; `eval.cpp`, `routing.cpp`,
  `respace.cpp`, `cfg_score.cpp` untouched.

## 9. Commit

One commit, staging exactly: `src/search.cpp`, `src/search.hpp`, `src/main.cpp`, `src/rank_stats.hpp`,
`.gitignore`, `test/regression_b5_20260724.cpp`, `test/run_b5_regressions.sh`,
`.superpowers/sdd/b5-design.md` (untracked, staged per instruction), `.superpowers/sdd/b5a-report.md`,
`.superpowers/sdd/progress.md`; message:
`feat(search): elite admission v2 as a 4-leg variant-portfolio dimension (B5a)` +
`Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>`.

This commit landed as `70d8f38`. Everything below is new (B5a.2, same commit boundary the
coordinator asked to close with).

---

## 10. B5a.2 addendum — variant-registry portfolio, default-first budget mode, gate closure

Adjudication that authorized this addendum (§6's gate report was accepted, then acted on): close the
budget gate **structurally** — make every budgeted run a strict superset of the default run — and
generalize the hardcoded 4-leg fold into a variant registry while in there, since B2's future
seed-variant dimension will want the same shape. One commit. Byte-identity for the budget-off path
is a hard, re-verified gate (§10.2); so is the full 17-suite battery (§10.3).

### 10.1 What changed

**10.1.1 Registry refactor** (`src/search.hpp`, `src/search.cpp`) — B5a.1's hardcoded 4 local
variables (`(official,v1)`, `(legacy,v1)`, `(official,v2)`, `(legacy,v2)`) are now data:

```cpp
struct SearchVariant {
    bool pricing_official;
    bool elite_admission_v2;
};

std::vector<SearchVariant> defaultVariants();  // same 4 legs, same order, as data
```

`defaultVariants()` returns the identical cross product in the identical order B5a.1 used — this
half of the refactor is a pure extraction, no behavior change (confirmed by the byte-identity gate,
§10.2, and by every per-case pin in §4 still holding unchanged since no leg's own semantics moved).

**10.1.2 Default-first, two-phase budget mode** (`runPricingPortfolio`, `src/search.cpp`) — the
part that actually closes the gate:

- **Phase 1**: run the *entire* registry (all of `defaultVariants()`) at `options.iters`, with
  `time_budget_sec` forced to `0.0` regardless of what the caller asked for. This is, by
  construction, *exactly* the default/budget-off computation — same code path, same iters, same
  seed. Its wall-clock cost is measured (`std::chrono::steady_clock`) as `t0`.
- **Phase 2** (conditional): only if `remaining = options.time_budget_sec - t0` clears a small floor
  (`kPhase2Floor = 2.0s`), re-run the *entire* registry again, this time with
  `time_budget_sec = remaining / defaultVariants().size()` per leg (the existing wall-clock-bounded
  continued-SA behavior, unchanged in itself). If the floor isn't cleared — a requested budget
  smaller than the default run's own cost — phase 2 is skipped outright and the result is phase 1
  alone.
- **Fold**: every leg that ran, from both phases, is folded together with the existing
  `paydownBetter` (legality-first, then `score.value`; a transitive total preorder, so fold order
  never changes the winner). Because phase 1's legs are *unconditionally* part of this fold, the
  portfolio's result can never be worse than the plain default run, under the metric
  `paydownBetter` optimizes (`score.value`) — this is what makes the guarantee structural rather
  than an empirical property of any particular sweep.
- Budget-off (`options.time_budget_sec == 0.0`): phase 1 runs once, `remaining` is negative, phase 2
  never fires. Single registry pass, identical cost/behavior to B5a.1.
- `SearchResult.budget_phase2` (new field, `search.hpp`) reports whether phase 2 actually executed
  — `main.cpp`'s stderr summary gained `budget_phase2=yes|no` immediately after `admission=`.

### 10.2 Byte-identity gate (item 1 — HARD, re-verified against HEAD `70d8f38`)

`git stash` the B5a.2 working tree, rebuild HEAD (`70d8f38`, the committed B5a.1 code), run all 5
default-flag cases, md5-compare, restore:

```
case0: MATCH (8814c8771d96eb0d63ffe8ca797c2c43)
case1: MATCH (bb738dc520417e0a60412dab0a2687b5)
case2: MATCH (967cb149537f7b345b03849dd22469fe)
case3: MATCH (3388a0a277109fd7fb28469e22e4b8b7)
case4: MATCH (675558297c2d6e5cb8d3f1e1c9d2c5ec)
```

**All 5 MATCH.** The budget-off path is byte-identical to `70d8f38`; the registry refactor and the
phase-1/phase-2 restructuring change nothing about what the default flags produce. (Note: these
hashes differ from §3's pre-B5a.1 hashes for case2/case4, as expected — §3 compared v1-only against
*pre-B5a*; this gate compares the real 4-leg default result, where v2 legitimately wins case2/case4,
against `70d8f38`, which already has that behavior. The relevant claim here is B5a.2 vs. B5a.1, not
B5a.2 vs. pre-B5a.)

### 10.3 Suite battery (item 4 — HARD, all 17 must pass)

```
PASS test/run_case00.sh (15s)
PASS test/run_placement_engine_regressions.sh (24s)
PASS test/run_20260618_regressions.sh (19s)
PASS test/run_20260608_regressions.sh (18s)
PASS test/run_cfg_score_regressions.sh (10s)
PASS test/run_selfconsistency_regressions.sh (138s)
PASS test/run_integer_output_regressions.sh (30s)
PASS test/run_testdata_regressions.sh (10s)
PASS test/run_p0_regressions.sh (9s)
PASS test/run_p1_regressions.sh (28s)
PASS test/run_p2_regressions.sh (10s)
PASS test/run_p3_regressions.sh (14s)
PASS test/run_b1_regressions.sh (45s)
PASS test/run_b3_regressions.sh (23s)
PASS test/run_b4_regressions.sh (59s)
PASS test/run_b6_regressions.sh (10s)
PASS test/run_b5_regressions.sh (46s)

=== SUMMARY: 17 passed, 0 failed (of 17) ===
```

This is the commit-time re-run (run immediately before staging, against the exact working tree being
committed). `test/run_b5_regressions.sh`'s
8th test, `testPortfolioRestoresCase3FloorPostB5a1`, still passes unchanged — it calls
`runPricingPortfolio` at budget-off, which is exactly phase 1 alone, so the registry refactor is
invisible to it. No test file changes were needed for B5a.2 — the registry produces the same legs
in the same order as B5a.1's hardcoded body.

### 10.4 Budget sweep matrix — golden-`checker_cost`-only view (the ft0 view)

Same sweep configuration as §6 (`ENGINES="bstar hybrid" BUDGETS="0 15 45" SEEDS="1" CASES="0 1 2 3 4"
REF_ITERS=1000`), re-run against the B5a.2 default-first mechanism:

| engine | case | @0 (base) | @15 | Δ%@15 | @45 | Δ%@45 |
|---|---|---:|---:|---:|---:|---:|
| bstar | 0 | 5,202,230.000 | 5,202,230.000 | 0.000% | 5,202,230.000 | 0.000% |
| bstar | 1 | 12,509,317.480 | 10,191,746.900 | −18.531% | 10,021,092.900 | −19.897% |
| bstar | 2 | 52,962,157.808 | 52,962,157.808 | 0.000% | 49,217,165.802 | −7.071% |
| bstar | 3 | 52,353,204.607 | 52,353,204.607 | 0.000% | 52,917,851.466 | **+1.0785%** |
| bstar | 4 | 71,490,184.614 | 71,490,184.614 | 0.000% | 66,949,648.243 | −6.352% |
| hybrid | 0 | 5,233,375.000 | 4,860,186.000 | −7.131% | 4,860,186.000 | −7.131% |
| hybrid | 1 | 9,100,697.980 | 9,100,697.980 | 0.000% | 9,100,697.980 | 0.000% |
| hybrid | 2 | 56,007,181.850 | 56,007,181.850 | 0.000% | 54,359,329.282 | −2.942% |
| hybrid | 3 | 36,615,637.236 | 36,615,637.236 | 0.000% | 36,615,637.236 | 0.000% |
| hybrid | 4 | 66,198,069.443 | 66,198,069.443 | 0.000% | 66,198,069.443 | 0.000% |

All 30 cells: `open_routes=0`. **19 of 20 non-baseline cells pass** under this view (huge
improvement from B5a.1's 12/20, §6) — the *only* remaining failure is `bstar case3@45` at
**+1.0785%**, the exact cell diagnosed in `b1-report.md`'s note: phase 2 there traded feedthrough
mismatch (413,340.50 → 97,755.38) for worse checker-relevant geometry. Every other cell that isn't
flat-0.000% (phase 2 found nothing to improve on, or the floor wasn't cleared) strictly *improves*.

### 10.5 Metric-alignment ruling

`checker_cost` is the golden rescorer's (`bin/efgr_rescore`, `src/cfg_score.cpp`) reconstruction of
the contest cost function from a written `.cfg`. It carries `ft_penalty=0` — a hardcoded fallback,
not a measured zero — because **every alpha-test golden case happens to carry feedthrough mismatch
0**, so that term of the formula was never exercised or calibrated against a nonzero example. That
absence is evidence of *no alpha coverage*, not evidence of *no official deduction*.

Official QA A1.2 states the contest cost function applies a proportional deduction for
feedthrough-area mismatch, and the alpha-test `report.json` itself carries a constant `β_ft = 3.0`
for precisely this term (project memory: `official-cost-function.md`). The contest-defensible
metric is therefore:

```
official_expected = checker_cost + β_ft * feedthrough_mismatch   (β_ft = 3.0)
```

This is **not a new formula introduced for this gate** — it is exactly the solver's own internal
`score.value`, verified numerically against the golden `checker_cost` on all 30 sweep cells:

```
MAX ABS DIFF ACROSS ALL 30 ROWS (checker_cost + 3.0*mismatch  vs.  score.value): 0.000400
```

(Full per-row identity check available; the largest absolute discrepancy across 30 rows spanning
values up to ~7.2e7 is four ten-thousandths — floating-point accumulation noise, not a formula
mismatch.) **`paydownBetter` was already folding by the aligned, contest-defensible metric all
along** — `score.value` — every time it folded the portfolio's legs. The golden rescorer's
`checker_cost`, which silently drops the `β_ft` term, was the outlier relative to the official cost
function, not the fold.

### 10.6 Budget sweep matrix — aligned-metric view (`score.value`)

Same 30 cells, gated on `score.value` (`= checker_cost + 3.0*feedthrough_mismatch`) instead:

| engine | case | @0 (base) | @15 | Δ%@15 | @45 | Δ%@45 |
|---|---|---:|---:|---:|---:|---:|
| bstar | 0 | 5,205,374.6300 | 5,205,374.6300 | 0.000% | 5,205,374.6300 | 0.000% |
| bstar | 1 | 12,960,898.3902 | 10,536,792.2000 | −18.703% | 10,379,912.2500 | −19.914% |
| bstar | 2 | 56,762,166.3083 | 56,762,166.3083 | 0.000% | 50,289,915.3120 | −11.402% |
| bstar | 3 | 53,593,226.1073 | 53,593,226.1073 | 0.000% | 53,211,117.6061 | **−0.713%** |
| bstar | 4 | 72,179,626.8237 | 72,179,626.8237 | 0.000% | 67,501,336.9628 | −6.482% |
| hybrid | 0 | 5,256,095.6800 | 4,897,142.7300 | −6.829% | 4,897,142.7300 | −6.829% |
| hybrid | 1 | 9,473,691.7599 | 9,473,691.7599 | 0.000% | 9,473,691.7599 | 0.000% |
| hybrid | 2 | 57,334,883.7500 | 57,334,883.7500 | 0.000% | 56,099,100.2917 | −2.155% |
| hybrid | 3 | 38,049,010.3264 | 38,049,010.3264 | 0.000% | 38,049,010.3264 | 0.000% |
| hybrid | 4 | 70,951,815.4729 | 70,951,815.4729 | 0.000% | 70,951,815.4729 | 0.000% |

**20/20 non-baseline cells pass** (every value ≤ its own budget-0 baseline; several exactly equal —
phase 2 found nothing to improve on and the fold degenerates to phase 1 verbatim; the rest strictly
improve). Counting the 10 trivially-passing budget-0 baselines, **30/30 sweep cells pass under the
aligned metric.** The previously-failing cell flips sign: **`bstar case3@45` = −0.7130%** (an actual
improvement, matching the coordinator's own "becomes −0.7%-ish" prediction almost exactly) — under
the metric `paydownBetter` actually optimizes, phase 2's mismatch paydown at case3/@45 *is* a net win
once `β_ft` is priced in.

**Gate verdict: PASSES, 30/30, under the aligned metric.** This is the metric used to mark the
budget gate CLOSED in `b1-report.md`.

### 10.7 Dual-metric table — cells with mismatch > 0

The instruction was to show both metrics side by side for every cell whose winner carries
`feedthrough_mismatch > 0`. **That is every one of the 30 cells in this sweep** — see §10.8; no
cell in this dataset has zero mismatch, so the "filtered" table and the full table are the same
table. In the interest of not duplicating §10.4/§10.6 twice more, here it is once, with mismatch and
the implied `3×mismatch` gap called out directly (this is the same data as §10.4/§10.6, joined):

| engine | case | budget | checker_cost | score (aligned) | mismatch | 3×mismatch |
|---|---|---:|---:|---:|---:|---:|
| bstar | 0 | 0/15/45 | 5,202,230.000 | 5,205,374.6300 | 1,048.21 | 3,144.63 |
| bstar | 1 | 0 | 12,509,317.480 | 12,960,898.3902 | 150,526.97 | 451,580.91 |
| bstar | 1 | 15 | 10,191,746.900 | 10,536,792.2000 | 115,015.10 | 345,045.30 |
| bstar | 1 | 45 | 10,021,092.900 | 10,379,912.2500 | 119,606.45 | 358,819.35 |
| bstar | 2 | 0/15 | 52,962,157.808 | 56,762,166.3083 | 1,266,669.50 | 3,800,008.50 |
| bstar | 2 | 45 | 49,217,165.802 | 50,289,915.3120 | 357,583.17 | 1,072,749.51 |
| bstar | 3 | 0/15 | 52,353,204.607 | 53,593,226.1073 | 413,340.50 | 1,240,021.50 |
| bstar | 3 | 45 | 52,917,851.466 | 53,211,117.6061 | 97,755.38 | 293,266.14 |
| bstar | 4 | 0/15 | 71,490,184.614 | 72,179,626.8237 | 229,814.07 | 689,442.21 |
| bstar | 4 | 45 | 66,949,648.243 | 67,501,336.9628 | 183,896.24 | 551,688.72 |
| hybrid | 0 | 0 | 5,233,375.000 | 5,256,095.6800 | 7,573.56 | 22,720.68 |
| hybrid | 0 | 15/45 | 4,860,186.000 | 4,897,142.7300 | 12,318.91 | 36,956.73 |
| hybrid | 1 | 0/15/45 | 9,100,697.980 | 9,473,691.7599 | 124,331.26 | 372,993.78 |
| hybrid | 2 | 0/15 | 56,007,181.850 | 57,334,883.7500 | 442,567.30 | 1,327,701.90 |
| hybrid | 2 | 45 | 54,359,329.282 | 56,099,100.2917 | 579,923.67 | 1,739,771.01 |
| hybrid | 3 | 0/15/45 | 36,615,637.236 | 38,049,010.3264 | 477,791.03 | 1,433,373.09 |
| hybrid | 4 | 0/15/45 | 66,198,069.443 | 70,951,815.4729 | 1,584,582.01 | 4,753,746.03 |

### 10.8 Budget=0 winner mismatch — all 10 cells (honesty check)

The adjudication's supporting rationale for the metric ruling included: *"all shipped defaults
carry mismatch=0 so both metrics agree on actual submissions."* The instruction was to add a
per-cell winner-mismatch column proving this for the 10 budget-0 cells. **The data does not support
that claim** — reporting it plainly rather than asserting it:

| engine | case | checker_cost | score | feedthrough_mismatch |
|---|---|---:|---:|---:|
| bstar | 0 | 5,202,230.000 | 5,205,374.6300 | **1,048.21** |
| bstar | 1 | 12,509,317.480 | 12,960,898.3902 | **150,526.97** |
| bstar | 2 | 52,962,157.808 | 56,762,166.3083 | **1,266,669.50** |
| bstar | 3 | 52,353,204.607 | 53,593,226.1073 | **413,340.50** |
| bstar | 4 | 71,490,184.614 | 72,179,626.8237 | **229,814.07** |
| hybrid | 0 | 5,233,375.000 | 5,256,095.6800 | **7,573.56** |
| hybrid | 1 | 9,100,697.980 | 9,473,691.7599 | **124,331.26** |
| hybrid | 2 | 56,007,181.850 | 57,334,883.7500 | **442,567.30** |
| hybrid | 3 | 36,615,637.236 | 38,049,010.3264 | **477,791.03** |
| hybrid | 4 | 66,198,069.443 | 70,951,815.4729 | **1,584,582.01** |

**Every one of the 10 budget-0 cells carries nonzero feedthrough mismatch** — including the
`hybrid` rows, which are the literal no-flags-overridden shipped default (`bin/efgr input.csv
output.cfg`, i.e. `--engine hybrid --iters 1000 --seed 1` with no `--time-budget`). Mismatch ranges
from 1,048.21 (bstar case0, smallest) to 1,584,582.01 (hybrid case4, largest — a ~4.75M swing
between the two metrics on that one case alone).

**This does not undermine the metric-alignment ruling itself** — §10.5's argument (A1.2's
proportional deduction, `report.json`'s constant `β_ft=3.0`, the verified `checker_cost + 3·mismatch
= score.value` identity) holds regardless of whether mismatch happens to be exactly 0; it is not
contingent on this "low-stakes in practice" framing. But the framing itself is not supported by
today's 5 test cases: `checker_cost` and `score.value` are materially different numbers — not a
theoretical edge case — on every one of the 10 default-shape cells actually measured. Flagged here
for visibility per instruction ("disclose, don't hide"), not silently smoothed over.

### 10.9 Gate verdict

**Budget gate: CLOSED**, on the aligned metric (§10.5/§10.6, 30/30). `b1-report.md`'s note has been
updated a third time to state this, with the full rationale, and with the golden-ft0-view's one
remaining cell (`bstar case3@45`, +1.0785%, §10.4) disclosed alongside it rather than omitted.
`main.cpp`'s `kDefaultTimeBudgetSec` stays `0.0` — closing this gate is a precondition for later
flipping that default, not the same action as flipping it.

### 10.10 Runtime

- **Default (budget-off) 5-case total**: **130.0s** (vs. §6's 136.760s for B5a.1 — no material
  change, as expected: budget-off is a single registry pass in both versions).
- **Budget-mode wall-clock is no longer ≈ the requested budget** — this is a real, worth-disclosing
  cost of the "never worse than default" guarantee. Measured directly (`hybrid`, case3,
  `--time-budget 45`): phase 1 alone (the deterministic default, measured standalone) took **36.79s**
  (`iters=6000`); the full budgeted run took **65.37s** wall-clock total (`iters=10596`,
  `budget_phase2=yes`) — i.e. **45% over the nominal 45s budget**, not under or at it. The gap is
  phase 1's own registry (4 legs, each paying its elite-pass finalization cost) running to
  completion *before* phase 2's wall-clock-bounded continuation even starts, plus phase 2 running
  its *own* full 4-leg registry (each again paying elite-pass finalization) under the residual
  `remaining/4` budget. Net effect: a budgeted run now always costs **at least** the default run's
  own wall-clock cost `t0`, plus up to the requested residual, plus the fixed per-leg elite-pass
  overhead paid twice (once per phase) instead of once. `--time-budget N` no longer means "finish in
  ~N seconds" — it means "spend N seconds of *extra* search on top of whatever the default costs,
  never less than the default." Worth flagging for anyone tuning a wall-clock deadline against this
  flag; not addressed further here (out of scope for this task — no code change proposed).

### 10.11 Files changed (delta from B5a.1's §7 list)

- `src/search.hpp` — new `struct SearchVariant`, new `std::vector<SearchVariant> defaultVariants()`
  declaration, new `SearchResult.budget_phase2` field (next to `elite_admission_v2`),
  `runPricingPortfolio`'s doc comment rewritten for the two-phase contract.
- `src/search.cpp` — `defaultVariants()` implementation (the former hardcoded 4-leg body, now data);
  `runPricingPortfolio` rewritten around the phase-1/phase-2 split and the registry loop (net diff
  vs. B5a.1: +106/−87 lines).
- `src/main.cpp` — stderr summary gained `budget_phase2=yes|no` immediately after `admission=`.
- `.superpowers/sdd/b1-report.md` — budget-gate note revised a third time: CLOSED, with the
  metric-alignment rationale and the disclosed golden-ft0 cell.
- `.superpowers/sdd/b5a-report.md` — this addendum.
- `.superpowers/sdd/progress.md` — ledger line appended.
- No changes to `test/regression_b5_20260724.cpp` or `test/run_b5_regressions.sh` — the registry
  reproduces B5a.1's exact leg set/order, so the existing 8 tests (unit + portfolio-floor pin) cover
  the new code without modification.
- No changes to `eval.cpp`, `routing.cpp`, `respace.cpp`, `cfg_score.cpp`, `validate.cpp`,
  `placement_engine.cpp`.

### 10.12 Deviations / honest findings

- **The "defaults carry mismatch=0" supporting rationale is not supported by this project's 5 test
  cases** (§10.8) — surfaced plainly rather than omitted, per instruction. Does not change the gate
  verdict (§10.5's core argument does not depend on it) but does mean the metric choice is
  materially consequential on every real case measured, not a low-stakes technicality.
- **Budget-mode wall-clock now exceeds the requested budget** (§10.10) — an inherent, disclosed
  consequence of making every budgeted run pay for two full registry passes (phase 1 + phase 2)
  instead of one. No code change attempted to mitigate this — out of scope for this task.
- **All of B5a.1's §8 open findings are now resolved by the default-first mechanism itself**: the 8
  sweep-gate failures §6 reported (`bstar case0@{15,45}`, `bstar case3@45`, `hybrid case1@{15,45}`,
  `hybrid case3@{15,45}`, `hybrid case4@45`) are down to 1 under the golden-ft0 view (§10.4) and 0
  under the aligned view (§10.6) — none of them needed a targeted fix; the structural phase-1-always-
  in-the-fold guarantee subsumed all of them at once.
- No architectural changes beyond the coordinator's own spec: `paydownBetter`, `defaultVariants()`'s
  leg set/order, and B5a's free functions (`rememberBounded`/`reservoirSample`/`buildEliteFullSet`)
  are untouched. Only `runPricingPortfolio`'s own control flow (phase 1/phase 2 split) and its
  surrounding declarations changed.

### 10.13 Commit

One commit, staging exactly: `src/search.cpp`, `src/search.hpp`, `src/main.cpp`,
`.superpowers/sdd/b1-report.md`, `.superpowers/sdd/b5a-report.md`, `.superpowers/sdd/progress.md`;
message: `feat(search): variant-registry portfolio; budget mode runs default legs first (B5a.2)` +
`Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>`.
