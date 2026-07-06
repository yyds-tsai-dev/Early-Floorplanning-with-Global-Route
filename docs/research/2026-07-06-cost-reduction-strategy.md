# Cost Reduction Strategy — 2026-07-06

Research synthesis: latest official spec (`docs/official/E_20260608.pdf`) + Q&A (`docs/official/E_QA_20260618.pdf`) + src audit + external literature/repo sweep. Goal: reduce `Cost = OutlineArea + α × TotalWireLength`.

Supersedes nothing; extends `docs/superpowers/specs/2026-06-14-long-term-placement-engine-design.md` (placement-engine architecture stays).

---

## 0. TL;DR — priority ladder

| Prio | Item | Cost lever | Effort |
|------|------|-----------|--------|
| **P0** | Scoring-fidelity fixes: FT area write-back to output blocks, `ceil` FT extend + 2-decimal area rounding, eval outline must include route channels | Removes *guaranteed* FT-overflow deduction + stops SA optimizing a wrong objective | days |
| **P1** | Outline (W,H) as a search variable (shrink-wrap + edge re-anchor) | Case00 utilization is 55% ⇒ up to tens of % of OutlineArea | ~1 wk |
| **P2** | Cost-driven routing: FT-vs-channel marginal-area edge weights + negotiated rip-up/reroute | Official example: FT saves 2.4× area vs channel on one bundle | ~1 wk |
| **P3** | Demand-driven channel sizing (constraint-graph re-spacing; width = demand/25) | Converts leftover whitespace into exactly-needed channels; kills overflow | ~1 wk |
| **P4** | Search upgrades: Fast-SA schedule, IAR moves, adjacency-targeted moves, LR soft-block sizing, portfolio + threads + `--time-budget` | Better optima within 2 h budget; WL via zero-WL abutment | 1–2 wk |
| **P5** | Optional: FTAFP-style slack rep, flow-based congestion proxy, slicing/shape-curve engine, MILP polish | Incremental | opt |

---

## 1. Spec deltas (20260608 + QA 20260618) vs current implementation

Verified by src audit (2026-07-06):

| Rule | Source | Status | Evidence |
|------|--------|--------|----------|
| WL = Σ HPWL between guiding points (midpoints of overlap segments) × nets | PDF Cost Function | ✅ correct | `routing.cpp:24-35,178-186` |
| Per-direction channel capacity (25 nets/um; horizontal vs vertical separate ledgers) | QA A3/A5 | ✅ correct | `model.hpp:67-68`, `routing.cpp:229-232` |
| Direct block↔block routing without channel (adjacent pair port-to-port) | QA A31 | ✅ supported | `routing.cpp:104-112` (adjacency over all rects) |
| PORT EDGE multi-value = participant's choice; left=1 clockwise 1→2→3→4 | QA A21/A22/A35.3 | ✅ correct | `io.cpp:86-95`, `routing.cpp:17-22,45-49` |
| LOCATION: 90° pair (TL,LT) = AND/corner; parallel (TM,TR) = touch-one | QA A8/A35.2 | ✅ correct | `placement_engine.cpp:94-111` |
| Edge block flush with declared outline; regions = equal thirds; 1D check | QA A10/A12/A20 | ✅ | `placement_engine.cpp:31-208` |
| **FT-inflated area must appear in output block dims** (else proportional deduction) | QA A1.2, A25 | ❌ **missing** | required area is penalty-check only, never written back (`routing.cpp:234-241`) |
| **FT extend = ceil((nets/25 × rate)/2) per side, unconditional; area rounded to 2 decimals** | PDF p.8 note, QA A34 | ❌ **missing** | `routing.cpp:237-238` plain float, no ceil/round |
| **Output outline = die incl. channels + top routing + FT-inflated blocks** | QA A25 | ⚠️ **split-brain** | `io.cpp:128-131` includes channels; `eval.cpp:6-16` (scoring) does **not** |
| Channel overflow / FT overflow = proportional deduction, not fail | QA A1 | ⚠️ search uses hard 1e6 penalty | `eval.cpp` |
| Bundle may split into multiple paths; same block may exit different edges | QA A4/A6 | ✅ (fixed ≤8×~1000 chunks) | `routing.cpp:216-217` |
| MACRO/EDGE no rotation | QA A15 | ✅ | engine preserves dims |
| α read from input; varies per testcase | QA A29/A35.5 | ✅ | `io.cpp` |
| Runtime: ALL testcases within 2 h; no GPU; BLK < 50; nets ≤ 500k | QA A1.3/A1.4/A24 | ❌ no time budget, no threads, default 1000 iters | `search.cpp`, `Makefile` |

Fail conditions (only these): format, block overlap, routing open, outline violation. Everything else is graded ⇒ search may traverse overflow states.

---

## 2. Cost-model analysis (why these levers)

**Cost = OutlineArea + α·TotalWireLength**, α per testcase (0.2 in the five public cases).

### 2.1 Outline is a decision variable — the biggest untapped lever
- `outline_w/h` are parsed from the `MAX` row (`io.cpp:181-183`) and used everywhere as *the* outline. Edge blocks anchor to MAX borders; `compactTowardOrigin` (`search.cpp:18-55`) moves only non-edge blocks. Any R/T-side edge block ⇒ bbox = MAX ⇒ **OutlineArea stuck at MAX**.
- QA A13: MAX is an upper bound; the *declared* output outline is what's scored. Case00: Σ block areas ≈ 3.60M um² vs MAX 2916×2220 = 6.47M um² ⇒ utilization ~55%. Even at a conservative 75–80% achievable utilization (channels included), OutlineArea drops ~25–30% ⇒ similar-order Cost drop.
- All edge-anchoring / 12-region-thirds / flush rules are relative to the **declared** outline (QA A12/A13), so shrinking is legal as long as edge blocks are re-legalized against the shrunk outline.

