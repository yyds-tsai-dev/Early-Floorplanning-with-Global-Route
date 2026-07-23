# P2 Task 5.1 (amendment) — full comparison gate for cost-driven routing

## Summary

Implemented the binding design's full comparison gate (`p2-design.md` §9 T5 preamble: "`routeProblem`
returns the cost-driven result when it is fully routed and no worse, else the fallback"), replacing
T5's narrower open-routes-only fallback. Along the way, found and fixed a pre-existing correctness bug
in T5's `runLegacyPass` that would have silently defeated the gate.

## Files changed

- `src/model.hpp` — added `RoutingResult::used_fallback` (telemetry, default `false`).
- `src/routing.cpp` — the gate itself, plus the `force_legacy` fix (see below).
- `test/regression_p2_20260707.cpp` — new fixture/tests for the gate, updated T5 dead-end fixture's
  assertions for its new (correct) tie-break outcome, extended the T4 negotiation test.

## Implementation

### 1. The gate (`src/routing.cpp`, end of `routeProblem`)

Replaced:
```cpp
if (best.open_routes > 0) {
    runLegacyPass(result);
    return result;
}
return best;
```
with an unconditional legacy run (exactly once) and a full lexicographic comparison using the SAME
`roundCost` key already used for negotiation round selection — `(open_routes, alpha*WL +
kOverflowAreaWeight*overflow + kFtMismatchWeight*mismatch)`:
```cpp
runLegacyPass(result);
if (roundCost(best) <= roundCost(result)) {
    return best;
}
result.used_fallback = true;
return result;
```
`<=` implements "ties go to cost-driven" per controller resolution #1. `cost_driven=false` is
untouched (early-returns before this code, single `runLegacyPass` call, `used_fallback` stays at its
default `false`).

### 2. Pre-existing bug found and fixed: `force_legacy`

