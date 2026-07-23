# Research Refresh & Pre-Beta Strategy — 2026-07-23

Purpose: delta-research pass on top of the 2026-07-07 survey/review (`2026-07-07-external-techniques-survey.md`, `2026-07-07-direction-review.md`), incorporating everything that changed since: the byte-verified official cost function (beta T2/T3, `official-cost-function` memory), the 07-13 testcase / 07-18 Q&A / 07-20 beta-postponement drops, the integer-geometry work (beta T4), and the measured "time budget degrades results" failure (beta T5). Method: 6 parallel research passes (prior-work ledger, official-PDF full re-read, contest-site + repo delta, papers delta with exclusion list, surrogate/multi-fidelity ranking literature, and a golden-rescorer measurement experiment), synthesized against a code-level read of `search.cpp` / `eval.cpp` / `cfg_score.cpp`. The 07-07 do-not-repeat list stands in full; nothing below re-opens a rejected direction.

**Deadline context: beta 2026-07-31 17:00 GMT+8 (8 days). Branch `fable5` beta-final at `bcb5c90` (Σ checker 252.7M, −27.5% vs alpha).**

---

## 1. The cost structure has shifted — WL is now the dominant term

Fresh measurement (this session, `bin/efgr` default = hybrid @ iters1000 seed1, scored by the golden rescorer; `/tmp/ar_lever/` experiment):

| case | A = W·H | α·WL | overflow_penalty | checker_cost | A share | α·WL share | ovf share | R | whitespace |
|---|---|---|---|---|---|---|---|---|---|
| 0 | 5,038,504 | 742,875 | 0 | 5,781,379 | 87% | 13% | 0% | 0 | 28.0% |
| 1 | 7,474,401 | 6,308,400 | 248,673 | 14,031,473 | 53% | 45% | 1.8% | 0.0017 | 30.6% |
| 2 | 29,672,874 | 40,968,597 | 4,288,940 | 74,930,411 | 40% | 55% | 5.7% | 0.0072 | 25.7% |
| 3 | 20,088,525 | 23,086,153 | 1,529,490 | 44,704,168 | 45% | 52% | 3.4% | 0.0038 | 35.0% |
| 4 | 34,823,572 | 71,327,348 | 7,831,546 | 113,982,467 | 31% | 63% | 6.9% | 0.0112 | 55.7% |

Implications (this reorders the 07-07 priority ladder):

1. **α·WL is the single largest term on every non-trivial case (45–63%).** The 07-07 prediction "once overflow is gone, α·WL ≈ or > OutlineArea" is now measured reality. WL-directed search work (abutment, move design, proxy credit) has the largest headroom.
2. **Overflow penalty is down to 0–6.9% of cost** (R = 0.002–0.011, all far below the θ=0.05 tail). P1/P3/P2/beta already did the heavy lifting; further overflow work has a hard ceiling of ~13.9M Σ (5.5% of total). It is still worth harvesting — but via the cheap levers in §3, not new routing machinery.
3. **Area second (31–53%)**; case0 is now essentially area-only.

## 2. Headline re-diagnosis: why more budget makes results worse (code-verified)

The known-issues register attributes the T5 finding (hybrid 252.7M @ budget-0 → 318.2M @ 15s; overflow-driven, e.g. case4 37,844 → 161,013 nets) to "elite-pass cheap-score mis-ranking under integer geometry". A code-level walk of the selection pipeline sharpens this materially:

- Within a run, **every adoption path is already exact-gated and monotone**: best update (`search.cpp:473` `exact.value < result.best_score.value`), elite sweep adoptions (`outlineSweepBetter`: legality-first, then value), respace adoption (Pareto-safe by contract). With the same seed, budget-on replays the identical mutate stream, so at any iteration count the pre-finalize best is **provably no worse** than a shorter run's.
- Therefore the degradation can only enter through the two orderings that are *not* value-gated:
  1. **`acceptFeedthroughPaydown` (`search.cpp:117`)** — accepts any candidate whose `feedthrough_mismatch` strictly drops, *regardless of how much `score.value` worsens*. `finalizeFeedthrough` only fires when the best has mismatch > 0 (`search.cpp:253`); longer runs land on tighter packings where the paydown re-decode is more destructive, and the gate happily trades checker-scored area/WL/overflow for mismatch.
  2. **`paydownBetter` (`search.cpp:110`, used at `search.cpp:546`)** — hybrid's leg selection prefers lower mismatch lexicographically *before* comparing value. A mismatch-0 leg at checker 300M beats a mismatch-100µm² leg at 200M.
