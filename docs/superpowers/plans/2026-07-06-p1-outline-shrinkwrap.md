# P1 Outline Shrink-Wrap Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make the declared outline a per-candidate solution variable — shrink-wrapped from the decoded placement, edge blocks re-anchored to the shrunk border — so `OutlineArea` becomes a real optimization gradient (today it is constant ≡ MAX), and let `finalizeFeedthrough` grow the outline toward MAX to absorb FT inflation (closing the hybrid case1/case3 residual mismatch).

**Architecture:** Per the approved design in `.superpowers/sdd/p1-design.md` (READ IT — it is the canonical algorithm spec; each task below names its governing sections). Declared outline = two new `Placement` fields stamped by a pure post-decode `shrinkWrapOutline` (exact, grow-until-legal reusing `legalizeEdgeBlocks` at a scoped Problem) and a cheap O(n) `shrinkWrapEstimate` for `cheapScore`. Exact evals route inside the declared outline with a 1-step corrective grow; elites get a 5–7-trial outline sweep; validation/output read the declared outline with MAX as ceiling.

**Tech Stack:** C++17, make, shell regressions under test/.

## Global Constraints

- Build warning-clean under `-Wall -Wextra -pedantic`; C++17; 4-space indent; `namespace efgr`; `lowerCamelCase` functions, `k`-constants.
- Declared outline invariants: `0 < W ≤ MAX_w`, `0 < H ≤ MAX_h`, `W×H ≥ 1`, every rect and every extracted channel ⊆ `[0,W]×[0,H]`. Fallback when unset (0) = problem MAX (today's behavior). Shrink is Pareto-safe: any infeasibility ⇒ fall back toward/at MAX, never emit overlap/open/outline-violation.
- Edge rules on the SHRUNK outline (QA A10/A12/A19/A20/A35.2): flush with declared border; thirds computed from declared W,H; corner 90° pair = AND; parallel multi = touch-one; 1D positive overlap suffices. Reuse `placeEdge`/`firstLegalEdgeSlot`/`legalizeEdgeBlocks`/`edgeRectLegal` at a scoped Problem — write NO new geometry predicates.
- `placementKey` MUST include declared outline (`llround(outline_w*1000)`, `llround(outline_h*1000)`) — stale exact-cache hits across different outlines corrupt scores.
- Out-of-outline penalty in `cheapScore` stays vs MAX (ceiling), NOT the shrunk W,H.
- Determinism: fixed trial points, no new RNG; two runs with the same `--seed` produce byte-identical `.cfg`.
- Concurrent-session guard: another session is changing `eval.cpp` `cheapScore`'s `score.value` composition line to include `score.penalty`. Do not touch that line; if it conflicts on merge, keep their version of the composition and re-apply our `outline_area` sourcing.
- Commits: Conventional Commits + trailer `Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>`. Never commit `bin/`, `*.o`, `*.cfg`.
- Run all shell (make/tests/git) inside WSL at `/home/yyds/Early-Floorplanning-with-Global-Route`; edit via UNC.
- Every task: `make` warning-clean + `bash test/run_p0_regressions.sh` + the four legacy `test/run_*.sh` green before commit. New P1 tests go into a NEW `test/regression_p1_20260706.cpp` + `test/run_p1_regressions.sh` (created in T1, modeled on the p0 pair; link the same src list as run_p0 plus nothing extra).

---

### Task 1: Placement outline fields + placementKey + p1 test harness (design §2 model.hpp, §3.2)

**Files:** Modify `src/model.hpp` (Placement), `src/search.cpp` (placementKey). Create `test/regression_p1_20260706.cpp`, `test/run_p1_regressions.sh`.
**Interfaces → later tasks:** `Placement::outline_w`, `Placement::outline_h` (double, default 0.0 = unset ⇒ callers fall back to problem outline).

- [ ] Failing test in the new p1 test file: two placements identical rects, one with `outline_w=100.0`, other unset ⇒ `placementKey` differs; unset defaults compile and equal today's key format for the same rects.
- [ ] Runner script mirrors `test/run_p0_regressions.sh` (same g++ src list, output `test/p1_regression_bin`; add to .gitignore).
- [ ] RED (field/key missing) → implement: append the two fields to `Placement`; in `placementKey` append `':' << llround(outline_w*1000) << 'x' << llround(outline_h*1000)`.
- [ ] GREEN + all suites + commit `feat: add declared outline fields to Placement and placementKey`.

### Task 2: shrinkWrapEstimate (cheap, geometry-only) (design §1.2.1, §1.2.2, §2)

**Files:** Modify `src/placement_engine.hpp/.cpp`. Test in `test/regression_p1_20260706.cpp`.
**Interfaces → later:** `struct OutlineFit { double w=0.0, h=0.0; bool legal=true; };` and `OutlineFit shrinkWrapEstimate(const Problem&, const Placement&);` (const, no mutation, O(n): per-axis lower bounds W_core/H_core + edge width/height needs + thirds feasibility by monotone probe; clamp to [1, MAX]).
- [ ] Failing tests: (a) parse `test/2026 ICCAD_case.case0.csv`, decode with bstar seed 1 ⇒ estimate.w*h < MAX area AND ≥ core bbox area; (b) synthetic problem with two corner-AND edge blocks at opposite corners sized ~MAX ⇒ estimate == MAX dims; (c) no-edge-block synthetic ⇒ estimate == core bbox (floored at 1).
- [ ] RED → implement per design §1.2.1-1.2.2 (reuse `locSegment`/`rectTouchesLocation`/`edgeRectLegal` via scoped Problem copies for the thirds probe; probe W downward: try candidate = max lower bound, verify each edge block CAN be legally placed on the scoped outline via `firstLegalEdgeSlot` on a trial copy — if any fails, bump that axis 5% + retry ≤6, else MAX).
- [ ] GREEN + suites + commit `feat: add cheap shrink-wrap outline estimate`.

### Task 3: shrinkWrapOutline (exact, re-anchor + grow loop) (design §1.2.3, §1.2.4, §3.1)

**Files:** Modify `src/placement_engine.hpp/.cpp`. Test in p1 test file.
**Interfaces → later:** `OutlineFit shrinkWrapOutline(const Problem&, Placement&, int max_grow_steps = 6);` — mutates edge rects + stamps `placement.outline_w/h`; core rects untouched; implements design §1.2.4 pseudocode verbatim (scoped Problem, `legalizeEdgeBlocks`, verify all edge blocks `edgeRectLegal` + no overlap + containment; on failure grow deficient axis `*1.05 + kEps` toward MAX; fallback = MAX with re-legalized edges = today's layout).
- [ ] Failing tests: for each of case0..case4 (parse, decode bstar seed 1, then shrinkWrapOutline): (a) `validateSolution` against a scoped Problem (outline = stamped W,H) reports ok for containment/edge-location/overlap (note: current validateSolution reads problem outline — build the scoped copy in the test, T6 fixes the real validator); (b) stamped W ≤ MAX_w, H ≤ MAX_h; (c) no core rect moved (compare before/after); (d) seeds 2,3 fuzz on case0 — same invariants; (e) a mid-side (TM-style) edge block longer than the shrunk third still legal (targeted A19/A20 regression, design Q3).
- [ ] RED → implement → GREEN + suites + commit `feat: add exact shrink-wrap with edge re-anchoring`.

### Task 4: stamp in decode + cheapScore area gradient (design §1.3)

**Files:** Modify `src/placement_engine.cpp` (end of BOTH engine `decode`s: call `shrinkWrapOutline`), `src/eval.cpp` (`cheapScore` outline_area from stamped fields w/ MAX fallback; add static `channelMarginProxy(problem, placement, W, H)` = Σ per bundle `(nets/25) × (crossing span proxy)` clamped to `MAX_area − W·H`, added to outline_area; containment penalty stays vs MAX — do NOT touch the `score.value` composition line).
- [ ] Failing test: two hand-built placements (same blocks, one compact core / one spread) ⇒ compact has strictly smaller `cheapScore().outline_area`; both < MAX area when loose.
- [ ] RED → implement → GREEN + suites (existing regressions may shift SA trajectories — legality asserts must stay green; if any asserts an exact score value, update + note in commit body) + commit `feat: cheap shrink-wrap area gradient in cheapScore`.

### Task 4.5 (amendment 2026-07-06): bisection shrink — replace linear grow (evidence: T4 smoke shrank case0 only 0.16%)

**Why:** T3's grow loop (×1.05^≤6 from the per-axis lower bounds, both axes coupled) exhausts before reaching the true feasible frontier on case0 (feasible W ≈ core_right + inward-moving corner-block width, ≫ lower_bound × 1.34) ⇒ falls back to MAX on most decodes; SA only sees shrink after lucky mutations. Feasibility is monotone in the outline ⇒ bisection finds the near-minimal legal outline deterministically.

**Files:** Modify `src/placement_engine.cpp` only (`shrinkWrapOutline` + `shrinkWrapEstimate` search strategy; feasibility predicates `placementLegalAt`/`edgeBlocksFeasibleAt` unchanged). Test in p1 test file.
**Algorithm:** t-interpolation bisection: candidate(t) = lerp((W_lb,H_lb) → (MAX_w,MAX_h), t ∈ [0,1]); feasible(t) monotone ⇒ binary search smallest feasible t (≤12 iterations or until relative gap < 0.5%); then two per-axis refinement bisections (shrink W alone holding H, then H holding W, same tolerance). Keep MAX fallback; stamp result; determinism (pure arithmetic midpoints).
- [ ] Failing test: case0 bstar seed 1 RAW initial decode ⇒ stamped area ≤ 0.9 × MAX area (currently ≈ MAX = RED); T3's whole invariant suite stays green; byte-determinism across two runs.
- [ ] Implement → GREEN + all suites + commit `perf: bisection shrink-wrap finds near-minimal legal outline`.

### Task 5: exactScore routes inside declared outline + 1-step corrective grow (design §1.4)

**T4-review carry-over (binding):** `compactTowardOrigin` mutates core rects but keeps a STALE stamped outline — T5 must reset `outline_w/h = 0` on the compacted copy (or re-run `shrinkWrapOutline`) before exact-scoring, otherwise exactScore reads a wrong declared outline.

**Files:** Modify `src/eval.cpp` (`exactScore`: scope outline from stamped fields — recompute via `shrinkWrapOutline` on a local copy if unset; after routing, if `open_routes > 0`, do ONE corrective grow — re-run `shrinkWrapOutline` with the failed axis bumped by `channel_overflow/25` margin (min +5%) — and re-route once; keep the better of the two results by value with `open_routes==0` preferred absolutely).
- [ ] Failing test: case0 decode+shrink ⇒ `exactScore` outline_area < MAX area, `open_routes == 0` (post corrective grow), and returned score.outline_area == stamped W×H used for routing.
- [ ] RED → implement → GREEN + suites + commit `feat: exact scoring routes inside the declared outline`.

### Task 6: validation + writeCfg use declared outline (design §1.6)

**Files:** Modify `src/validate.cpp` (build scoped Problem from `placement.outline_w/h` fallback MAX for containment/thirds/flush), `src/io.cpp` (`writeCfg` emits declared W,H when set, fallback current 3-arg usedOutline path). Extend `testOutlineConsistencyEvalVsCfg` in the p0 OR p1 test file to a shrunk case: exactScore.outline_area == emitted OUTLINE w×h == stamped W×H.
- [ ] Failing test first (false edge-location violation on a shrunk case0 solution via real `validateSolution`; outline consistency on shrunk case).
- [ ] RED → implement → GREEN + suites + commit `fix: validate and emit against the declared outline`.

### Task 7: elite-pass outline sweep (design §1.4 elite path)

**Files:** Modify `src/search.cpp` (elite top-16 + compacted loop: for each candidate run `outlineSweep`), `src/eval.cpp` or `search.cpp` static helper `outlineSweep(problem, placement)` — deterministic 5–7 additive-margin trials between the tight shrink-wrap and MAX (e.g. margins {0, 0.25, 0.5, 1, 2, 4} × (overflow/25) or geometric fractions of (MAX−tight)); exact-route each via the cached `exactCached`; pick min value with `open_routes==0` strictly preferred.
- [ ] Failing test: regression harness runs case0 sp/bstar seed 1 with sweep ⇒ final best score ≤ pre-sweep best (record numbers), validation ok. Plus determinism: two identical runs byte-identical `.cfg`.
- [ ] RED (helper missing) → implement → GREEN + suites + commit `feat: elite outline sweep finds min-cost declared outline`.

### Task 8: finalizeFeedthrough grows outline + guard alignment (design §1.5 + final-review adjudication)

**Files:** Modify `src/search.cpp` `finalizeFeedthrough`.
Semantics (this is the reconciled rule — design §1.5 item 1 as written is NOT allowed):
- Inflated re-decode: after `engine.decode(inflated, state)`, run `shrinkWrapOutline(inflated, placement)` letting the outline settle anywhere ≤ MAX (grow for headroom) before `exactScore`.
- Accept guard: commit iff `new_routing.feedthrough_mismatch < incumbent_mismatch − kEps` **AND** `new_routing.open_routes == 0` **AND** shrink-wrap reported legal (containment/overlap by construction). Do NOT require `exact.value < best` anymore, but NEVER accept an illegal variant regardless of mismatch (final-review adjudication: legality is non-negotiable; mismatch is only a graded penalty).
- [x] Failing test: extend the p1 regression — run hybrid on `test/2026 ICCAD_case.case1.csv` and `case3.csv` (seed 1, iters 2000): assert `feedthrough_mismatch == 0`, `open_routes == 0`, validation ok. (These are the P1 acceptance criteria; they currently fail with mismatch 14479/36988.)
- [x] RED → implement → GREEN + all suites + commit `fix: feedthrough finalize grows declared outline for headroom`. **Verified already satisfied by prior commits** (`16cf24b` paydown guard + T4/T4.5's shrink-wrap-inside-decode growing toward MAX on the inflated Problem copy) — no further `src/` change needed; added the permanent acceptance regression (`testFinalizeFeedthroughAcceptanceCase1Hybrid`/`Case3Hybrid` in `test/regression_p1_20260706.cpp`) to lock the gate in place.
- [x] Final: run the 5-case × 3-engine sweep (iters 2000 seed 1), append a P1 before/after table to `docs/research/2026-07-06-cost-reduction-strategy.md` appendix, mark §4 items 5-6 done, commit `docs: record P1 outline shrink-wrap results`.

## Self-Review
- Spec coverage: design decisions §1.1-1.6 map to T1(fields/key) T2(estimate) T3(exact wrap) T4(cheap gradient) T5(exact routing) T6(validate/output) T7(sweep) T8(finalize+acceptance). Cache fix in T1; hybrid seeding needs no change (design §3.3); compactTowardOrigin covered by T5 recompute path (design §3.4).
- Deviation from design, deliberate: T8 guard requires legality (open==0 + legal shrink) with mismatch-decrease, per whole-branch-review adjudication; design's "independent of total value" is adopted but its bare "commit on mismatch decrease" is constrained by legality.
- No placeholders: algorithms live in p1-design.md §1.2 (verbatim pseudocode) — each task names its governing sections; test assertions are concrete.
