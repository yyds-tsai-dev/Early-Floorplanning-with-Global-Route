# P3 Demand-Driven Channel Re-Spacing Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Convert P1's tight-core packings into routable-tight packings by widening exactly the deficient inter-core strips (channel width = demand/25), collapsing the `1e6 × channel_overflow` term that dominates Cost on cases 1-4 (see the P1 addendum in docs/research/2026-07-06-cost-reduction-strategy.md).

**Architecture:** Per the approved design in `.superpowers/sdd/p3-design.md` (canonical algorithm spec — each task names its governing sections). New module `src/respace.hpp/.cpp`: read per-channel directional usage from the routed result, map channel deficits to pairwise gap demands (AXIS-CROSSING INVARIANT: horizontal overflow ⇒ vertical re-spacing, vertical overflow ⇒ horizontal re-spacing), relax two longest-path constraint graphs over core blocks (+walls), re-anchor edge blocks via P1's shrinkWrapOutline/anchorAtOutline, re-route; ≤3-iteration monotone-widen fixed point; hook into the elite pass and finalizeFeedthrough only (never the SA hot loop). Pareto-safe: adopt only when legal AND better by `outlineSweepBetter` discipline.

**Tech Stack:** C++17, make (src/*.cpp auto-compiled), shell regressions.

## Global Constraints

- Axis-crossing invariant (design decision 2 — the crux): a channel's HORIZONTAL usage is capacitated by its HEIGHT (25·h) ⇒ relieve by y-direction re-spacing; VERTICAL usage by WIDTH (25·w) ⇒ x-direction re-spacing. Never consume `Channel::usage` (reporting aggregate only).
- Absolute-gap semantics: each channel charges w_req = usage/25 (per direction) as an absolute min-gap on its separating block pair; multiple channels on one pair ⇒ max.
- Core blocks: relative order frozen from current coordinates; longest-path relax only increases coordinates (monotone). Edge blocks are NOT graph nodes — re-anchored afterward via existing P1 machinery. Core rects otherwise never re-ordered.
- Bounded: result must satisfy declared (W,H) ≤ MAX; infeasible total demand ⇒ proportional slack distribution + residual graded overflow (never illegal, never open).
- Every adopted result gated by `placementLegalAt`-equivalent + `validateSolution().ok` + `open_routes==0`-preferred (`outlineSweepBetter` rule). Fallback = keep incumbent (strict Pareto).
- Determinism (no RNG, fixed iteration order); `placementKey` already covers rects+stamp ⇒ cache sound.
- Hot-loop ban: respace requires a routed placement — elite pass + finalizeFeedthrough only.
- New files `src/respace.hpp/.cpp` (Makefile picks up src/*.cpp automatically); tests in NEW `test/regression_p3_20260707.cpp` + `test/run_p3_regressions.sh` (mirror the p1 pair's g++ src list PLUS src/respace.cpp; also ADD src/respace.cpp to any legacy runner that links search.cpp once search.cpp calls respace — check test/run_*.sh link lines).
- Build warning-clean -Wall -Wextra -pedantic; Conventional Commits + trailer `Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>`; run all shell in WSL; edit via UNC; every task ends with `make` + all suites (p3, p1, p0, four legacy) green.

---

### Task 1: channelBounds + channelDemand (pure demand model) — design T1, decisions 1-3

**Files:** Create `src/respace.hpp`, `src/respace.cpp`, `test/regression_p3_20260707.cpp`, `test/run_p3_regressions.sh`.
**Interfaces →:** `struct StripDemand { int left = -1, right = -1, below = -1, above = -1; double need_w = 0.0, need_h = 0.0; };` and `std::vector<StripDemand> channelDemands(const Problem&, const Placement&, const RoutingResult&);` — for each channel with usage > 0, geometrically derive bounding core-block indices (or wall = -1) and directional needs `need_w = vertical_usage/25`, `need_h = horizontal_usage/25`.
- [ ] Failing tests FIRST pin the axis-crossing invariant: hand-built 2-block problem with a routed vertical-usage channel between them ⇒ demand appears as `need_w` on the (left,right) pair; horizontal-usage fixture ⇒ `need_h` on (below,above). RED = missing module.
- [ ] Implement per design decisions 1-3. GREEN + all suites + commit `feat: add channel demand model for respacing`.

### Task 2: constraint-graph relax (pure) — design decision 4

**Files:** Modify `src/respace.hpp/.cpp`; tests in p3 file.
**Interfaces →:** `bool relaxToDemand(const Problem&, Placement&, const std::vector<StripDemand>&);` — build H/V graphs over core blocks + walls (order from current coords), min-gap edges from demands (max-merged per pair), longest-path re-coordinate (monotone increase only), reject/partial-scale when exceeding MAX (proportional slack per design failure-mode 1). Core order preserved by construction; edge blocks untouched here.
- [ ] Failing tests: 3-block row with a `need_w` demand between A,B ⇒ B (and C) shift right exactly by the deficit; demand exceeding MAX ⇒ partial widening, coordinates ≤ MAX, function still returns true with residual flagged (return value semantics per design). Order-freeze test: no pair swaps under any demand.
- [ ] Implement. GREEN + suites + commit `feat: longest-path respacing over core blocks`.

### Task 3: respaceToDemand single pass (integrate + legality gate) — design T3

**Files:** Modify `src/respace.hpp/.cpp`; tests in p3 file.
**Interfaces →:** `bool respaceToDemand(const Problem&, Placement&, const RoutingResult&);` — demands → relax → reset stamp → `shrinkWrapOutline` (re-anchor edges + fresh stamp) → full legality (placementLegalAt-equivalent via the anchor result + containment). On any failure: restore input placement, return false.
- [ ] Failing tests: case1 bstar seed1 decode+route (known overflow) ⇒ respaceToDemand true, placement legal (validateSolution ok vs its stamp), stamped area grew, core order preserved; failure-path test (impossible demand fixture) ⇒ placement byte-identical to input.
- [ ] Implement. GREEN + suites + commit `feat: demand-driven respace single pass`.

### Task 4: route↔respace fixed point (≤3 iters, monotone) — design decision 5

**Files:** Modify `src/respace.hpp/.cpp`.
**Interfaces →:** `struct RespaceResult { Placement placement; RoutingResult routing; Score score; bool improved = false; };` `RespaceResult respaceFixedPoint(const Problem&, const Placement&, const ExactCachedFn&);` — iterate: route (via exactCached), demands, respace, re-route; keep best-legal by `outlineSweepBetter`; stop when overflow == 0, no demand growth, or 3 iters. Monotone-widen within the loop.
- [ ] Failing test: case1 fixture ⇒ fixed point reduces `channel_overflow` vs the un-respaced route (strict), `open_routes == 0`, deterministic across two runs.
- [ ] Implement. GREEN + suites + commit `feat: route-respace fixed point loop`.

### Task 5: elite-pass hook — design T5 (first Cost-moving task)

**Files:** Modify `src/search.cpp` (elite loop: after outlineSweep, run respaceFixedPoint on the swept winner; adopt via `outlineSweepBetter`); add `#include "respace.hpp"`; extend legacy runner link lines with src/respace.cpp where search.cpp is linked.
- [ ] Failing test: case1 bstar seed1 iters 500 end-to-end ⇒ final `channel_overflow` strictly below the pre-hook value (measure baseline in-test by scoring the same placement without respace), validation ok, open 0. Determinism byte-identical.
- [ ] Implement. GREEN + suites + commit `feat: respace elites to demand`.

### Task 6: finalizeFeedthrough hook — design T6

**Files:** Modify `src/search.cpp` (inside finalize: respaceFixedPoint on the inflated re-decode before tryCommit; acceptance rule unchanged — paydown + open==0 + validateSolution).
- [ ] Failing test: reuse the T5-era FT-forced fixture (see test/regression_p1_20260706.cpp finalize tests) with a congested variant ⇒ finalize commits with mismatch 0 AND overflow reduced vs no-respace.
- [ ] Implement. GREEN + suites + commit `feat: respace inside feedthrough finalize`.

### Task 7: P3 sweep + docs

- [x] Full 15-row sweep (5 cases × sp/bstar/hybrid, iters 2000, seed 1); appended `## Appendix: P3 demand-respacing results (2026-07-07)` to the strategy doc with the table + comparison lines vs the P1 appendix. Actual outcome differs from the original expected-per-design note above: 5/15 rows moved (case1 all engines −12% to −57% score/overflow; case2 bstar/hybrid −25% to −30%); the other 10 rows (case2/sp, all of case3, all of case4) are byte-identical to P1 — those layouts are axis-locked (core x-extent pinned at MAX per the T4 report), so spacing alone cannot relieve their overflow. Marked §4 P3 "done with findings" (not overclaimed) and cross-referenced P2 as the strategic next phase for the axis-locked rows.
- [x] All suites + `make` + commit `docs: record P3 demand-respacing results`.

## Self-Review
- Design coverage: decisions 1-6 → T1(demand+invariant) T2(graphs) T3(integration+gate) T4(fixed point) T5/T6(hooks) T7(evidence). Failure modes (infeasible demand, order-flip, illegal-emit) each have a dedicated test.
- The axis-crossing invariant is pinned by T1's FIRST tests — the design names it highest-risk; nothing downstream lands until those are green.
- No placeholders: algorithms live in p3-design.md; tasks reference sections; test fixtures and assertions are concrete.