### 2.2 Direct abutment ⇒ zero wirelength
- A path `BLK_A → BLK_B` (two rects) has 1 overlap segment ⇒ 1 guiding point ⇒ the HPWL sum over consecutive guiding-point pairs is **empty ⇒ WL = 0**, and QA A31 says adjacent blocks connect port-to-port with **no channel**.
- Consequence: for heavy bundles, abutting the pair erases `α × nets × dist` from Cost *and* removes their channel demand. Case00 BLK03→BLK04 = 1500 nets: at ~1000 um separation and α=0.2 that's ~300k cost from one bundle.
- Placement search should therefore *explicitly* maximize bundle-weighted abutment (subject to PORT EDGE sides), not just minimize center-HPWL.

### 2.3 FT vs channel is a computable area trade — stop treating FT as fallback
- Official Figure 14/15: 3600 nets via channel CH7 needs 144 um width ⇒ 87,940 um²; same nets via FT through BLK07 (40% tier) adds 36,063 um². FT wins 2.4×.
- Current router: pass 0 channels-only, pass 1 allow soft blocks (`routing.cpp:135-136`) — feasibility-driven, never cost-driven.
- Fix: Dijkstra edge weights = α·ΔWL + Δarea, where channel traversal charges marginal widening `(chunk_nets/25) × λ_strip` and soft-block traversal charges the *incremental* FT-tier area for that block (tier function of accumulated crossing nets). Router then minimizes actual ΔCost. Add 2–3 negotiation rounds (PathFinder-style history cost) to fix ordering artifacts; trivial at ≤50 blocks / ≤ a few hundred chunks.

### 2.4 Channel width should equal demand
- Channels today are leftover whitespace (`extractChannels`, `routing.cpp:54-90`): wider than demand ⇒ wasted area; narrower ⇒ overflow deduction.
- Post-route re-spacing: build H/V constraint graphs over blocks with **min-gap = directional demand/25** on strips that carry routes (0 where unused), longest-path re-coordinate, re-anchor edge blocks, re-extract channels, re-route. 2–3 iterations converge in practice. This makes P1's shrink-wrap *safe* (shrink never below required channel widths).

### 2.5 FT write-back is a correctness-of-score issue, not a nicety
- Judging recomputes FT-required area from crossing nets; output block dims must reflect it (QA A1.2) else proportional deduction *every time FT is used*. Currently never applied (`routing.cpp:234-241` penalty-check only) ⇒ P2's aggressive FT is unsafe until P0 lands. Pipeline: final placement → routes → per-soft-block required dims (ceil + 2-dec rounding, AR-legal reshape per QA A32) → write back → re-legalize (may push neighbors) → re-route → iterate ≤3 (tier oscillation capped; prefer tier-stable fixed point).

### 2.6 Runtime budget is 2 h; we use minutes
- No wall-clock stop, no threads (`Makefile` single-TU, no `-pthread`). At n<50, decode is µs-scale; exact routing dominates. Portfolio = K threads × {sp, bstar, hybrid} × seeds, shared best pool, deterministic merge (fixed order), `--time-budget` per case. Zero algorithmic risk, pure quality variance reduction.

---

## 3. External research digest

### 3.1 Directly-on-topic (feedthrough / early-floorplan routing)
- **FTAFP** (Li et al., ASP-DAC 2025, 10.1145/3658617.3697728): first floorplanner with feedthrough cost *inside* SA; SCB-Tree = CB-tree + O(1) slack queries; two-phase SA (legalize-into-outline, then optimize FT+WL); slack-driven targeted perturbation. → Port the *ideas*: FT-count estimate in `cheapScore`; slack-driven moves; two-phase schedule. Full SCB-Tree rep optional (P5).
- **Piano** (arXiv:2508.13161, 2025): geometric adjacency graph + shortest-path pin assignment deciding feedthrough vs channel per net — architecturally what our router already is; their pin-assignment stage informs PORT-EDGE-aware endpoint choice.
- **Channel-Based SoC Feedthrough Insertion** (IEEE 9825309, ~2022): industrial validation of channel-vs-FT trade; sanity-check reference for P2.
- **Flora** (2025): zero-whitespace reshaping conflicts with hard/edge blocks needing channels (flagged in prior Gemini v2 report) — mine only the whitespace-absorption idea for soft blocks; skip the engine.

### 3.2 Fixed-outline / soft-block classics with direct hooks
- **Fast-SA** (Chen & Chang, ISPD 2005): 3-stage schedule (random → pseudo-greedy → hill-climb), adaptive cooling from avg uphill cost. → replace current SA schedule (P4).
- **IAR / IARFP** (ISPD 2007): remove + enumerate legal reinsertion positions (SP & B*-tree contour insertion points) instead of blind swaps — spends iterations only on near-legal candidates. → highest bang-per-day move upgrade (P4).
- **Lagrangian soft-module sizing** (Young, Chu, Luk, Wong, TCAD 2001): fixed topology ⇒ solve soft w/h optimally on the constraint graph in near-linear time, instead of mutating aspect by ±15%. → cuts area+WL simultaneously; run on elites or every k iters (P4).
- **Stockmeyer shape curves / DeFer** (TCAD 2010): exact soft sizing for slicing trees; optional alternative engine (P5).
- **Parquet** (Adya & Markov): slack-based moves; auto-select representation by whitespace/block count — validates `hybrid`. Source gated (email); unofficial mirror `github.com/archfp/floorplan` for reading only.
- **Flow-based congestion estimation in floorplanning** (Chu et al.): −36% congestion for +1.3% area — principled upgrade of `routeAwareProxyPenalty` (P5, or with P4 proxy alignment).
- **HeLEM-GR** (ICCAD 2024): exponential/linearized multiplier overflow penalties beat fixed 1e6-style weights — shape the graded overflow term during search (supports "traverse overflow states").
- **Surrogate management** (Complex & Intelligent Systems 2024 survey; chiplet-thermal SA arXiv:2504.03808): vocabulary + recipes for tuning `--exact-interval` / elite-only exact scoring — our two-tier design is the standard pattern; measure proxy↔exact rank correlation (Spearman) to tune cadence.
- **PARSAC** (`github.com/IntelLabs/parsac`, Apache-2.0, C++ SA core, archived read-only): SA built for *boundary constraints* (≈ our edge blocks) + preplaced blocks via an **anchored B*-tree**, plus a parallel multi-worker portfolio — the closest permissively-licensed code to mine for P4's constraint-aware moves, edge-anchored decode, and portfolio runner. (Note: the academic "Flora" floorplanner is arXiv:2507.14914; the GPL `fakerbaby/Flora` repo is an unrelated DREAMPlace post-processor.)
- **WireMask-BBO** (NeurIPS 2023): greedy wire-mask position + black-box outer loop; CPU-portable seed generator (P5, low prio at n<50).

