# B5b Report — Abutment-credit cheap-score upgrade, landed as a registry variant

Status: **DONE.** Implemented exactly per `.superpowers/sdd/b5-design.md` §3, flag-gated behind
`Problem.abutment_credit` (default `false`, byte-identical), landed as **two new legs** in the
`SearchVariant` registry `defaultVariants()` established by B5a.2 — the flag-gated-variant lesson
B5a's own STOP (`.superpowers/sdd/b5a-report.md` §1) forced onto this codebase. All 5 default-flag
cases pass their pins under the **aligned metric** (coordinator ruling below, extending
b5a-report.md §10.5 to every per-case gate from B5b onward); the golden `checker_cost`-only view is
reported alongside for transparency, per the same "disclose, don't hide" discipline b5a-report.md
§10.4/§10.7/§10.8 established, and shows one disclosed cell (case4, +3.43%) that the ruling below
explains and accepts. 17/17 suites green, warning-clean.

---

## 1. What changed

- **`src/model.hpp:88`** — `bool abutment_credit = false;` added to `Problem`, doc-commented like
  the `route_pricing_official` precedent it mirrors (same file, a few lines above): solver-internal,
  never CSV-parsed, exists purely so the registry can run it as a dimension.
- **`src/eval.cpp:52`** — `constexpr double kAbutRampFrac = 1.0;`, the named ramp-width tunable
  §3.2 calls for ("partial credit (ramp), width one block side").
- **`src/eval.cpp:62-75`** — `abutmentCredit(a, b)`, implementing §3.2's formula verbatim: x/y-overlap,
  dx/dy-gap, the three-way `g` selection (vertical-edge / horizontal-edge / diagonal), `d_thr =
  kAbutRampFrac·0.5·(min(a.w,b.w)+min(a.h,b.h))`, `credit = clamp(1 − g/d_thr, 0, 1)`, diagonal pairs
  capped at 0.5. Pure function of two rects, deterministic, no `Problem`-level state.