- These orderings are fossils of the pre-T3 era when mismatch carried a 1e6/µm² internal weight. Since beta T3, `exactScore` prices mismatch at the official `kBetaFt` (≈3.0/µm², `eval.cpp:182`) inside `score.value` — so mismatch-lexicographic ordering now actively fights the objective it was meant to protect. A 14,479µm² mismatch is worth ~43k cost units; the paydown regularly spends millions to erase it.

**Fix (small, surgical):** make both orderings legality-first → value-only (mismatch is already priced into value). Keep a mismatch-0 preference only as a near-tie tiebreak (safety: the official FT deduction has never been exercised — all alpha cases had mismatch 0; rescorer hardcodes ft=0; β_ft=3.0 comes from the report.json constant). After the fix, budget is monotone by construction → re-run the T5 sweep matrix → the 45–300× runtime headroom (A42: 8 cases / 2h *total*) finally becomes spendable. This is the prerequisite for the P4a portfolio and the highest-leverage pre-beta item.

**Supporting literature (new survey thread, none previously covered):** the surrogate-management canon confirms both the diagnosis pattern and the follow-on architecture — Jin/Olhofer/Sendhoff (IEEE TEC 2002: approximate-fitness search converges wrong on model false-optima; always exact-evaluate the best), saACM-ES (GECCO 2012: adapt surrogate trust to *measured Kendall τ* between cheap and exact rankings, computed from pairs already paid for), Lau & Ho (JOTA 1997: alignment probability — size the nomination set to survive proxy error; blind-pick floor `1−C(N−g,s)/C(N,s)`), successive halving (ICML'13/AISTATS'16) on a partial-routing fidelity ladder as a drop-in upgrade of "exact-score fixed top-16", and Gelfand & Mitter (JOTA 1989: tolerable evaluation error must shrink with temperature — densify exact evals late in the schedule). Honest gap: no theory covers SA under *systematic* (non-zero-mean) proxy bias — which is exactly our integer-geometry regime — so the engineering fix above comes first; the literature patterns are the efficiency layer on top.

## 3. New lever: capacity-denominator engineering (measured)

The official overflow penalty is `20·A·R + 1.5·A·max(0, R−0.05)` with `R = overflow_total / capacity_total` and `capacity_total = Σ_channels 25·(h+w)` over **the CHANNEL rows of the emitted cfg** (`cfg_score.cpp:151-171`), used or not (`writeCfg` already emits all extracted channels). Whitespace *is* capacity. This invalidates the premise of the 07-07 rejection "peripheral outline growth cannot relieve inter-core congestion" (P1 T8.1) — that was measured under the old `1e6×overflow-µm²` accounting, where the denominator did not exist.

Golden-rescorer experiment (this session): appending one derivation-consistent 1µm-wide full-height empty strip at the right edge (OUTLINE W+1, one extra CHANNEL row; no path touches it):

| case | Δcapacity | ΔR | Δoverflow_penalty | Δchecker_cost |
|---|---|---|---|---|
| 1 (+1 strip) | +55,000 (+10.0%) | −9.1% | −22,471 | **−20,272 (−0.14%)** |
| 4 (+1 strip) | +160,325 (+4.8%) | −4.5% | −354,734 | **−348,322 (−0.31%)** |

Overflow_nets and WL provably untouched — the lever is a pure denominator play. Marginal analysis on case4 (H=6412, O=37,844, C₀=3.37M): break-even at k* ≈ 139 added 1µm slivers, net saving ≈ 5.9M ≈ **5.2% of case4 cost** — *if* each sliver counted as a separate channel.