### 3.3 Non-transfers (checked, rejected)
- **STAIRoute / HGR**: monotone-staircase channels are *not legal here* — the spec fixes channel extraction to vertical slicing (PDF p.6 + reference code). Rejected.
- **Corner stitching** (Magic `tiles/`): O(1) neighbor queries irrelevant at n<50; heavy mutable structure. Rejected.
- **vEB-tree FAST-SP decode**: O(n²) decode is µs at n<50. Rejected.
- **RL / GPU anything**: no GPU in judging env (QA A1.4); n<50 too small to amortize learning. Rejected. (PARSAC itself is CPU C++ and *is* minable — see §3.2; only its massive-scale ambitions are irrelevant here.)
- **Flora code** (`fakerbaby/Flora`): GPL-3.0 + DREAMPlace post-processor, not floorplan-stage — skip code, keep only the whitespace-absorption idea.
- **ICCAD 2026 ecosystem**: Problem E is a *Promotional* problem (MediaTek) on the Taiwan contest site (`iccad-contest.org/tw/`), absent from the international 2026 site. News page (`/tw/02_news.html`) has nothing after 06/18 (Q&A + testcase update); **no α announcement, no 7-extra-testcases, no official evaluator for E** as of 2026-07-06. Deadlines: alpha-test 06/22 (past), **beta 2026-07-24**, final 2026-08-21 — expect α/testcase drops near beta; re-check the news page + Problem E Drive links before then. No public team repos for E. Closest public analogs: `johnnyhuang1007/iccad-2023-problem-D` (claimed 1st, fixed-outline + rectilinear soft blocks — worth a source read), Intel FloorSet (similar area+weighted-WL+constraint scoring).

### 3.4 Prior in-repo reports (docs/research/*.md)
GPT/Gemini ×2 converge on FTAFP/Piano/Flora/PARSAC but **predate the 20260608 spec**: none cover PORT EDGE, per-direction capacity, proportional overflow scoring, corner AND-semantics, direct block-adjacency routing, or guiding-point WL. This document + the audit table above supersede them on spec matters.

---

## 4. Roadmap

### P0 — scoring fidelity (do first; everything else is unsafe or mis-measured without it)
1. ✅ **DONE** (`3a04376`, `262e77c`, `15ad8ec`; commit-guard fix 2026-07-06) — **FT area write-back pipeline** (`routing.cpp`, `search.cpp` final pass, `io.cpp`): per soft block, required dims from accumulated crossing nets (per-tier rate from CSV) → `extend = ceil((nets/25 × rate)/2)` per side → reshape to AR-legal dims (QA A32: √area basis) → write into output; re-legalize + re-route to fixed point (≤3 iters). Implemented as `finalizeFeedthrough` (`search.cpp:71-103`), hooked into every `runPlacementSearch` call (including both legs of `hybrid`, `search.cpp:213`, `:227,230,232`). **Known defect (fixed 2026-07-06):** the pass's commit guard (`search.cpp:96-97`) accepts a FT-corrected candidate only if its *total* `exact.value` beats the current best, not just if `feedthrough_mismatch` drops. Because re-legalizing an inflated block via `seedFromPlacement`/`decode` (full re-decode, not an in-place grow) usually spills neighbors out-of-outline or overflows channels, the corrected candidate's total score is almost always far worse, so the guard discards it — even on passes where mismatch genuinely reached 0. Confirmed by instrumented tracing (see the P0 cost sweep Appendix below): `hybrid` on case1/case3 never converges to mismatch 0 this way; the mismatch-0 rows elsewhere are explained by the SA search itself landing on a mismatch-0 placement, not by this pass correcting a nonzero one. **Follow-up (landed 2026-07-06):** the guard is now `acceptFeedthroughPaydown` — commit on strict `feedthrough_mismatch` decrease (or equal mismatch at a lower total, the compact/reclaim path), never with open routes, and only if `validateSolution` passes; each pass also tries a `compactTowardOrigin` variant and a bounded 32-attempt mutate+decode repair loop (geometric legality pre-filter before paying `exactScore`), and `hybrid` leg selection uses the same paydown-first ordering (`paydownBetter`). Post-fix 5×3 sweep (`--iters 2000 --seed 1`): all 15 rows report `feedthrough_mismatch=0`, `validation=ok`, `open_routes=0`.
2. ✅ **DONE** (`3a04376`, `262e77c`) — **Exact FT math**: unconditional `ceil` on extend; area rounded to 2 decimals (QA A34). Unit-tested against the four official Figure 9 examples (87,500 um² @1000→+4um→89,882.43; @3500→+28→104,849.02; @6711→+108→163,057.66; @12013→+241→288,158.52) in `test/run_p0_regressions.sh` (`testFtMathOfficialExamples`).
3. ✅ **DONE** (`10d7615`) — **Unify outline**: `eval.cpp` `usedOutline` now shares the same computation as `io.cpp`'s emitted `.cfg` outline (QA A25); score and emitted `.cfg` agree by construction. Regression: `testOutlineConsistencyEvalVsCfg` in `test/run_p0_regressions.sh`.
4. **Deferred to P4**: exact overflow penalty is already amount-proportional (1e6 × overflow um); only the SA weight *schedule* remains, which is search-engineering, not scoring fidelity.