- **`src/eval.cpp:101`** — `routeAwareProxyPenalty`'s congestion term: `if (problem.abutment_credit)
  congestion *= (1.0 - abutmentCredit(a, b));` — gated by an `if`, not a multiply-by-1, so credit-off
  skips the new arithmetic entirely.
- **`src/eval.cpp:174`** — `cheapScore`'s WL term: same gating discipline,
  `if (problem.abutment_credit) term *= (1.0 - abutmentCredit(a, c));`.
- **`src/search.hpp:96`** — `SearchResult.abutment_credit` (telemetry, defaults `false`, same
  precedent as `pricing_official`/`elite_admission_v2`/`budget_phase2`).
- **`src/search.hpp:270`** — `SearchVariant.abutment_credit` (the registry dimension).
- **`src/search.cpp:962-978`** — `defaultVariants()` now returns **6 legs**: the original 4
  (`{true,false,false}`, `{false,false,false}`, `{true,true,false}`, `{false,true,false}` — all
  credit-off, unchanged), then B5b's 2 new legs (`{true,true,true}` = official/v2/credit-on,
  `{true,false,true}` = official/v1/credit-on), in exactly that order.
- **`src/search.cpp:1002`** — `runLeg`'s Problem copy gains `p.abutment_credit =
  variant.abutment_credit;`.
- **`src/search.cpp:1079`** — winner-stamping gains `result.abutment_credit = winner.abutment_credit;`.
- **`src/main.cpp:134`** — stderr summary gains `credit=on|off` immediately after `admission=`.
- **`test/regression_b5_20260724.cpp`** — 5 new tests (§2 below), wired into the existing
  `run_b5_regressions.sh` (no g++ line change needed — `src/eval.cpp`/`src/route_cost.hpp` were
  already linked/available).

No other file touched. `eval.cpp`'s exact tier (`exactScore`/`routeAndScoreAt`), `routing.cpp`,
`respace.cpp`, `cfg_score.cpp`, `validate.cpp`, `placement_engine.cpp` are all untouched by this
task, consistent with §3.4's own scoping.

## 2. Unit tests (design §7 B5b + this task's item 3)

All 5 new tests pass, appended to `test/regression_b5_20260724.cpp`'s `main()`:

- **`testAbutmentCreditFullAtExactTouch`** — a two-block fixture (10×10 each, `nets=1000` so the
  congestion term is also exercised) at gap 0 (exact `positiveTouch`-consistent abutment): asserts
  `score.wirelength == 0.0` **and** `score.penalty == 0.0` — both credited terms fully zeroed.
- **`testAbutmentCreditZeroAtRampThreshold`** — same fixture at `gap == d_thr` (10.0 for this
  fixture): asserts credit-on and credit-off produce **bit-identical** `Score` in every field
  (`credit == 0.0` exactly, so `(1−credit) == 1.0` exactly, and IEEE754 `x·1.0 == x`).
  Note this test is what pinned the "byte-same as credit-off for that bundle" wording verbatim.
- **`testAbutmentCreditMidRampMonotonicity`** — same fixture at gaps 8/5/2 (all inside the ramp):
  asserts strictly decreasing `wirelength` and `value` as the gap shrinks. Measured:
  `wl@8=14400 wl@5=7500 wl@2=2400` — matches the hand-derived `credited_WL(g)=(10+g)·1000·(g/10)`
  exactly (§3.3's monotonicity argument, numerically confirmed).
- **`testAbutmentCreditFlagOffByteIdenticalOnNontrivialFixture`** — a 3-block/2-bundle fixture
  (one pair at *exact* geometric abutment, gap 0) with `abutment_credit=false`: independently
  recomputes the expected `wirelength`/`penalty`/`value` from the same public helpers
  (`manhattan`, `kBetaLin`) `cheapScore`/`routeAwareProxyPenalty` themselves use, and asserts exact
  equality — proving the credit-off path performs the **identical** arithmetic to pre-B5b, not
  merely a close approximation. Measured: `wirelength=2700, penalty=1000` (hand-verified).
- **`testDefaultVariantsRegistryHasSixLegsInOrder`** — pins the registry's exact 6-leg shape/order.

A test-fixture bug was caught and fixed mid-task (not an implementation bug): the first version of
the two-block fixture set `Placement.outline_w/h` but not `Problem.outline_w/h` (the MAX-outline
containment bound `cheapScore` actually checks), which spuriously tripped the `1e12` out-of-outline
penalty on every rect. Fixed by setting both; all 5 tests then passed on the first re-run with every
hand-computed number matching exactly.

## 3. Correctness verification (byte-identity + isolation proof)

**Structural argument:** the credit is applied behind `if (problem.abutment_credit) { ... }` in both
call sites — not a `×(1.0 − 0.0)` — so when the flag is `false` the exact pre-B5b arithmetic executes
with zero new instructions in the taken path. The 4 pre-existing registry legs all set
`abutment_credit=false` explicitly in `defaultVariants()`, so their `Problem` copies are
byte-identical to what `runLeg` would have produced without this field existing at all.

**Empirical confirmation:** isolated leg 3, `(official, v2, credit-off)` — the pre-existing floor
leg — directly on case4, bypassing the 6-leg fold entirely:

```
leg3(official,v2,credit-off) open_routes=0 score.value=70951815.472934 mismatch=1584582.010000
leg3 checker_cost=66198069.442934
```

`checker_cost=66,198,069.442934` matches the historical pin (66,198,069.443) exactly, and
`score.value=70,951,815.472934` matches b5a-report.md §10.6's own recorded number for this cell to
the same precision. The 4 old legs are unaffected, both by construction and by direct measurement.

**Self-consistency, end to end:** `run_selfconsistency_regressions.sh` stayed green (212s) running
the *real* default pipeline, in which credit-on legs now win 3 of 5 cases — an empirical, full-scale
confirmation of §3.4's cancellation proof (the credited `routeAwareProxyPenalty` term is added in
`cheapScore`'s addend and subtracted identically in `routeAndScoreAt`, so it cancels regardless of
the flag; `exactScore.value` is untouched by construction).

## 4. Metric-alignment ruling, extended to all per-case gates (coordinator adjudication)

The per-case default-flag acceptance gate (§5 below) surfaced the same metric-disagreement shape
b5a-report.md §10.4–§10.9 already found and ruled on for the *budget-sweep* gate: a new leg can
legitimately win `runPricingPortfolio`'s fold (which folds by `paydownBetter`, i.e. by `score.value`)
while showing a *worse* number under the golden rescorer's `checker_cost` alone, because
`checker_cost` hardcodes `ft_penalty=0` (never calibrated against a nonzero feedthrough-mismatch
case) and is therefore blind to a real trade the aligned metric prices correctly.

**Adjudication:** the b5a-report.md §10.5 aligned-metric ruling now extends to **every** per-case
gate and pin, not just the budget-sweep gate — the governing metric, from B5b onward, is

```
official_expected = checker_cost + β_ft · feedthrough_mismatch     (β_ft = 3.0)
                   = score.value   (on any legal, open_routes==0 output)
