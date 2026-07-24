# B5a / B5a.1 Report — Elite-pool rework, its regression, and the flag-gated variant-portfolio fix

Status: **B5a.1 DONE** (flag-gated variant portfolio); **budget-gate (item 5) does NOT pass** — 8 of 20
non-baseline sweep cells still exceed +1% vs their own budget-0 (down from a much worse pre-fix
picture; see §5). Per the coordinator's explicit instruction, the §5 structural-snapshot backstop is
**not** built here — that decision is reserved for the coordinator. Everything else (byte-identity
gate item 1, per-case pins item 3, all 17 suites item 4) passes.

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
