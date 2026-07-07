# P2 Cost-Driven Routing Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace feasibility-driven two-pass routing with cost-driven routing whose edge weights are actual ΔCost (µm² + α·µm·nets), so channel-vs-FT is priced rationally (official 2.4× example) and flow steers off axis-locked saturated channels — relieving the 10/15 sweep rows P3 could not move.

**Architecture:** Per `.superpowers/sdd/p2-design.md` (canonical; each task names its sections). Prerequisite T0 fixes the units incoherence (overflow charged in nets, FT mismatch in µm², both ×1e6 → search-phase FT aversion ~22×, Codex-verified). Then: pure cost-unit edge functions → cost-driven Dijkstra → adaptive bottleneck chunking → PathFinder-lite negotiation → feasibility fallback (never emit open) → default-on → sweep.

**Tech Stack:** C++17, make, shell regressions. New tests in `test/regression_p2_20260707.cpp` + `test/run_p2_regressions.sh` (mirror p3 pair incl. src list).

## Global Constraints

- All routing weights/penalties in **Cost units**: area-equivalents in µm², wirelength as α·µm·nets. Named constants (model.hpp or routing.cpp top), no magic numbers.
- Overflow area-denomination (T0, axis-crossing semantics): horizontal overflow (usage vs 25·h) → missing height `(usage−cap)/25` × channel **width** as span; vertical overflow (usage vs 25·w) → missing width × channel **height**. Sum per channel per direction.
- Marginal pricing rule (Codex caveat): router edges price the **increment** `Δpenalty(usage → usage+n)`, not total-demand area; the official 2.4× full-area comparison is a scorer-level fact, not the edge formula.
- FT edge pricing: incremental `ftRequiredArea(base, ftExtend(acc+n,…)) − ftRequiredArea(base, ftExtend(acc,…))` against the block's placed area slack (free while placed ≥ required); tier boundaries 3000/6000/9000 must price the jump.
- Direct block-abut edges stay 0 (QA A31). Open routes NEVER emitted: cost-driven pass falls back to the legacy two-pass for any chunk left open (T5); exit-code semantics unchanged.
- `routeProblem` stays a pure deterministic function of (problem, placement): no statics/globals; negotiation history is call-local; bundle order nets-desc + index tie-break; PQ deterministic.
- Rollout is comparison-gated (Pareto-safe): until T6, the new router runs behind a flag/comparison and the better-scoring result (legality-first) is kept; T6 flips default only with sweep evidence.
- Determinism gate: byte-identical .cfg across two identical runs, asserted per task where routing changes.
- Warning-clean -Wall -Wextra -pedantic; Conventional Commits + trailer `Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>`; all shell in WSL; edit via UNC; never commit CLAUDE.md; every task ends with make + all suites (p2 new, p3, p1, p0, four legacy) green.

---

### Task 0: area-denominate channel overflow (design §8.1 R0 — prerequisite, scoring-model change)