```

**Additional rationale recorded here** (beyond b5a-report.md §10.5's original A1.2/`report.json`
argument): the case4 credit-on winner's ~92% feedthrough-mismatch cut (1,584,582.01 → 120,210.35,
§6 below) is also the **tail-safe choice under β_ft uncertainty**. Official QA A1.2 states only that
the deduction is "proportional" to feedthrough-area mismatch — it does not pin the proportionality
constant to 3.0 for every contest instance. If the true rate the official checker applies exceeds
3.0, the *old* floor leg's 1.58M µm² of mismatch is a large hidden liability that scales with
whatever that rate turns out to be, whereas the new pick's 120k µm² is nearly immune to that
uncertainty. The downside if the true rate were instead 0 (i.e. `checker_cost` alone were the whole
story) is a *bounded* +3.43% on this one case — asymmetric risk in the new leg's favor.

**Transparency requirement (binding from B5b onward):** every per-case pin/gate in this project is
now stated in the aligned metric, **with the golden-`checker_cost`-only ("ft0") view always reported
alongside it**, never omitted — the same "disclose, don't hide" discipline b5a-report.md's §10.4/
§10.7/§10.8 already practiced for the budget-sweep gate, now generalized to every gate.

## 5. Acceptance — per-case, both metrics (item 4)

| case | pin (checker_cost, ft0) | measured checker_cost | Δ% (ft0) | **aligned score.value — OLD** | **aligned score.value — NEW** | **Δ% (aligned, governing)** | winner (pricing, admission, credit) |
|---|---:|---:|---:|---:|---:|---:|---|
| 0 | 5,233,375.00 | 4,624,846.00 | −11.63% | 5,256,095.68 | **4,653,040.63** | **−11.47%** | official, v2, **on** |
| 1 | 9,100,697.98 | 8,564,588.73 | −5.89% | 9,473,691.76 | **8,881,672.86** | **−6.25%** | official, v2, **on** |
| 2 | 56,007,181.85 | 56,007,181.85 | 0.00% | 57,334,883.75 | **57,334,883.75** | **0.00%** | official, v2, off (unchanged floor) |
| 3 | 36,615,637.24 | 36,615,637.24 | 0.00% | 38,049,010.33 | **38,049,010.33** | **0.00%** | legacy, v1, off (unchanged floor) |
| 4 | 66,198,069.44 | 68,469,987.82 | **+3.43%** (disclosed) | 70,951,815.47 | **68,830,618.87** | **−3.00%** | official, v1, **on** |
| **Σ** | **173,154,961.51** | **174,282,241.64** | **+0.65%** (disclosed) | **181,065,496.99** | **177,749,226.44** | **−1.83%** | |

**New reference numbers, stated prominently per instruction:**

- **Aligned Σ (governing metric) = 177,749,226.44** (was 181,065,496.99 → **−1.83%**).
- **Golden-ft0 Σ (disclosed, non-governing) = 174,282,241.64** (was 173,154,961.51 → **+0.65%**,
  driven entirely by case4; not a gate failure under the ruling above, reported for transparency).
- **Per-case aligned (the new pins, effective immediately):** case0 **4,653,040.63** / case1
  **8,881,672.86** / case2 **57,334,883.75** / case3 **38,049,010.33** / case4 **68,830,618.87**.

**Gate verdict: PASSES, 5/5, under the governing (aligned) metric** — every case is ≤ its prior
aligned value, 0 cases regress. The ft0 view's one disclosed cell (case4, +3.43%) is explained in
§6 and accepted per §4's ruling, not hidden.

## 6. Leg-isolation table for case4 (why the ft0 view disagrees)

Isolating case4's three relevant legs directly (bypassing the fold):

| leg | checker_cost | score.value (aligned) | feedthrough_mismatch |
|---|---:|---:|---:|
| (official, v2, credit-**off**) — existing floor, isolated | 66,198,069.44 | 70,951,815.47 | 1,584,582.01 |
| (official, v2, credit-**on**) — new, isolated | 72,203,342.75 | 72,815,345.87 | 204,001.04 |
| (official, v1, credit-**on**) — new, **wins the 6-leg fold** | 68,469,987.82 | 68,830,618.87 | 120,210.35 |

The winning leg trades a higher ft0 `checker_cost` for a ~92% cut in feedthrough mismatch relative
to the old floor, netting a real improvement on the metric the fold actually optimizes. Neither new
leg on case4 is a bug: both are legal (`open_routes=0`), deterministic, and the credit-off floor leg
(row 1) is independently confirmed unchanged (§3).

## 7. τ-diagnostics (item 5 — measurement, not a gate)

| case | leg | tau | pool_tau | exact_best_cheap_rank/K |
|---|---|---:|---:|---|
| 0 | OLD (official,v1,credit-off, pre-B5b winner) | 0.067 | 0.828 | 1/16 |
| 0 | NEW (official,v2,credit-**on**, wins post-B5b) | 0.200 | 0.757 | 1/48 |
| 0 | NEW (official,v1,credit-**on**, isolated) | 0.067 | 0.133 | 12/16 |
| 1 | OLD (official,v1,credit-off, pre-B5b winner) | 0.689 | −0.433 | 9/16 |
| 1 | NEW (official,v2,credit-**on**, wins post-B5b) | 0.822 | 0.186 | 43/48 |
| 1 | NEW (official,v1,credit-**on**, isolated) | 0.822 | −0.217 | 6/16 |
| 2 | OLD (official,v2,credit-off, wins both pre/post-B5b) | 0.689 | −0.294 | 39/48 |
| 2 | NEW (official,v2,credit-**on**, isolated) | 0.689 | −0.300 | 46/48 |
| 2 | NEW (official,v1,credit-**on**, isolated) | 0.689 | **+0.300** | 7/16 |
| 3 | OLD (legacy,v1,credit-off, wins both pre/post-B5b) | 0.600 | −0.067 | 16/16 |
| 3 | NEW (official,v2,credit-**on**, isolated) | 0.644 | **+0.599** | 1/48 |
| 3 | NEW (official,v1,credit-**on**, isolated) | 0.644 | **+0.433** | 1/16 |
| 4 | OLD (official,v2,credit-off, pre-B5b winner) | 0.200 | −0.475 | 45/48 |
| 4 | NEW (official,v1,credit-**on**, wins post-B5b) | 0.644 | **+0.200** | 3/16 |

**Reading:** `pool_tau` flips from negative to positive on case2 (v1-admission leg only), case3
(both new legs), and case4 (the winning leg) — the WL/mismatch-heavier cases the design's own
hypothesis targeted (§3, design doc §7 target table: "does credit improve tau/pool_tau on WL-heavy
cases 2/3/4?"). Case0/case1 are more mixed (case1's v2 leg improves pool_tau to 0.186 from −0.433;
its v1 leg is still negative at −0.217; case0's v2 leg is essentially flat vs its already-strong old
value of 0.828). Not a uniform win — reported as measured, not oversold. Interesting secondary
finding: case2 and case3's two new legs (v1-admission, v2-admission) converge to the **same**
`checker_cost`/`score.value`/`mismatch` on their own case — since admission only changes which
candidates receive the expensive elite-pass sweep/compact/respace treatment, not the SA loop itself
(which the credit *does* affect equally under both admission schemes), both admission variants
apparently land on the same final basin for these two cases.

## 8. Runtime (item 6)

5-case default (6-leg) total: **206.24s**, vs ~137s for the pre-B5b 4-leg registry (B5a.1/B5a.2) —
**+50.5%**, matching this task's own expectation ("current 4-leg ~137s; expect ~+50%"). This is
**under the 210s bound, but by only 3.76s (1.8% margin)**.

**Watch item flagged for B2's leg-pruning policy:** this margin is thin enough that ordinary
run-to-run system-load variance could plausibly tip a future measurement over 210s even with no
further code change. B2 (the next portfolio-shaped work, per `CLAUDE.md`'s model-orchestration
table and the multi-seed × {sp,bstar,hybrid} restart portfolio noted in b5-design.md §6) will add at
least one more dimension to this same registry — its own design should budget for pruning a leg
(most plausibly `(official, v2, credit-on)`, leg 5, since §6/§7 show it never wins outright once
`(official, v1, credit-on)` is available, and §7's τ data does not show it uniquely improving
fidelity anywhere it doesn't also lose to leg 6) rather than assuming headroom that no longer exists
at 6 legs. Not acted on here — the task's own runtime gate passes today, and pruning a
task-brief-specified leg is a design decision, not a mechanical one.

## 9. Suite battery (item 7 — all 17 + new tests)

```
PASS test/run_case00.sh (12s)
PASS test/run_placement_engine_regressions.sh (23s)
PASS test/run_20260618_regressions.sh (19s)
PASS test/run_20260608_regressions.sh (17s)
PASS test/run_cfg_score_regressions.sh (10s)
PASS test/run_selfconsistency_regressions.sh (212s)
PASS test/run_integer_output_regressions.sh (29s)
PASS test/run_testdata_regressions.sh (9s)
PASS test/run_p0_regressions.sh (11s)
PASS test/run_p1_regressions.sh (28s)
PASS test/run_p2_regressions.sh (8s)
PASS test/run_p3_regressions.sh (14s)
PASS test/run_b1_regressions.sh (46s)
PASS test/run_b3_regressions.sh (26s)
PASS test/run_b4_regressions.sh (85s)
PASS test/run_b6_regressions.sh (18s)
PASS test/run_b5_regressions.sh (77s)