While validating the gate empirically (see below), `runLegacyPass(result)` called from *within* a
`cost_driven=true` invocation turned out **not** to run the true legacy algorithm at all.
`findRoutePath` dispatches on the *outer* `cost_driven` function parameter (captured by reference,
fixed for the whole call), not a per-call flag — so `runLegacyPass`, when invoked from the
`cost_driven=true` branch (as both T5's original fallback and my new gate do), silently reused
cost-driven's own single-pass pricing (`allow_soft` always true, `channelMarginalCost`/
`softFtMarginalCost` edge weights) with fixed `ceil(nets/1000)` chunking, instead of the real
two-pass channels-then-soft-fallback legacy algorithm with its `10000*nets*(usage/cap)` congestion
heuristic. This bug predates this task (T5's original fallback had it too) but was invisible because
the T5 dead-end fixture's topology doesn't distinguish the two algorithms.

Fixed by threading an explicit `force_legacy` parameter through `findRoutePath` → `routeChunk`,
defaulting to `false` (preserves every existing call site's behavior — the top-level
`cost_driven=false` caller already had `cost_driven=false`, and the negotiation loop never passes it).
`runLegacyPass` now calls `routeChunk(rr, bundle, nets, /*force_legacy=*/true)`, guaranteeing true
legacy pricing regardless of the outer flag. Without this fix, the gate would have compared
cost-driven against a mislabeled copy of itself, making the whole amendment vacuous.

`runLegacyPass` still executes **at most once** per `routeProblem` call in every path (the
"share one legacy run" resolution from the escalation clause) — I removed the old conditional
`if (best.open_routes > 0)` guard entirely rather than adding a second call site.

## TDD evidence

### RED (before implementing the gate)
Ran `test/run_p2_regressions.sh` immediately after adding the gate skeleton and `used_fallback` field:
```
p2_regression_bin: test/regression_p2_20260707.cpp:840: void {anonymous}::testFeasibilityFallbackNeverWorseThanLegacyOnGenuineDeadEnd(): Assertion `cd_dead->nets == legacy_dead->nets' failed.
```
This is the expected RED: the T5 dead-end fixture is an *exact tie* under the new key (both bundles'
`open_routes`/WL/overflow/mismatch numerically identical between legacy and cost-driven), so per the
tie rule the gate now returns cost-driven's own result instead of legacy's, flipping the old
wholesale-substitution assertion (`cd_dead->nets` becomes cost-driven's own convention, 10, not
legacy's default 0). Verified via a scratch probe (not just the assertion failure) that this is a
genuine, hand-computable tie: `legacy: open=1 wl=200.0 overflow=0 mismatch=0` vs
`cd: open=1 wl=200.0 overflow=0 mismatch=0` — identical.

### Constructing fixture (a) — a genuine "cost-driven fully routed but loses" case
This was the hard part. Single-bundle scenarios provably cannot lose (the marginal-cost comparison at
a virgin resource exactly reproduces legacy's own cost for that resource, so cost-driven always picks
whichever is cheaper, including "behave like legacy"). Multi-bundle attempts via FT tier-boundary
reservation effects also resolved in cost-driven's favor (diverting one bundle to FT always frees the
channel for the other, for free). The winning construction exploits a **documented** gap already
called out in `routing.cpp`'s own comments: `findRoutePath`'s cost-driven pricing charges channel
usage only on the entry axis of a hop, while `commitRoutePath`'s accrual charges *both* axes for an
"elbow" path (enters a channel on one axis, exits on the orthogonal axis). Legacy's cruder heuristic
(`10000*nets*(horizontal_usage/hcap + vertical_usage/vcap)`) sums both axes' current usage regardless
of entry direction, so it correctly avoids the elbow; cost-driven doesn't see the risk and takes it.

Constructed geometry (`ElbowMispricingFixture`, `test/regression_p2_20260707.cpp`): a straight vertical
filler bundle (900 nets) fills a narrow channel CH1 (`vcap=1000`) to 900 vertical usage; a second
bundle (300 nets) from a block beside CH1 can reach the same target either through CH1 via a
horizontally-entering elbow (mispriced as free) or via an uncongested channel CH4 above it (legacy's
choice). Verified empirically via scratch probes (`/tmp/probe_a*.cpp`, compiled directly against
`src/routing.cpp`) before transcribing into the test:
- cost-driven (`best`, stable across all 3 negotiation rounds): `open=0, wl=648000, overflow=4880 um^2,
  mismatch=0` → key = `0.001*648000 + 1e6*4880 = 4,880,000,648`
- legacy: `open=0, wl=646500, overflow=0, mismatch=0` → key = `646.5`

Gate correctly switches to legacy (`used_fallback=true`) despite `open_routes==0` on both sides —
exactly the gap between T5's original fallback and this amendment's full gate.

### GREEN
```
testVerticalOverflowIsAreaDenominatedByChannelHeight ok
...
testNegotiationClearsRoundZeroOverflow ok (overflow=0)
testNegotiatedRoutingDeterministic ok
testFeasibilityFallbackNeverWorseThanLegacyOnGenuineDeadEnd ok (open_routes=1)
testComparisonGatePrefersLegacyWhenCostDrivenFullyRoutedButWorse ok (legacy_wl=646500, legacy_overflow=0)
testComparisonGateDeterministic ok
P2 T0+T1+T2+T3+T4+T5+T5.1 regressions passed
P2 regressions passed
```

## Full gate

`make clean && make`: clean, warning-free (`-Wall -Wextra -pedantic`).

All 8 suites green:
```
PASS: case00
PASS: placement_engine
PASS: r20260618
PASS: r20260608
PASS: p0
PASS: p1
PASS: p2
PASS: p3
```

## Test changes in detail (`test/regression_p2_20260707.cpp`)

1. **New fixture `ElbowMispricingFixture` + `testComparisonGatePrefersLegacyWhenCostDrivenFullyRoutedButWorse`**
   — requirement (a). Asserts `open_routes==0` on the returned result, `used_fallback==true`, and
   wholesale bit-for-bit equality with a direct legacy call.
2. **`testFeasibilityFallbackNeverWorseThanLegacyOnGenuineDeadEnd`** (existing T5 fixture) — updated to
   match the new, correct tie-break outcome: added `assert(!cd.used_fallback)` (ties go to
   cost-driven), and flipped the final "wholesale substitution" check to assert
   `cd_dead->nets == 10` (cost-driven's own open-route convention) and `legacy_dead->nets == 0`,
   with comments explaining why this is the documented deterministic outcome, not a regression.
3. **`testNegotiationClearsRoundZeroOverflow`** (existing T4 fixture) — added
   `assert(!r.used_fallback);` per requirement (c) — the gate must not flip this fixture's win.
4. **New `testComparisonGateDeterministic`** — two calls on the same fixture, checks
   `used_fallback` and all aggregate fields match (GC6 purity, same pattern as every other P2 fixture).

## Self-review / concerns

- The `force_legacy` fix is outside the literal task brief but was necessary: without it the gate
  compares cost-driven against a mislabeled copy of itself, which is not a comparison gate at all. I
  judged this squarely inside "restructure minimally so legacy runs at most once" from the escalation
  clause, since it's the same code path and same correctness property (does `runLegacyPass` actually
  mean "legacy"). Flagging it prominently here since it also silently affected T5's original fallback
  (now fixed as a side effect) — worth a reviewer's explicit sign-off given it changes behavior in any
  fixture where "legacy pricing" vs "cost-driven pricing with fixed chunking" actually differ (the
  existing T5 dead-end fixture didn't differ; my new fixture does, by construction).
- `roundCost` is a `std::pair<int,double>`; `<=` on `std::pair` is lexicographic, matching the intended
  tie rule exactly — no separate tie-break code needed.
- Runtime cost: legacy now always runs once per `cost_driven=true` call (previously only on open
  routes). Per the task's resolution #5, accepted against the 45–300x contest-budget headroom.
- Did not rename `testFeasibilityFallbackNeverWorseThanLegacyOnGenuineDeadEnd` despite its behavior
  changing (tie now resolves to cost-driven, not legacy substitution) — the invariant the name
  describes ("never worse than legacy") still holds; only the *mechanism* proving it changed. Renaming
  felt like scope creep beyond what was asked.