### P1 — outline shrink-wrap (biggest single lever)
5. ✅ **DONE** (T1-T8, 2026-07-06/07) — Candidate outline (W,H) ≤ MAX as part of `Placement`; decode anchors edge blocks + 12-region thirds against (W,H). Per exact eval: shrink-wrap W,H = max(non-edge packing extent + edge extents + demanded channel widths), re-anchor, validate (LOCATION AND/OR per shrunk outline), route, score. Cheap eval uses proxy demand. See `.superpowers/sdd/p1-design.md` and the P1 appendix below for the full sweep. (Direct outline SA genes intentionally deferred — design §1.1 rejects them for v1: the shrink-wrap already gives the exact minimal-legal answer per decode for free.)
6. ✅ **Conservative v1 shipped, tightening deferred to P2/P3** — the elite-pass sweep (T7) and per-eval corrective grow (T5) use generous margins and never shrink below a routable/legal frontier (`open_routes==0` is a strict precondition everywhere); the P1 appendix sweep shows `channel_overflow` rising as outline area falls on several rows, confirming shrink is currently *safe but not overflow-aware* — P3's demand model is the intended fix for over-shrinking into unroutable widths.

### P2 — cost-driven routing
7. Dijkstra weights = α·ΔWL + marginal area (channel widening vs incremental FT tier), replacing channel-first pass structure; keep pass-0/1 as fallback for open routes.
8. Negotiated reroute: 2–3 rip-up/reroute rounds with history cost on overloaded channels; order bundles by nets desc.
9. Adaptive chunking: split at capacity bottlenecks instead of fixed ~1000×≤8 (`routing.cpp:216-217`); QA A6 allows arbitrary split.

### P3 — demand-driven spacing
10. Constraint-graph re-spacing with per-strip directional min-gaps = demand/25; longest-path recoordinate; iterate route↔space ≤3. Also usable as a *repair* operator for overflow (widen exactly the deficient strip).

### P4 — search engine
11. Fast-SA 3-stage schedule + adaptive cooling (`search.cpp`).
12. Move portfolio: IAR reinsertion (SP position enumeration; B*-tree contour insertion points), abutment-targeted move (pick bundle by weight, move endpoint adjacent to partner on a PORT-EDGE-legal side), slack-driven move (FTAFP), soft-aspect nudge toward constraint-graph critical path; tuned probabilities.
13. LR soft sizing on elites / every k iterations (Young-Chu TCAD 2001).
14. `cheapScore` alignment: bundle-weighted abutment credit (adjacent ⇒ ~0 WL), per-strip directional demand proxy, FT tier proxy w/ ceil math; validate by Spearman rank-correlation of cheap vs exact on sampled candidates per case.
15. Portfolio runtime: `--time-budget` (wall-clock, default sized for 12 cases / 2 h), `-pthread`, K chains × {sp,bstar,hybrid} × seeds, shared best pool with deterministic merge; keep single-thread determinism under fixed `--seed`.

### P5 — optional
16. Flow-based congestion proxy; FTAFP SCB-Tree slack representation; slicing-tree + Stockmeyer engine as 4th backend; CP-SAT/LNS coordinate polish on final elites; read `johnnyhuang1007/iccad-2023-problem-D` source for fixed-outline tricks.

### Verification gates (every phase)
`make` warning-clean; `bash test/run_case00.sh`; `bash test/run_placement_engine_regressions.sh`; `bash test/run_20260618_regressions.sh`; `bash test/run_20260608_regressions.sh`; new: FT-math unit tests (official examples), outline-consistency regression, per-case Cost tracking table (all 5 public cases × engines × seeds) to catch regressions in Cost, not just legality.

---

## 5. Open questions (official ambiguity — keep conservative)
- **Overflow deduction formula unpublished** (QA A1 says proportional). Until known: final answers 0-overflow; graded penalty only inside search.
- **A3.1 vs A3.2 tension**: A3.1 sums all occupied segments regardless of y-band; A3.2's example splits a channel into disjoint y-bands each with own width capacity. Current model (sum, per-direction) is the conservative reading. Exploiting band-disjointness could *legally raise* effective capacity ⇒ narrower channels; revisit only if an official evaluator appears.
- **α for hidden cases** unknown (public = 0.2, default 1): keep all weights α-parametric; consider a quick α-sensitivity sweep per case at runtime (cheap at n<50).
- **7 extra public testcases** promised (BLK<50) — not found publicly as of 2026-07-06; re-check the contest channel periodically.