**Honest constraints discovered:**
- **Derivation consistency.** `extractChannels` (and presumably the official checker — A40's alpha feedback includes "channel 合法性") merges adjacent empty strips: k side-by-side slivers with no separating block edge are ONE channel of width k → capacity 25·(H+k), not k·25·(H+1). The measured 5-sliver row (−1.29% on case4) is therefore an *upper bound reachable only by hand-splitting channel rows*, which risks the channel-legality check. Do not hand-split.
- Within derivation-consistent geometry, the first µm of a new peripheral margin captures almost all of that margin's gain (+25·H capacity); extra width adds only 25/µm. Distinct channels require distinct strips, which require real block x-edges.
- The safe sub-levers, in increasing effort:
  a. **Peripheral margin rungs in `outlineSweep`** (S): add rungs that pad declared W and/or H by +1µm (all four sides where MAX allows) before shrink-clamping; the sweep's existing exact-score adjudication decides per case. Immediate measured value: −0.1..−0.3%/case for a few lines.
  b. **All-margins variant** (S): a 1µm bottom/top margin is sliced by every block x-cut into ~#strips channels (Σcap ≈ 25·(#strips + W)) — comparable value to a side margin, and it *is* derivation-consistent because the cuts come from real block edges.
  c. **Capacity-profit respace mode** (M): P3's `relaxToDemand` machinery, but driven by the official marginal objective `d(20·A·R)/d(gap width)` instead of demand deficit — interior gaps both add capacity and relieve real congestion. Under the same Pareto-safe exact gate as P3.
  d. **Re-evaluate shrink aggressiveness** (free): the sweep already carries MAX-like rungs; after (a) the area-vs-capacity trade is fully adjudicated by exact score per case. Expect the optimum to stop at *less-than-maximal* shrink on overflow-bearing cases.
- **Risks:** β_lin/β_tail/θ are alpha-package reverse-engineered (the PDFs never publish the formula — confirmed by the full 07-18 Q&A re-read); the tail residual (case1, 0.31%, H1-gate) is still open with the organizers; a final-checker change to channel derivation or capacity definition would shift the calculus. All sub-levers stay inside "real whitespace, real block edges", so worst case they degrade to neutral-minus-area-cost, which the exact-scored sweep rejects automatically.

## 4. Router pricing: port the per-edge official marginal cost (issues (a)+(e) unified)

Known-issues (a) `pathSlack` entry-axis overshoot and (e) route_cost still 1e6-area-denominated are one fix: price Dijkstra edges and chunk sizing with the *official* marginal cost under per-edge accounting:

- Per-edge demand (byte-verified): every channel entry AND exit edge charges the full chunk net count to that edge's axis; a straight pass-through charges its axis twice; an L-turn splits the charge across H and V ledgers. Route shaping consequences the current pricing cannot see: (i) fewer channel crossings = less demand *and* shorter guiding-point chains (WL); (ii) on a channel whose H-ledger is saturated but V is not, turning inside the channel halves the H charge; (iii) `pathSlack` must take the min over per-edge residual capacities with both charges counted (fixes the overshoot).
- Marginal overflow cost of adding n nets to a specific edge ledger is `20·A·Δ(overflow)/C` (+tail term when R>θ) — cheap to compute incrementally; replaces the 1e6-denominated `channelMarginalCost` weights.
- Literature check (new): no 2024–2026 paper addresses boundary-crossing-count demand models at floorplan level (nearest: Bonn's Dynamic Local Usage, DAC 2025 — grid-GR, concept-level support only). This is contest-specific math; internal derivation is the right approach.

## 5. WL-first search agenda (the new largest lever)

With WL at 45–63% of cost, the twice-deferred P4b/P4d items are now the core, enriched by this round's finds:

1. **Bundle-weighted abutment credit in `cheapScore`** (P4b, unchanged rationale, higher priority): A31 — abutted pairs route port-to-port with **zero WL, zero channel demand, no density limit**. The proxy must see the cliff; today it cannot steer toward it.
2. **Wire-weighted median move** (new, from the 2023 Problem D winner's source — first actual read this session): relocate a block toward the bundle-weighted *median* of its connected partners' centers (per-module HPWL-optimal target), blended with noise that anneals out (`curLD = ratio·rand + (1−ratio)·optimPos`). Directly implementable in both engines' mutate via `seedFromPlacement`-style reinsertion.
3. **Greedy-repositioning mutation** (EGPlace, ICML 2025): systematically pick the block contributing worst to the objective and re-place it at its best legal position, as a move type — complements (2).
4. **Edge-alignment as a WL/demand optimization** (new insight from the official-PDF re-read): A41 forces paths through overlap-boundary midpoints; more distinct block x-edges → more strip cuts → more forced zigzag and more charged crossings. Aligning block edges (snap-to-neighbor-edge move) shortens chains and reduces per-edge demand. **Tension**: fewer cuts also means fewer distinct channels (less capacity denominator, §3) — per-case, exact-scored adjudication required.
5. **Scale-free SA acceptance** (2023-D winner): relative-delta normalization (`Δ/current`) makes one temperature schedule robust across cases whose costs span 1e7–1e8 — our absolute-delta schedule is provably mis-tuned on at least one end. Cheap experiment: FastFP-style rejection-ratio-adaptive cooling (JCSC 2025) as a follow-on.
6. **Anytime output** (2023-D winner): write the best-so-far cfg on every new legal best — mandatory de-risking before any `--time-budget` contest run (A42: a 2h overrun with no output = fail).

## 6. External delta — everything else found (and what it closes)

- **Contest site (checked 07-23):** E alpha results still unpublished (A40 promised 07-17 — overdue; will include per-metric breakdown incl. subsys area). The 3 extra testcases (A38, final = 8 total, ETA 07-15) also overdue. **Watch daily; re-run the full sweep the day they drop.** No evaluator will ever be provided (A2). Thread policy: still unstated anywhere.
- **`wee-tw/problemE`** (new repo find): not a solver, but a serious spec-faithful third-party Evaluator.cpp (43KB + 30KB tests) implementing the *literal* spec reading (overlap-segment midpoints, per-direction ledgers, turn counting). Ready-made material for the P4e independent-evaluator cross-check; also a concrete embodiment of the literal-vs-center-anchored WL discrepancy (our center-anchored chain is byte-verified against the alpha checker; the literal reading is what the PDF says — keep the dual awareness for hidden-case risk).
- **2023-D winner deep read** (closes survey item #8): full architecture extracted — corner-stitching geometric-state SA (not B*-tree/SP), wire-median targeting, adaptive penalty-weight escalation with band retry on zero-legal-progress, legal-only elite snapshots with restore-between-bands, dedicated cheap post-SA whitespace-redistribution phase, construct-repair fallback legalizer, anytime output. Items absorbed into §5; the representation itself is not needed at n<50.
- **Papers delta (2025–2026, full exclusion list applied):** worth knowing — TOFU (DATE 2023, constraint-graph two-step refinement; same family as our respace, read for §3c design), FastFP (adaptive SA), EGPlace (§5.3), JigsawPlanner (ICCAD 2024, JV-assignment whitespace elimination — note: we now *want* selective whitespace), ICCAD 2024 NTU rectilinear-soft-module analytical FP, GrandPlan (ISPD 2026, direction validation only), ISQED 2025 feedthrough-insertion (industrial validation of the FT-relieves-channels model). Clean negatives: no congestion-driven *floorplanning* literature 2023–2026; no per-edge-crossing demand-model literature; FTAFP and Piano have no public code; FTAFP has no algorithmic successor yet. Blind spot: DAC 2026 (titles not yet published).
- **Do-not-repeat list:** unchanged and re-confirmed; nothing this round argues to reopen MCMF/Lagrangian, parallel tempering, MB*-tree, staircase channels, RL/GPU, analytical-engine replacements, etc.

## 7. Updated pre-beta priority ladder (8 days)

| # | Item | Sections | Effort | Expected effect | Risk |
|---|---|---|---|---|---|
| **B1** | Fix `acceptFeedthroughPaydown` + `paydownBetter` orderings (value-gated, mismatch as near-tie tiebreak); re-run T5 sweep matrix | §2 | **S** | Budget becomes monotone; unlocks B2; removes a live several-% failure mode | Low (keep mismatch-0 tiebreak; regression-test the T5 fixtures) |
| **B2** | Time-budget portfolio: multi-seed × {sp,bstar,hybrid} restarts merged by exact score; anytime cfg writing; VAL-style restart lengths optional | §2, §5.6 | **S–M** | Converts bstar's measured 5.8× seed variance into wins; first real use of the 2h/8-case budget | Low after B1 |
| **B3** | Capacity margin rungs in `outlineSweep` (+1µm side/top/bottom variants) | §3a-b | **S** | Measured −0.1..−0.3%/case immediately; more where MAX slack allows | Low (exact-scored rungs; derivation-consistent only) |
| **B4** | Official per-edge marginal pricing in route_cost + pathSlack (issues a+e) | §4 | **M** | Harvests part of the remaining 13.9M overflow penalty; shorter chains help WL too | Medium (router behavior change — full suite + sweep gate) |
| **B5** | Abutment credit in cheapScore + wire-median / greedy-reposition moves | §5.1-3 | **M** | Attacks the 45–63% WL share; the largest open headroom | Medium (proxy change → verify with τ-harness, B6) |
| **B6** | Kendall-τ cheap-vs-exact harness (the twice-deferred measurement) + adaptive exact-interval | §2 lit | **S** | Turns proxy rot into an observable; gates B5's proxy edits | None (measurement) |
| **B7** | Capacity-profit respace mode | §3c | **M** | Interior capacity on cases with interior slack | Medium |
| **B8** | Evaluator cross-check vs `wee-tw/problemE` + send organizer QA (add: FT-deduction form, channel-legality definition, runtime-penalty applicability) | §6 | **S** | Hidden-case insurance; resolves β_ft/tail/α uncertainties | None |

Deferred (post-beta / P5, unchanged): SDS shaping (P4c), successive-halving elite race on a partial-routing fidelity ladder, GP/SOCP sizing, BCSA crossover, flow-based proxy.

## 8. Watch items & open questions

1. **E alpha results and 3 extra testcases are both overdue** — check `iccad-contest.org/tw/02_news.html` daily; re-sweep and re-prioritize the day either drops.
2. **β constants are reverse-engineered, not published** (full-PDF re-read confirms the PDFs contain no formula); tail residual 0.31% open; β_ft=3.0 never exercised (all alpha mismatch 0). QA draft (`docs/official/qa-draft-20260722.md`) should gain: exact overflow/FT deduction forms, whether channel rows are validated against the reference derivation, and whether the alpha ranking's runtime penalty (0.5·t·√A) applies at final — the last one governs how much of the 2h budget is actually free.
3. **α contradiction** (CSV 0.2 vs alpha scoreboard per-case 0.3/0.15/0.1/0.1/0.1) still pending with organizers; keep everything α-parametric and keep the dual scoreboard.
4. **Literal vs center-anchored guiding points** (spec text vs byte-verified checker): center-anchored is correct for the alpha checker; if the final checker moves to the literal reading, WL math changes — the `wee-tw` evaluator gives a free second implementation to quantify the gap on our outputs.
5. `channelBounds` wrong-pair attribution (P3 minor) becomes load-bearing if B7 consumes it — re-check then.

## 9. Sources

Internal: `.superpowers/sdd/progress.md` + beta task reports; `src/search.cpp`, `src/eval.cpp`, `src/cfg_score.cpp` (code-level verification); `/tmp/ar_lever/` measurement (this session; commands recorded in the experiment log). Official: `docs/official/E_20260608.pdf` (16pp full), `docs/official/E_QA_20260717.pdf` (18pp full, Q1–Q44), iccad-contest.org news/problems pages (fetched 2026-07-23). External: Jin et al. IEEE TEC 2002; Jin Soft Computing 2005 / SEC 2011; Loshchilov et al. GECCO 2012 (saACM-ES); Lau & Ho JOTA 1997; Ho/Zhao/Jia 2007; Karnin ICML 2013; Jamieson & Talwalkar AISTATS 2016; Li et al. JMLR 2018 (Hyperband); Chen et al. 2000 (OCBA); Gelfand & Mitter JOTA 1989; Ceperley & Dewing JCP 1999; Branke et al. J. Heuristics 2008; Alexandrov et al. 1998 (trust region); Peherstorfer et al. SIAM Rev 2018; Kandasamy et al. NeurIPS 2016/JAIR 2019; Toal SMO 2015; RISA ICCAD 1994; RUDY DATE 2007; SimPLR ICCAD 2011; Ripple 2.0 DAC 2013; TOFU DATE 2023; FastFP JCSC 2025; EGPlace ICML 2025 (PMLR v267); JigsawPlanner ICCAD 2024; NTU rectilinear-soft ICCAD 2024; GrandPlan ISPD 2026; Sakariya et al. ISQED 2025; Bihler & Blankenburg DAC 2025 (DLU); `github.com/johnnyhuang1007/iccad-2023-problem-D` (MIT, full source + Mandarin report read); `github.com/wee-tw/problemE`; `github.com/lamda-bbo/DAC25-ReMaP`. Full URLs in the session's research-agent transcripts.
