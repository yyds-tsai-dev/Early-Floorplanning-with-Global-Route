# P2 Task 6 Report — cost-driven routing default-on

Branch `fable5`, base commit `f4443d0`.

## 1. Implementation

### 1.1 The flip (requirement 1)

`src/routing.hpp:14` — default parameter changed:

```cpp
// before
RoutingResult routeProblem(const Problem& problem, const Placement& placement, bool cost_driven = false);
// after
RoutingResult routeProblem(const Problem& problem, const Placement& placement, bool cost_driven = true);
```

No other signature change. Doc comment above it rewritten to describe the new default and point at
the T5/T5.1 comparison gate as the safety net; the legacy path stays reachable via an explicit
`cost_driven=false` (used by tests, and internally by `routing.cpp`'s own `runLegacyPass`/gate).

Only one production call site exists (`src/eval.cpp:139`, `routeProblem(scoped, placement)`), and it
was already relying on the default argument, so it now gets `cost_driven=true` — the comparison gate
inside `routeProblem` (landed at T5.1, commit `f4443d0`) means this call now always races the
cost-driven+negotiated router against the true legacy two-pass router and returns whichever wins the
lexicographic key `(open_routes, then alpha*WL + kOverflowAreaWeight*overflow +
kFtMismatchWeight*mismatch)`, ties going to cost-driven. `search.cpp` and `respace.cpp` call
`exactScore`/`routeProblem` transitively through `eval.cpp`, so they inherit this automatically; no
edits needed there.

### 1.2 Constant unification (requirement 2)

Grepped `1e6` across `src/`: the only occurrences meaning the overflow/FT-mismatch weights were
`src/eval.cpp:145-146` (the router side, `src/route_cost.hpp:20-21`, already used named constants
since T1). `model.hpp` has no T0-era duplicate of these constants (never existed there — T0 put them
directly in `route_cost.hpp`). No `1e9`/`1e12`/`1e15` literals were touched (different semantics —
overlap/outline/open-route penalties, not overflow/FT weights).

`src/eval.cpp`:
- line 9: added `#include "route_cost.hpp"`.
- lines 142-149 (`routeAndScoreAt`): `routing.channel_overflow * 1e6` → `routing.channel_overflow *
  kOverflowAreaWeight`; `routing.feedthrough_mismatch * 1e6` → `routing.feedthrough_mismatch *
  kFtMismatchWeight`. Pure refactor — both constants equal `1e6`, so `eval.cpp`'s emitted `Score`
  values are numerically unchanged by this edit alone (verified: no test depending on these lines
  needed updating, see §3).

This closes the single-source-of-truth gap the design's §7/§9-T0 flagged: router (`route_cost.hpp`)
and scorer (`eval.cpp`) now reference the identical symbols, so a future re-tune only touches one
place.

### 1.3 Rider B — force_legacy warning comment (requirement 4)

`src/routing.cpp`, at the `findRoutePath` lambda's `force_legacy` parameter declaration (now around
line 174, immediately before the lambda), added:

```cpp
// WARNING (P2 Task 6, Rider A of the T5.1 review): any FUTURE call site that wants true legacy
// (channels-first, then soft-FT fallback) pricing from WITHIN a cost_driven=true routeProblem
// call MUST pass force_legacy=true explicitly. Omitting it silently reruns cost-driven's own
// pricing instead (the exact bug class this parameter was added to fix) -- the default `false`
// only preserves behavior for callers where the outer `cost_driven` is already false.
```