**Files:** `src/routing.cpp` (overflow accounting ~222-225), possibly `src/model.hpp` (comment/constant), tests: new p2 pair + updates to any test asserting overflow magnitudes (grep all test/*.cpp for channel_overflow).
- [ ] RED: unit test — channel 80µm wide, span 610µm, 3600 nets vertical usage ⇒ `channel_overflow ≈ (3600−2000)/25 × 610 = 39,040 µm²` (current code yields 1600). Second fixture for horizontal direction (span = width). Official-example comparability assert: FT mismatch for the same flow (base 372,949, tier 40%) within ~1.1× of that overflow area.
- [ ] Implement per Global Constraint 2. Audit + update dependents: respace `channelDemands` uses usage/25 directly from routing usage fields (UNAFFECTED — verify by test), P3 case1 tests assert overflow>0 (still true, magnitude changes — relax exact-magnitude asserts only), paydown/sweep preference rules are relative (unaffected).
- [ ] GREEN + all suites + note in commit body that all recorded sweep numbers re-baseline at T7. Commit `fix: denominate channel overflow in area units`.

### Task 1: pure cost-unit edge functions (design §Q1)

**Files:** `src/routing.cpp` (file-local helpers) or new `src/route_cost.hpp` (header-only, mirrors ft.hpp pattern — implementer's choice, note it). Tests in p2 file.
- [ ] RED: `channelEdgeCost(channel, dir, usage, n, α, dist)` — slack channel ⇒ α·n·dist only; saturated ⇒ + 1e6 × marginal area (per T0 denomination); `softFtEdgeCost(block, acc, n, placed_area, α, dist)` — below-slack free; tier-jump priced (fixture at acc=2900, n=200 crossing the 3000 boundary); direct-abut ⇒ 0.
- [ ] Implement pure functions. GREEN + suites + commit `feat: cost-unit routing edge functions`.

### Task 2: cost-driven Dijkstra pass (design §Q1-Q2 core)

**Files:** `src/routing.cpp` `routeChunk` — replace heuristic weights (center-manhattan + nets·0.05 + 10000·usage/cap) with T1 functions; single unified pass (soft blocks always allowed, priced); keep guiding-point WL accounting unchanged.
- [ ] RED: fixture where a saturated channel and an abutting soft block both connect source→target ⇒ router picks FT once channel saturates (assert soft_feedthrough_nets>0 and overflow lower than channel-forced route); zero-WL abut fixture still routes direct.
- [ ] Implement behind comparison gate (route both old+new per bundle set? too heavy — gate at routeProblem level: env/flag param defaulting OFF, exactScore keeps old path until T6; the p2 tests call the new path explicitly). GREEN + suites + commit `feat: cost-driven routing pass`.

### Task 3: adaptive bottleneck chunking (design §Q3)

**Files:** `src/routing.cpp` bundle loop (~209-217).
- [ ] RED: bundle 3000 nets, path channel slack 1000 nets ⇒ chunks split ≈(1000, rest...) filling slack then diverting; kMaxChunks=16 bound; PATH-per-chunk output preserved (io writes one line per route).
- [ ] Implement greedy split-at-bottleneck (new-router path only). GREEN + suites + commit `feat: adaptive bottleneck chunking`.

### Task 4: negotiation rounds (design §Q2)

**Files:** `src/routing.cpp` routeProblem (new-router path).
- [ ] RED: two bundles contending one narrow channel with an alternate slack path ⇒ after ≤3 negotiation rounds total overflow strictly below round-1 (history cost diverts the later/lighter bundle); deterministic across runs.
- [ ] Implement per-direction call-local history cost, rounds ≤3, best-round-wins by (open==0, then total cost). GREEN + suites + commit `feat: negotiated rip-up reroute`.

### Task 5: feasibility fallback — never open (design §Q5/T5)

**Files:** `src/routing.cpp`.
- [ ] RED: fixture where cost-driven pass leaves a chunk open (constructible: all finite-cost edges port-blocked... if not constructible, characterization: assert fallback path exists by code inspection test calling it directly) ⇒ legacy two-pass reroutes it; open_routes==0 whenever legacy would achieve it.
- [ ] Implement. GREEN + suites + commit `feat: feasibility fallback for cost-driven routing`.

### Task 6: default-on + integration (design §T6)

**Files:** `src/routing.cpp` (flip default), `src/eval.cpp` (exactScore uses it), remove/retire comparison gate.
- [ ] Full suites; 4-row spot sweep (case1/case3 × bstar/hybrid) must be ≥ baseline by legality-first-then-value; determinism byte-identical ×2.
- [ ] Commit `feat: cost-driven routing by default`.

### Task 7: P2 sweep + docs re-baseline

- [ ] 15-row sweep (iters 2000 seed 1). Append `## Appendix: P2 cost-driven routing results (2026-07-07)` to the strategy doc: table + deltas vs P3 appendix + NOTE that T0 re-denominated overflow (P3-era overflow numbers not directly comparable — state the conversion) + honest findings (which axis-locked rows converted overflow→FT, which remain demand-bound) + mark §4 P2 status.
- [ ] All suites + commit `docs: record P2 cost-driven routing results`.

## Self-Review
- Design coverage: R0→T0; Q1→T1/T2; Q3→T3; Q2→T4; never-open→T5; rollout→T2-gate+T6; evidence→T7. Codex caveat encoded as Global Constraint 3 (marginal pricing).
- Sensitive dependents of T0 enumerated (respace demand math independent of overflow units — uses usage fields directly; test-magnitude asserts; relative preference rules safe).
- No placeholders; fixtures concrete; rollout Pareto-safe throughout.