## 6. Key sources
Fast-SA (Chen & Chang, ISPD'05) · IARFP (ISPD'07) · Young/Chu/Luk/Wong LR sizing (TCAD'01) · Stockmeyer'83 / DeFer (TCAD'10) · Adya-Markov Parquet (TVLSI'03) · FTAFP (ASP-DAC'25) · Piano (arXiv:2508.13161) · Channel-Based SoC FT Insertion (IEEE 9825309) · Flow-based congestion (Chu et al.) · HeLEM-GR (ICCAD'24) · surrogate-management survey (2024) · WireMask-BBO (NeurIPS'23) · OpenROAD Hier-RTLMP (TCAD'24) — full URLs in the agent transcripts and §3.

---

## Appendix: P0 before/after cost sweep (2026-07-06)

Commands: `bin/efgr "test/2026 ICCAD_case.caseN.csv" out.cfg --engine ENGINE --iters 2000 --seed 1` for `caseN` in `case0..case4` and `ENGINE` in `sp`, `bstar`, `hybrid` (15 runs per side).

- **AFTER**: HEAD `15ad8ec` (branch `fable5`), built via `make clean && make`.
- **BEFORE**: pre-P0 baseline `81a4e27f5928` ("docs: add cost-reduction strategy..."), built in an isolated `git worktree add /tmp/p0_before 81a4e27f5928` + `make`, then the worktree was removed (`git worktree remove --force`).

All 15 BEFORE and 15 AFTER runs reported `open_routes=0 validation=ok`.

| Case | Engine | score (before → after) | outline_area (before → after) | wirelength (before → after) | channel_overflow (before → after) | ft_mismatch (before → after) |
|---|---|---|---|---|---|---|
| case0 | sp     | 1.32981e+07 → 1.38054e+07 | 6.47352e+06 (same) | 1.17437e+07 → 1.17418e+07 | 0 (same) | 0 → 0 |
| case0 | bstar  | 3.31217e+07 → 3.49438e+07 | 6.47352e+06 (same) | 7.12544e+06 → 7.12304e+06 | 0 (same) | 0 → 0 |
| case0 | hybrid | 1.32981e+07 → 1.38054e+07 | 6.47352e+06 (same) | 1.17437e+07 → 1.17418e+07 | 0 (same) | 0 → 0 |
| case1 | sp     | 5.28117e+10 → 5.31373e+10 | 7.61914e+06 (same) | 4.37130e+07 → 4.37117e+07 | 51967.7 (same) | 0 → 0 |
| case1 | bstar  | 3.41863e+10 → 3.44200e+10 | 7.81099e+06 (same) | 4.67091e+07 → 4.66932e+07 | 33744.4 (same) | 0 → 0 |
| case1 | hybrid | 3.95763e+10 → 4.52637e+10 | 7.70811e+06 (same) | 1.97570e+07 → 1.97366e+07 | 30204.7 (same) | **9034.12 → 14479.2** |
| case2 | sp     | 8.96237e+10 → 9.32960e+10 | 4.20000e+07 (same) | 2.11233e+08 → 2.11225e+08 | 87570.9 (same) | **101.451 → 0** |
| case2 | bstar  | 8.84452e+10 → 8.86481e+10 | 4.20000e+07 (same) | 2.22937e+08 → 2.22922e+08 | 86736.2 (same) | 0 → 0 |
| case2 | hybrid | 7.06610e+10 → 7.08288e+10 | 4.20000e+07 (same) | 2.69260e+08 → 2.69253e+08 | 68936.4 (same) | 0 → 0 |
| case3 | sp     | 9.67545e+10 → 9.69316e+10 | 2.34157e+07 (same) | 1.40098e+08 (same) | 96188.5 (same) | 0 → 0 |
| case3 | bstar  | 7.85292e+10 → 7.87006e+10 | 2.34157e+07 (same) | 1.25993e+08 (same) | 78048.1 (same) | 0 → 0 |
| case3 | hybrid | 6.07492e+10 → 6.39076e+10 | 2.34157e+07 (same) | 1.14642e+08 → 1.14620e+08 | 26182.2 (same) | **34006.2 → 36987.5** |
| case4 | sp     | 1.90885e+11 → 1.90924e+11 | 3.77819e+07 (same) | 3.33606e+08 → 3.33605e+08 | 190350 (same) | 0 → 0 |
| case4 | bstar  | 2.05348e+11 → 2.05397e+11 | 3.77819e+07 (same) | 3.41655e+08 (same) | 204501 (same) | 0 → 0 |
| case4 | hybrid | 1.90885e+11 → 1.90924e+11 | 3.77819e+07 (same) | 3.33606e+08 → 3.33605e+08 | 190350 (same) | 0 → 0 |

### Observations

- **outline_area and channel_overflow are unchanged** before vs. after in every row — expected, since P0 (items 1-3) only touches FT area math/write-back and the shared-outline unification, not the placement search itself; the SA landed on effectively the same topology both times (identical `blocks/bundles/channels/routes` counts per row).
- **wirelength is flat to 5 significant figures** in every row — the abutment fix (commit `4a9cf72`) had no measurable effect on these five cases at `--iters 2000`, i.e. the SA did not happen to select adjacent-abutting placements for the heavy bundles in this run; the zero-WL-on-abutment code path is exercised and covered by its own regression (`testDirectAbutmentZeroWirelength`) but isn't the dominant contributor to these particular runs' scores.
- **`feedthrough_mismatch` went from nonzero to 0 in the one case where it was small** (case2/sp: 101.451 → 0) — a genuine fix from the write-back pass (commit `15ad8ec`).
- **`feedthrough_mismatch` remains nonzero and slightly larger in two rows: case1/hybrid (9034.12 → 14479.2) and case3/hybrid (34006.2 → 36987.5).** ⚠️ **This is an unresolved defect in `finalizeFeedthrough`'s acceptance guard, not merely an inherent limitation — flagged BLOCKED, needs a follow-up fix, not deferred.** *(Resolved 2026-07-06: commit guard replaced by the paydown-first rule `acceptFeedthroughPaydown` — commit on strict mismatch decrease, or equal mismatch + lower total for the compact/reclaim path — gated by `validateSolution`, plus a `compactTowardOrigin` variant and a bounded 32-attempt mutate+decode repair loop per pass; hybrid leg selection now uses the same ordering via `paydownBetter`. Post-fix 5×3 sweep at `--iters 2000 --seed 1`: all 15 rows report `feedthrough_mismatch=0`, `validation=ok`, `open_routes=0`.)* *(Update, later 2026-07-06: after the `cheapScore` penalty fix — see the second appendix below — these two rows reach mismatch 0 via search directly; the residual mismatch moves to case1/sp and case2/sp. The guard defect was fixed later the same day — see the resolution note above.)*
  Root-caused (instrumented run, then reverted before commit — see Task 6 report) to `finalizeFeedthrough`'s commit condition at `search.cpp:96-97`: `mismatch <= kEps && open_routes == 0 && exact.value < result.best_score.value`. The third clause is the problem. Inflating a soft block's area and re-decoding (`seedFromPlacement` + `decode`) shifts every downstream block and frequently spills blocks out of the outline or blows up channel overflow, which `exactScore` penalizes at `1e12`/`1e6` — so the FT-corrected candidate's *total* score is almost always far worse than the original mismatched best, and the guard discards it even on passes where `new_mismatch` legitimately reached 0. Confirmed by instrumentation: case1/hybrid's `bstar` leg never reaches mismatch 0 in any of its 3 passes (scores 1.70e12/1.96e12/4.49e12 vs. best 4.53e10); case3/hybrid's `sp` leg *does* reach `new_mismatch=0` on pass 0, but at `new_score=2.19e11 > best=6.39e10`, so the mismatch-free fix is discarded anyway. case2/sp's earlier "genuine fix to 0" is not actually `finalizeFeedthrough` succeeding either — hybrid's `bstar` leg happened to reach mismatch 0 via its own SA search before finalize was even needed, so the reported 0 there is a side effect of engine/seed selection, not evidence the guard works. Not a call-order/wiring bug: finalize runs inside every `runPlacementSearch` call, both hybrid legs included (`search.cpp:213`, `search.cpp:227,230,232`). **Fix direction (applied 2026-07-06):** commit whenever the FT-corrected candidate strictly reduces `feedthrough_mismatch` versus the current best (independent of total score, since the mismatch penalty is intended to be paid down first), then re-legalize/compact afterward to try to reclaim outline/overflow, rather than gating acceptance on total `exact.value`. All affected rows still report `open_routes=0` and `validation=ok` — the residual mismatch is a scoring penalty, not a legality violation, so nothing here is unsafe to ship, but the P0 roadmap's "write-back to a fixed point" goal is not fully met for engine/case combinations where the SA search itself doesn't already land on a mismatch-0 placement.
- All other combinations report `feedthrough_mismatch = 0` in both before and after runs (verified consistent with the same caveat: this reflects the SA search landing on a mismatch-0 placement directly, not `finalizeFeedthrough` correcting a nonzero one, since the guard above makes the pass a near-no-op whenever inflation would otherwise degrade total score — the common case).
- **Score deltas are small in most rows** and consistent with expectations (ceil'd FT math changing mismatch-penalty magnitude, minor floating-point differences in route geometry, no measurable outline growth in this sweep at these seeds). The two `hybrid` rows above are the exception: their score increase is directly attributable to the residual/regressed `1e6 × feedthrough_mismatch` term, which is an honest reflection of the unresolved defect above, not a new regression introduced by items 2-3.

## Appendix: cheapScore route-aware penalty fix sweep (2026-07-06)

**The fix.** `cheapScore()` (`src/eval.cpp:98`) computed `score.penalty = penalty + routeAwareProxyPenalty(...)` but then summed `score.value` from the *local* `penalty` only — the route-aware congestion/feedthrough proxy was stored in `score.penalty` yet never entered `score.value`, so it never influenced SA acceptance. Pre-existing bug, found during the P0 final review (out of P0 scope). One-line fix:
`score.value = score.outline_area + problem.alpha * wire_proxy + score.penalty;`

Commands: identical to the P0 sweep above (`bin/efgr "test/2026 ICCAD_case.caseN.csv" out.cfg --engine ENGINE --iters 2000 --seed 1`, 15 runs per side).

- **BEFORE**: clean `06a830b` ("feat: add declared outline fields to Placement and placementKey"), built in an isolated `git worktree add /tmp/prefix_ctl 06a830b` + `make`, worktree removed afterwards. This control reproduces the P0 sweep's AFTER column (`15ad8ec`) to printed precision in every row except one: **case2/sp reports `feedthrough_mismatch=3624.21` (the P0 table said 0) at an identical score** — attributable to `06a830b`'s `placementKey` change (declared-outline fields) altering exact-eval dedup on that row, not to this fix.
- **AFTER**: `06a830b` + the one-line fix (uncommitted working tree at measurement time; the tree's only other delta affecting the built binary was a declaration-only `placement_engine.hpp` stub with no callers — the remaining working-tree changes were `CLAUDE.md` and an uncompiled P1 test file). Build warning-clean (`-Wall -Wextra -pedantic`); all five suites pass (`run_p0_regressions.sh`, `run_case00.sh`, `run_placement_engine_regressions.sh`, `run_20260618_regressions.sh`, `run_20260608_regressions.sh`).

All 15 BEFORE and 15 AFTER runs report `open_routes=0 validation=ok`.

| Case | Engine | score (before → after) | outline_area (before → after) | wirelength (before → after) | channel_overflow (before → after) | ft_mismatch (before → after) |
|---|---|---|---|---|---|---|
| case0 | sp     | 1.38054e+07 → 1.55701e+07 | 6.47352e+06 (same) | 1.17418e+07 → 6.11657e+06 | 0 (same) | 0 → 0 |
| case0 | bstar  | 3.49438e+07 → 8.54956e+06 | 6.47352e+06 (same) | 7.12304e+06 → 3.97219e+06 | 0 (same) | 0 → 0 |
| case0 | hybrid | 1.38054e+07 → 1.55701e+07 | 6.47352e+06 (same) | 1.17418e+07 → 6.11657e+06 | 0 (same) | 0 → 0 |
| case1 | sp     | 5.31373e+10 → 4.92076e+10 | 7.61914e+06 → 7.47648e+06 | 4.37117e+07 → 4.69432e+07 | 51967.7 → 41750.0 | **0 → 6630.59** |
| case1 | bstar  | 3.44200e+10 → 5.80899e+10 | 7.81099e+06 → 7.55941e+06 | 4.66932e+07 → 3.82272e+07 | 33744.4 → 57540.7 | 0 → 0 |
| case1 | hybrid | 4.52637e+10 → 3.90550e+10 | 7.70811e+06 → 7.44470e+06 | 1.97366e+07 → 4.24022e+07 | 30204.7 → 38574.5 | **14479.2 → 0** |
| case2 | sp     | 9.32960e+10 → 8.29994e+10 | 4.20000e+07 (same) | 2.11225e+08 → 2.53346e+08 | 87570.9 → 65157.7 | **3624.21 → 16714.8** |
| case2 | bstar  | 8.86481e+10 → 8.01098e+10 | 4.20000e+07 (same) | 2.22922e+08 → 2.23880e+08 | 86736.2 → 78422.8 | 0 → 0 |
| case2 | hybrid | 7.08288e+10 → 6.34365e+10 | 4.20000e+07 (same) | 2.69253e+08 → 2.83797e+08 | 68936.4 → 62258.7 | 0 → 0 |
| case3 | sp     | 9.69316e+10 → 6.82716e+10 | 2.34157e+07 (same) | 1.40098e+08 → 1.80442e+08 | 96188.5 → 67913.9 | 0 → 0 |
| case3 | bstar  | 7.87006e+10 → 9.41422e+10 | 2.34157e+07 (same) | 1.25993e+08 → 2.03558e+08 | 78048.1 → 93621.8 | 0 → 0 |
| case3 | hybrid | 6.39076e+10 → 6.84063e+10 | 2.34157e+07 (same) | 1.14620e+08 → 1.80490e+08 | 26182.2 → 68048.6 | **36987.5 → 0** |
| case4 | sp     | 1.90924e+11 → 1.91059e+11 | 3.77819e+07 (same) | 3.33605e+08 → 3.48670e+08 | 190350 → 190602 | 0 → 0 |
| case4 | bstar  | 2.05397e+11 → 2.08476e+11 | 3.77819e+07 (same) | 3.41655e+08 → 3.68719e+08 | 204501 → 207752 | 0 → 0 |
| case4 | hybrid | 1.90924e+11 → 1.71727e+11 | 3.77819e+07 (same) | 3.33605e+08 → 3.21440e+08 | 190350 → 171323 | 0 → 0 |

### Observations

- **SA trajectories now change in almost every row** (channel counts, wirelength, overflow all move) — direct evidence the proxy participates in acceptance, in contrast to the P0 sweep where before/after landed on identical topologies.
- **Net effect is a cost win**: 9 of 15 rows improve. Sum over all 15 rows: 1.21244e+12 → 1.17502e+12 (**−3.1%**). Best-engine-per-case sum (contest usage): 3.60094e+11 → 3.42499e+11 (**−4.9%**). Per case (best engine): case0 −38.1% (bstar), case1 **+13.5%** (the one per-case regression — best engine shifts bstar→hybrid), case2 −10.4% (hybrid), case3 +6.8% (hybrid's mismatch-0 landing pays overflow; sp becomes the best engine), case4 −10.1% (hybrid).
- **The two ⚠ residual-mismatch rows from the P0 sweep (case1/hybrid 14479.2, case3/hybrid 36987.5) now report 0** — the proxy steers the search onto mismatch-0 placements directly. But **case1/sp (0 → 6630.59) and case2/sp (3624.21 → 16714.8) become the new residual rows**: this is the same open `finalizeFeedthrough` acceptance-guard defect documented above (the guard still cannot *correct* a nonzero-mismatch best), surfacing on different rows because the SA lands elsewhere. This fix neither repairs nor worsens that defect; the guard rework landed later the same day (see the resolution note in the P0 appendix above).
- **case1 is the only case whose outline_area moves — it shrinks on all three engines** (e.g. hybrid 7.70811e+06 → 7.44470e+06).
- `exact_evals` drops sharply in some rows (e.g. case0/sp 1161 → 390): with the proxy inside the accepted cheap value, cheap and exact rankings agree more often, so fewer moves become new cheap-best candidates that trigger exact routing.

## Appendix: P1 outline shrink-wrap results (2026-07-07)

Commands: `bin/efgr "test/2026 ICCAD_case.caseN.csv" out.cfg --engine ENGINE --iters 2000 --seed 1` for `caseN` in `case0..case4` and `ENGINE` in `sp`, `bstar`, `hybrid` (15 runs), built from HEAD after T1-T8 (declared-outline shrink-wrap + `finalizeFeedthrough` guard/headroom, this branch). **BEFORE** column is the "cheapScore route-aware penalty fix" appendix's AFTER column above (`06a830b` + the `score.value` proxy fix) — the correct immediate pre-P1 baseline, since P1 builds directly on top of it and P0's own appendix predates that fix. All 30 runs (15 before, 15 after) report `open_routes=0 validation=ok`.

| Case | Engine | score | outline_area | wirelength | channel_overflow | feedthrough_mismatch | validation |
|---|---|---|---|---|---|---|---|
| case0 | sp     | 1.80179e+07 | 5.54531e+06 | 1.09677e+07 | 0 | 0 | ok |
| case0 | bstar  | 9.20647e+06 | 6.46288e+06 | 7.30996e+06 | 0 | 0 | ok |
| case0 | hybrid | 1.80179e+07 | 5.54531e+06 | 1.09677e+07 | 0 | 0 | ok |
| case1 | sp     | 1.34604e+11 | 7.81821e+06 | 7.20216e+07 | 134405 | 0 | ok |
| case1 | bstar  | 3.93908e+10 | 7.55343e+06 | 3.99828e+07 | 38875.1 | 0 | ok |
| case1 | hybrid | 6.08190e+10 | 7.06371e+06 | 5.45152e+07 | 59986.7 | 0 | ok |
| case2 | sp     | 1.13474e+11 | 3.69609e+07 | 2.61459e+08 | 112436 | 0 | ok |
| case2 | bstar  | 1.12121e+11 | 3.41712e+07 | 2.47617e+08 | 109632 | 0 | ok |
| case2 | hybrid | 1.05866e+11 | 3.76369e+07 | 2.15922e+08 | 104514 | 0 | ok |
| case3 | sp     | 1.08810e+11 | 1.74095e+07 | 1.52748e+08 | 108465 | 0 | ok |
| case3 | bstar  | 9.79311e+10 | 2.32361e+07 | 2.21526e+08 | 97422.5 | 0 | ok |
| case3 | hybrid | 1.10756e+11 | 1.98094e+07 | 1.77796e+08 | 109987 | 0 | ok |
| case4 | sp     | 2.15875e+11 | 2.53474e+07 | 2.39256e+08 | 215317 | 0 | ok |
| case4 | bstar  | 2.32820e+11 | 2.90404e+07 | 3.24208e+08 | 232158 | 0 | ok |
| case4 | hybrid | 2.24423e+11 | 2.53910e+07 | 2.43792e+08 | 223916 | 0 | ok |

### Comparison against the pre-P1 baseline (table above)

- **P1 acceptance criterion met**: hybrid on case1 and case3 (the two rows that carried a residual `feedthrough_mismatch` — 14479.2 / 36987.5 — through most of the P0 investigation) report `feedthrough_mismatch=0, open_routes=0, validation=ok`, and now **every one of the 15 rows** reports `feedthrough_mismatch=0` — including case1/sp and case2/sp, which were the newest residual rows in the immediate pre-P1 baseline (6630.59 and 16714.8 respectively). `finalizeFeedthrough`'s existing paydown guard plus `decode`'s shrink-wrap growing toward MAX on the inflated problem (T4/T4.5, already in place before this task) resolved both without further code changes — verified, not re-implemented (see Status below).
- **outline_area shrinks in 11 of 15 rows**, most dramatically on the loose/overflow-light cases: case4 −33% (sp), case3 −26% (sp), case2 −19% (bstar), case0 −14% (sp/hybrid). case0/bstar and case1/bstar/case3/bstar barely move (<1%) — those topologies were already near their shrink-wrap floor.
- **Reported `score` rises in 14 of 15 rows despite the area win**, because `channel_overflow` (the `1e6×` penalty) grows as the outline tightens — e.g. case1/hybrid overflow 38574.5→59986.7, case3/sp 67913.9→108465. This is exactly the R1 trade-off flagged in the P1 design doc (§6) and the roadmap's own caveat (item 6: "depends on P3's demand model to not shrink below routable widths"): shrinking the outline makes channels scarcer, and the *net* Cost win on overflow-heavy cases is gated on P2 (cost-driven routing) / P3 (demand-driven spacing) landing, not on P1 alone. P1's job — a real area gradient reaching the SA, with legality (open_routes==0, validation ok, mismatch==0) preserved throughout — is delivered; the overflow-vs-area trade is deliberately left for the next roadmap phases per the design's own risk analysis.

### Status note

T8's two changes (paydown-first commit guard; inflated re-decode growing toward MAX for headroom) were **already implemented** before this task started — the guard landed in commit `16cf24b` (P0 follow-up, 2026-07-06) and the MAX-ceiling growth is an automatic consequence of T4/T4.5's `shrinkWrapOutline` running inside `decode` against the inflated `Problem` copy (whose `outline_w/h` stays at MAX). This task's contribution is verification (confirming both P1 acceptance criteria pass at the specified parameters) plus a new permanent regression (`testFinalizeFeedthroughAcceptanceCase1Hybrid`, `testFinalizeFeedthroughAcceptanceCase3Hybrid` in `test/regression_p1_20260706.cpp`) locking the acceptance gate in place, and this results writeup. No `src/` changes were needed.

### P1 addendum (2026-07-07, post-T8.1): outline growth cannot relieve inter-core congestion

Extending the elite sweep ladder to m ∈ {0.5, 1.0} (MAX-like rungs) was tested and mostly falsified:
14/15 rows unchanged (only case3/sp improved, 1.088e11 → 8.753e10). Root cause: `anchorAtOutline`
re-anchors edge blocks only — the core packing SA produced under the new area gradient stays frozen,
so larger declared outlines add *peripheral* whitespace, not channel capacity *between* congested
cores. With overflow weighted 1e6, tight-core candidates therefore score worse than the loose-core
solutions pre-P1 SA used to produce, on overflow-bound cases (1-4). This is the design's §5/§6-R1
dependency showing up as measured: the P1 area win is real (up to −33% outline_area; case0 hybrid
−14.5%) but converts into net Cost only where routing fits (case0), and needs **P3 demand-driven
re-spacing** (widen exactly the deficient inter-core strips) to land on cases 1-4. The wider ladder
is kept (harmless, cached, case3 benefits). No Pareto-vs-P0 claim is made for the P1 sweep table.