(Note: the brief's own numbering calls this "Rider B"; the comment text says "Rider A" because it
was drafted referencing the T5.1 review's internal rider numbering, which used "Rider A" for this
item — the content matches Task 6's requirement 4 either way.) `routeChunk`'s own `force_legacy`
parameter (a thin wrapper) already deferred to "see findRoutePath's `force_legacy` comment", so it
inherits the warning without duplication.

## 2. Rider A — soft-FT round-consistency fixture (requirement 3)

Added to `test/regression_p2_20260707.cpp`:

- `struct SoftFtRoundConsistencyFixture` + `makeSoftFtRoundConsistencyFixture()` (new lines, inserted
  before the closing `}  // namespace`): A/B blocks separated by a channel with `vcap=25*80=2000`,
  bundle of 3600 nets (1600 must overflow or divert), soft block `S` (200x810, `ft_base_area ==
  placed area`, i.e. **no starting slack** — deliberately different from the existing
  `ChannelVsFtFixture`, whose generous slack keeps `feedthrough_mismatch` at exactly 0). This forces a
  **nonzero** mismatch whenever nets divert through `S`, making the cross-check below nontrivial.
- `testRoundConsistencyOfSoftFeedthroughFields()`: calls `routeProblem(f.problem, f.placement)` with
  the **default** argument (now `true`, exercising the exact T6 production call shape), then:
  1. asserts `open_routes == 0` and `soft_feedthrough_nets[S] > 0` (requirement 3's "nonzero"),
  2. cross-checks `soft_feedthrough_nets[S]` against the sum of `route.nets` over every route whose
     `node_path` passes through `S` as an intermediate — this must equal `soft_feedthrough_nets[S]`
     exactly, since that is literally how `commitRoutePath` accrues it,
  3. recomputes `feedthrough_mismatch` from the returned `soft_feedthrough_nets[S]` via `ft.hpp`
     (`ftRequiredArea`/`ftExtend`/`ftTierRate`, the same formula `computeOverflowAndMismatch` uses)
     and asserts equality with the returned struct's `feedthrough_mismatch` field — the task's named
     check, and asserts the recomputed value is itself `> 0` (proving this is a nontrivial check, not
     0==0),
  4. calls `routeProblem` a second time and asserts `open_routes`, `soft_feedthrough_nets`,
     `feedthrough_mismatch`, and every route's `(node_path, nets)` are identical (determinism).
- Registered in `main()`: `testRoundConsistencyOfSoftFeedthroughFields();` before the final summary
  print (now `"P2 T0+T1+T2+T3+T4+T5+T5.1+T6 regressions passed"`).

Observed real run: `soft_feedthrough_nets[S]=3000, feedthrough_mismatch=9803.81` (all 3600 nets in
this fixture ended up routed via a mix, with 3000 crossing S under the negotiation loop's actual
chunking/history dynamics — the test does not hardcode this number, it recomputes expectations from
whatever the router returns).

### 2.1 Honesty check (TDD requirement)

Rider A passes unmodified against current code (as expected — T4/T5.1 are already correct, so no
natural RED state exists). To prove it is discriminating rather than vacuous, per the task's stated
escape hatch, I ran a **temporary 2-line mutation** in `src/routing.cpp` (reverted immediately, never
committed): appended `best.soft_feedthrough_nets.assign(..., 0.0)` / `result.soft_feedthrough_nets
.assign(..., 0.0)` right before each of `routeProblem`'s two return statements (the post-gate `return
best;` and `return result;`).

- First pass (full suite, mutation in place): aborted on an **earlier** existing assertion
  (`testCostDrivenPrefersFtOverSaturatedChannel`, line 342, `cd.soft_feedthrough_nets[2] > 0.0`) —
  confirms the mutation breaks *something*, but not specifically Rider A.
- Second pass: temporarily edited `main()` to call **only** `testRoundConsistencyOfSoftFeedthroughFields()`
  (isolating it from every earlier test), rebuilt, reran: aborted at
  `test/regression_p2_20260707.cpp:1078: ... Assertion 'r.soft_feedthrough_nets[2] > 0.0' failed` —
  **Rider A itself** fails under the mutation, confirmed in isolation.
- Reverted both the `main()` isolation edit and the `routing.cpp` mutation immediately afterward.
  `git diff --stat` and a `grep -n "TEMP HONESTY"` over `src/routing.cpp` and
  `test/regression_p2_20260707.cpp` confirm zero leftover hack markers before the final build/test
  pass and commit.

## 3. Expected test fallout (requirement 5)

**Zero assertions required changes.** All 8 suites passed green on the first full run after the flip
+ constant unification, with no test file edits beyond the new Rider A addition (§2). I verified this
was not a case of tests silently ignoring the change: I grepped every test file for `routeProblem(...)`
calls that omit the third argument (i.e. that flip from `false` to `true` under this task) —
`test/regression_p0_20260706.cpp`, `test/regression_20260608.cpp`, `test/regression_20260618.cpp`, and
several sites in `test/regression_p1_20260706.cpp`/`test/regression_p3_20260707.cpp` — and inspected
the surrounding assertions. Every one of them asserts a **threshold/qualitative** property (`open_routes
== 0`, `feedthrough_mismatch > 1e-6`, `channel_overflow <= 1e-6`, `near(x, y, tol)`, `vr.ok`,
`routes.size() >= 2`, exact-abut `wirelength == 0`) rather than an exact numeric value tied to the
specific routing algorithm's internal choices. `test/regression_p2_20260707.cpp`'s own default-arg
calls (the T0-era overflow-denomination fixtures, `testVerticalOverflowIsAreaDenominatedByChannelHeight`
etc.) are pure two-block/no-soft-block geometries where cost-driven and legacy routing necessarily agree
(only one path exists), so they were unaffected in principle as well as in practice.

**Count of assertions changed: 0. All-Pareto-safe: yes (trivially — nothing to reconcile).**

No BLOCKED condition was hit: I never observed a case where flipping the default made any existing
assertion fail. Given the task's escalation condition is specifically "if any changed outcome is NOT
explainable as Pareto-better-or-equal," and nothing changed, there is nothing to escalate here.

## 4. Smoke evidence (requirement 6)

```
$ bin/efgr "test/2026 ICCAD_case.case0.csv" /tmp/t6_case0.cfg --engine hybrid --iters 2000 --seed 1
engine=hybrid iters=2000 exact_evals=1504 blocks=7 bundles=11 channels=8 routes=11 score=1.4104e+07 outline_area=5.45689e+06 wirelength=1.10954e+07 channel_overflow=0 feedthrough_mismatch=0 open_routes=0 validation=ok

$ bin/efgr "test/2026 ICCAD_case.case1.csv" /tmp/t6_case1.cfg --engine hybrid --iters 2000 --seed 1
engine=hybrid iters=2000 exact_evals=1649 blocks=14 bundles=82 channels=25 routes=83 score=1.33163e+12 outline_area=7.8373e+06 wirelength=5.96773e+07 channel_overflow=1.33131e+06 feedthrough_mismatch=0 open_routes=0 validation=ok
```

Both exit 0, `open_routes=0`, `validation=ok`. Additional determinism spot-check: ran case0 twice with
the same seed and `diff`'d the two `.cfg` outputs — byte-identical (requirement 7).

These are sanity points only (per the task — the full 5x3 sweep is T7's job, not this task's).

## 5. Build + full suite results

```
make clean && make          -> warning-clean (-Wall -Wextra -pedantic), exit 0
bash test/run_case00.sh                        -> exit 0 (7 blocks, OUTLINE/BLOCK/CHANNEL/PATH present)
bash test/run_placement_engine_regressions.sh   -> placement_engine_regression: ok
bash test/run_20260618_regressions.sh           -> regression_20260618: ok
bash test/run_20260608_regressions.sh           -> regression_20260608: ok
bash test/run_p0_regressions.sh                 -> P0 regressions passed
bash test/run_p1_regressions.sh                 -> P1 regressions passed
bash test/run_p2_regressions.sh                 -> P2 T0+T1+T2+T3+T4+T5+T5.1+T6 regressions passed (23 tests, incl. new Rider A)
bash test/run_p3_regressions.sh                 -> P3 regressions passed
```

All 8 suites green.

## 6. Self-review

- Files touched: `src/routing.hpp` (the flip, doc comment), `src/eval.cpp` (include + 2-line constant
  substitution), `src/routing.cpp` (Rider B warning comment only — no behavior change),
  `test/regression_p2_20260707.cpp` (Rider A fixture + test + main() registration).
- No architectural changes, no unrelated refactors. The negotiation loop, comparison gate, adaptive
  chunking, and cost-unit edge pricing are all untouched — this task only flips the default and wires
  up the constant/comment hygiene items the T4/T5.1 reviews flagged.
- `CLAUDE.md` and `.superpowers/` were not touched by me (CLAUDE.md's existing modification predates
  this task and is not part of this commit).
- Verified with a real mutation (§2.1) that Rider A is discriminating, not vacuous — the TDD
  "honesty check" the brief demanded for a test that can't naturally RED.
- Double-checked (§3) that the "zero assertions changed" outcome is genuine (threshold-style
  assertions throughout, not blind fixture updates or tolerant catches).

## 7. Concerns

- None blocking. One observation for the record: `routeProblem`'s comparison gate now runs the legacy
  two-pass router unconditionally on **every** production `exactScore` call (the T5.1 amendment's
  accepted ~2x routing cost), so search/respace exact-eval passes now always pay both a negotiated
  cost-driven route and a legacy route per call. This was already an accepted, documented trade-off
  from T5.1 (against the "45-300x contest-budget headroom" measured then) — T6 does not change that
  trade-off, just makes it the default path instead of an opt-in one exercised only by
  `regression_p2_20260707.cpp`. Worth keeping an eye on wall-clock at T7's full sweep, but out of this
  task's scope to address.