=== SUMMARY: 17 passed, 0 failed (of 17) ===
```

`run_b1_regressions.sh`/`run_b4_regressions.sh` (the two suites B5a's *unconditional* wiring broke
historically) stay green — their pinned floors hold via the unchanged `(official,v1)`/`(legacy,v1)`
legs, exactly as B5a.1 established. `run_b5_regressions.sh` now carries 13 tests (8 pre-existing +
5 new, §2). Build warning-clean (`-Wall -Wextra -pedantic`, `make clean && make`, zero
warnings/errors). This run was the commit-time re-run, against the exact working tree being
committed (only `src/*.hpp`/`src/*.cpp` and `test/regression_b5_20260724.cpp` differ from HEAD;
this report and the ledger line are documentation, not code, and do not affect suite behavior).

## 10. Files changed

- `src/model.hpp` — `Problem.abutment_credit` (`:88`).
- `src/eval.cpp` — `kAbutRampFrac` (`:52`), `abutmentCredit` (`:62-75`), gating in
  `routeAwareProxyPenalty` (`:101`) and `cheapScore` (`:174`).
- `src/search.hpp` — `SearchResult.abutment_credit` (`:96`), `SearchVariant.abutment_credit`
  (`:270`), `defaultVariants()`/`runPricingPortfolio` doc comments updated for the 6-leg contract.
- `src/search.cpp` — `defaultVariants()` (`:962-978`, 6 legs), `runLeg` (`:1002`), winner-stamping
  (`:1079`).
- `src/main.cpp` — stderr gains `credit=on|off` (`:134`).
- `test/regression_b5_20260724.cpp` — 5 new tests + 2 fixture builders (§2); gained
  `#include "../src/eval.hpp"` and `#include "../src/route_cost.hpp"`.
- `test/run_b5_regressions.sh` — unchanged (no new source dependency; `src/eval.cpp` and
  `route_cost.hpp` were already reachable from the existing g++ line).
- `.superpowers/sdd/b5b-report.md` — this file.
- `.superpowers/sdd/progress.md` — ledger line appended.

No changes to `routing.cpp`, `respace.cpp`, `cfg_score.cpp`, `validate.cpp`,
`placement_engine.cpp`, or any B5c-scoped code (move portfolio, §4 of the design doc) — out of
scope for this task, not started.

## 11. Deviations / honest findings

- **Metric-alignment ruling generalized mid-task** (§4): the per-case gate's literal wording named
  golden `checker_cost` with a "STOP on violation" instruction; case4 violated it (+3.43%) while
  improving on the aligned metric (−3.00%). Stopped and reported per instruction rather than
  guessing at the resolution; the coordinator's adjudication (recorded verbatim in §4) extends the
  b5a-report.md §10.5 ruling to this gate and all future per-case gates, with an additional
  tail-safety rationale (β_ft-uncertainty asymmetry) added to the record. Both new legs are kept —
  `(official, v1, credit-on)` was NOT dropped, per explicit instruction, despite being the sole
  source of the ft0-view disagreement.
- **A test-fixture bug** (missing `Problem.outline_w/h` in the first draft of the two-block fixture,
  §2) was caught before it could mask a real result — flagged honestly as a test-authoring mistake,
  not an `eval.cpp` defect.
- **Runtime margin is thin** (§8, 1.8%) — disclosed as a watch item for B2, not silently absorbed.
- **τ/pool_tau improvement is real but not uniform** (§7) — cases 2/3/4 show clear gains on at least
  one new leg; case0/case1 are mixed. Reported as measured.
- No architectural changes beyond the coordinator's own spec: `paydownBetter`, `buildEliteFullSet`,
  `outlineSweep`, `defaultVariants()`'s pre-existing 4-leg shape, and the exact tier are all
  untouched. Only `cheapScore`/`routeAwareProxyPenalty` (the credit itself) and the registry's leg
  list/count changed.

## 12. Commit

One commit, staging exactly: `src/model.hpp`, `src/eval.cpp`, `src/search.hpp`, `src/search.cpp`,
`src/main.cpp`, `test/regression_b5_20260724.cpp`, `.superpowers/sdd/b5b-report.md`,
`.superpowers/sdd/progress.md`; message:
`feat(eval): bundle abutment credit as registry variant (B5b)` +
`Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>`.
