# P2 Task 5 report — feasibility fallback (never-open guarantee)

## What was implemented

### Riders (commit `0587b16 docs(routing): T4 review comment minors`)

1. `src/routing.cpp` (negotiation loop's zero-bump early-exit comment, ~line 557): softened the
   "provably identical" claim. It now distinguishes two cases: history all-zero going into the round
   → later rounds provably identical (unchanged); history nonzero but this round bump-free → later
   rounds provably **not better** for `best` (best already holds an overflow-free round; a further-
   decayed round can only tie, kept by the earlier-round-wins rule, or reintroduce overflow).
2. `test/regression_p2_20260707.cpp` (T4 negotiation fixture's hand-derivation comment, ~line 610):
   corrected the round-1 surcharge arithmetic. History bumps to 0.3 after round 0, but round 1 first
   *decays* it (`kHistoryDecay=0.5`) before pricing, so the live value is 0.15 and the surcharge used
   is `kOverflowAreaWeight * 0.15 * 900 = 1.35e8`, not the undecayed `0.3 → 2.7e8`. Assertions were
   already correct; only the prose numbers were wrong.

### T5 feasibility fallback (commit below)

`src/routing.cpp`:
- Extracted the legacy two-pass routing loop (fixed `ceil(nets/1000)` chunking via `routeChunk`, then
  `computeOverflowAndMismatch`) into a shared lambda `runLegacyPass(RoutingResult& rr)`, placed just
  before the `if (!cost_driven)` branch. The `!cost_driven` branch now just calls
  `runLegacyPass(result)` — same logic, same order, only `rr` substituted for `result` inside the
  lambda body (verified byte-identical below).
- At the end of the negotiation loop (after the `for (int round ...)` loop, replacing the bare
  `return best;`): if `best.open_routes > 0` (every negotiation round left at least one route open),
  call `runLegacyPass(result)` and return `result` instead of `best`. `result` was initialized at the
  top of the function (`result.channels`/`result.soft_feedthrough_nets`) and is never mutated by the
  negotiation loop (which only ever writes its own per-round `round_result` copies) — confirmed by
  `grep -n 'result\.' src/routing.cpp`, which shows zero writes to `result` between initialization and
  the fallback.

**Scope decision (documented, not escalated):** I implemented the narrower, literal reading of the
brief — fall back **only** when `best.open_routes > 0**, not a blanket "always compute legacy and pick
the cheaper of the two by cost" gate. Rationale:
- The brief's two explicit test bullets are exactly "cost-driven can route → cost-driven used" and
  "pathological open case → fallback engages, open_routes==0"; neither describes a "fully-routed but
  higher-cost → fall back anyway" scenario.
- I proved by code inspection (and the RED/GREEN cycle confirms empirically) that `findRoutePath`'s
  reachability gates (`allowed`/`endpointEdgeAllowed`) are **identical** between cost_driven's single
  pass (`allow_soft` always true) and the legacy two-pass router's pass 1 (`allow_soft` true), with all
  edge costs non-negative on both sides. Dijkstra reachability therefore cannot differ: a bundle
  unroutable by cost-driven is, by construction, equally unroutable by legacy, and vice versa. So
  "legacy succeeds where cost-driven's own reachability fails" is **not constructible** — matching the
  brief's own hedge ("if not constructible, characterization…").
- A blanket cost-comparison gate (recompute legacy every call, pick min-cost) risks silently
  overriding the T2–T4 fixtures that were specifically built to demonstrate cost-driven **winning**
  (e.g. `testCostDrivenPrefersFtOverSaturatedChannel`, `testNegotiationClearsRoundZeroOverflow`'s
  400/900 channel split). I did not want to gamble those tests' continued validity on an unverified
  wider mechanic when the brief's literal scope doesn't require it. The narrower gate leaves all
  existing cost-driven-wins fixtures completely untouched (verified: unchanged pass/fail, unchanged
  logged values) while still delivering the hard "never worse on open-route count" guarantee.

If the orchestrator wants the fuller "always compare cost, pick the better regardless of open-route
status" comparison gate (the design's §9 preamble phrase "picks the better... regression is impossible
by the comparison gate" reads that way in isolation), that is a natural, additive follow-up — happy to
implement it as a fast-follow with the existing test suite as the safety net if so directed.

## TDD evidence

**RED** (fixture in place, fallback not yet implemented):
```
$ bash test/run_p2_regressions.sh
...
p2_regression_bin: test/regression_p2_20260707.cpp:840: void {anonymous}::testFeasibilityFallbackNeverWorseThanLegacyOnGenuineDeadEnd(): Assertion `cd_dead->nets == legacy_dead->nets' failed.
test/run_p2_regressions.sh: line 13: 42526 Aborted   ./test/p2_regression_bin
```
This is a genuine RED, not a trivial no-op: `legacy.open_routes==1`, `cd.open_routes==1`, and every
*aggregate* field (`total_wirelength`, `channel_overflow`, `feedthrough_mismatch`, `routes.size()`)
already coincidentally matched between the plain (un-fallback'd) cost-driven negotiation result and
legacy for this fixture — because the OK bundle's path is unique and the DEAD bundle contributes zero
wirelength either way. The one field that actually differs is `Route.nets` on the DEAD bundle's open
route: legacy's `findRoutePath` intentionally leaves it at the struct default `0` (comment: "route.nets
is intentionally NOT set here"), while the negotiation loop's own exhausted-probe fallthrough
explicitly sets `probe.nets = remaining` (10) before pushing it. This decisively proves the
pre-fallback `cd` result is *not* legacy's result, even though the coarser assertions couldn't tell.

**GREEN** (after implementing the fallback):
```
$ bash test/run_p2_regressions.sh
...
testFeasibilityFallbackNeverWorseThanLegacyOnGenuineDeadEnd ok (open_routes=1)
P2 T0+T1+T2+T3+T4+T5 regressions passed
P2 regressions passed
```
All prior T0–T4 tests in the same run are unchanged (`testCostDrivenPrefersFtOverSaturatedChannel`,
`testNegotiationClearsRoundZeroOverflow`, etc. — same logged values as before the T5 change),
confirming the narrower open-routes-only gate doesn't perturb any cost-driven-wins fixture.

## New test added

`test/regression_p2_20260707.cpp`: `testFeasibilityFallbackNeverWorseThanLegacyOnGenuineDeadEnd` +
its `DeadEndFixture`/`makeDeadEndFixture()` helper.

**Why a "characterization" test, not a "legacy succeeds / cost-driven fails" test:** the brief allows
this explicitly ("if not constructible, characterization: assert fallback path exists by code
inspection test calling it directly"). As derived above, the reachability-equivalence between
cost-driven's single pass and legacy's pass 1 makes "legacy succeeds where cost-driven natively fails"
impossible to construct with the current code. Instead the fixture builds a genuine, port-blocked dead
end (unroutable by **both** legacy and cost-driven — the brief's own suggested construction: "all
finite-cost edges port-blocked") to exercise the fallback code path (`best.open_routes>0` after all
negotiation rounds) and prove three things:
1. `routeProblem(...,true)`'s `open_routes` is never worse than `routeProblem(...,false)`'s.
2. The fallback substitutes the legacy `RoutingResult` **wholesale** (not a partial/Frankenstein
   merge) — proven via the decisive `Route.nets` check described above, plus matching
   `total_wirelength`/`channel_overflow`/`feedthrough_mismatch`/`routes.size()`.
3. A co-existing, genuinely routable bundle (`A→B`, direct channel) in the same call still routes
   correctly in both — the fallback doesn't collateral-damage healthy bundles.

Fixture layout (single row, height 40, outline 200×40): `A[0,40) — CH[40,60) — B[60,100) —
WALL[100,140) — D_SRC[140,160) — D_TGT[160,200)`. `A`/`B` are channel-connected only via the `[40,60)`
gap. `B`, `WALL`, `D_SRC`, `D_TGT` tile the rest of the row with zero gaps, so `D_SRC`/`D_TGT` form an
isolated pocket reachable only from each other, by direct abut on `D_TGT`'s LEFT edge (`edgeFromTo`
code 1). `D_TGT.port_edges = {2}` (TOP only) excludes that edge code entirely — no traversable edge
into `D_TGT` exists anywhere in the graph, for either router.

## Files changed

- `\\wsl.localhost\ubuntu-24.04\home\yyds\Early-Floorplanning-with-Global-Route\src\routing.cpp`
  - `:557-571` (riders): softened zero-bump early-exit comment.
  - `:417-435` (T5): extracted `runLegacyPass` lambda; `!cost_driven` branch now calls it.
  - `:609-627` (T5): feasibility fallback — `if (best.open_routes > 0) { runLegacyPass(result); return result; }`.
- `\\wsl.localhost\ubuntu-24.04\home\yyds\Early-Floorplanning-with-Global-Route\test\regression_p2_20260707.cpp`
  - `:610-613` (riders): corrected the T4 fixture's decayed-surcharge arithmetic comment.
  - `:716-841` (T5): new `DeadEndFixture`/`makeDeadEndFixture()`/
    `testFeasibilityFallbackNeverWorseThanLegacyOnGenuineDeadEnd`, wired into `main()`.

## Full gate results (all green)

- `make clean && make` — warning-clean (`-Wall -Wextra -pedantic`).
- `bash test/run_case00.sh` — ok (both `bin/efgr` and `bin/EarlyFloorplanning_with_GlobalRoute` runs,
  `open_routes=0 validation=ok` on both).
- `bash test/run_placement_engine_regressions.sh` — `placement_engine_regression: ok`.
- `bash test/run_20260618_regressions.sh` — `regression_20260618: ok`.
- `bash test/run_20260608_regressions.sh` — `regression_20260608: ok` (`open_routes=0 validation=ok`).
- `bash test/run_p0_regressions.sh` — `P0 regressions passed`.
- `bash test/run_p1_regressions.sh` — `P1 regressions passed`.
- `bash test/run_p2_regressions.sh` — `P2 regressions passed` (20/20 tests, including the new T5 test).
- `bash test/run_p3_regressions.sh` — `P3 regressions passed`.

## Self-review

- **Completeness vs brief:** RED fixture (port-blocked construction, exactly as the brief suggests)
  → confirmed genuine failure → implemented → GREEN. Both required test bullets are covered (existing
  T0–T4 tests continuing to pass covers "cost-driven can route → cost-driven used"; the new fixture
  covers "pathological open case → fallback engages").
- **Legacy path (`cost_driven=false`) untouched:** traced via `grep -n 'result\.' src/routing.cpp` —
  `result` is only written by `extractChannels`/`assign` at the top and by `runLegacyPass`; the
  negotiation loop only ever touches its own `round_result` copies. The `!cost_driven` branch's
  behavior is the same loop body, just now inside a lambda parameterized on `rr` instead of closing
  over `result` directly — no logic changed, confirmed by every pre-existing regression suite still
  passing with identical logged numbers (case00, 20260608, 20260618, placement_engine, p0/p1/p3).
- **Determinism:** no RNG, no static/global state; `runLegacyPass` and the fallback branch are pure
  functions of `(problem, placement, cost_driven)` like the rest of `routeProblem`. Existing
  determinism tests (`testCostDrivenDeterministic`, `testAdaptiveChunkingDeterministic`,
  `testNegotiatedRoutingDeterministic`) still pass unchanged.
- **T4 single-round consistency preserved:** when the fallback engages, the returned `RoutingResult`
  is `result` populated **entirely** by one call to `runLegacyPass` — a single coherent computation
  (routes, usage, `total_wirelength`, `channel_overflow`, `feedthrough_mismatch`,
  `soft_feedthrough_nets`, `open_routes` all set together), not a merge of `best` and `result` fields.
  When the fallback does *not* engage, `best` is returned as-is (already T4-verified consistent).
- **Tests assert real behavior:** the RED failure was a genuine assertion failure at a decisive field
  (`Route.nets` on the open route), not a syntax/compile error or a vacuously-true check — confirmed
  by capturing the exact RED output above before writing the fix.

## Concerns

- **Scope interpretation flagged above** (narrower open-routes-only gate vs. a blanket
  always-compare-cost gate) — I judged the narrower reading correct per the brief's literal test
  bullets and to avoid unverified risk to the T2–T4 win-demonstrating fixtures, but the design's §9
  preamble phrasing could support the broader mechanic. Not escalating as NEEDS_CONTEXT since the
  brief's own explicit test list is unambiguous and fully satisfied, and every existing regression
  still passes (Pareto-safety proof, as the brief requires) — but flagging for the orchestrator's
  awareness before T6 wires cost_driven on by default.
- Production callers still pass `cost_driven=false` (T6 not yet done) — this task doesn't change that.
