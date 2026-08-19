# Beta 8/17 rerun: ranking position + final plan to 8/28 (2026-08-19)

Basis: official per-team evaluation package `docs/official/beta_test/cade0002.zip` (+ `result.html`,
`calculation.html`, `case_result.json`) dated "Beta Resubmission Evaluation Run 2026-08-17";
`E_Beta_Ranking(20260811).pdf` (all 9 table images re-extracted in this session via raw
PDF-object → PNG, read visually); ledger `.superpowers/sdd/progress.md` L74–77 +
`2026-08-17-case5-r2-tuning-900s-and-l2a-verdicts.md`; external check on the "VLSICAD 2026
GNN-seeded SA" paper (subagent, arXiv/S2/DBLP/OpenAlex/Scholar sweep). Final deadline is now
**2026-08-28** (user-confirmed).

## TL;DR

1. **The 8/17 rerun evaluated the 8/12 static resubmission (7/31-era code), not current HEAD.**
   8/8 PASS, total cost **322,102,436** (ΣWL 1,340.8M, ΣArea 126.3M, rt penalty 7.60M).
   Of that, **case5 alone is 184.8M — 95.1M of it overflow+FT penalties** (ft_area_excess
   30.93M × β_ft 3) that current HEAD already eliminates (case5 now ~55M all-in).
2. **Position: the evaluated package lands ≈ rank 4 of 41** in the 0811 field (5 teams had 8/8:
   ranks 1,2,4,5,7). **Current HEAD (≈182M checker + ~8M rt ≈ 190M) would sit in the rank 2–3
   zone**; champion composite ≈143M (+rt ≈151M) leaves us ~+26%. Caveat: every other team also
   got the resubmission window, so the real final field is stronger than the 0811 snapshot.
3. **Runtime penalty is live and measurable**: `δ·Runtime·√OutlineArea`, δ=0.5, verified digit-
   level on all 8 cases (case0: 0.5×599.692×√3,858,480 = 588,988 ✓). Wall-clock is now a direct
   cost term — 7.6M on the evaluated package, ~4% of a current-HEAD-sized total.
4. **The official machine is ~2.6–3.0× faster than our WSL box**: the identical deterministic
   case5 run (same WL/OA/overflow as our §3.2 measurement) took **272.98s officially vs 828.8s
   locally** (clean local est 700–780s). 900s cap risk is effectively dead; our 600s budget buys
   ~2.5–3× the local iteration count on their hardware; plateaus arrive earlier in wall time.
5. **The "VLSICAD 2026 GNN-seeded SA" paper is REAL** (correction, same day): it is S30-1 at
   the 37th VLSI Design/CAD Symposium (Taiwan, 2026-08-05/07), Wei/Chu/Huang/Chiang/Tseng/
   Lin/Lin, **Yuan Ze University** — invisible to arXiv/DBLP/S2 because the domestic symposium
   is unindexed. The title mirrors Problem E exactly → almost certainly contest-adjacent work,
   possibly a competing team. Full-text hunt in progress (§4). Independent of its content, the
   neighboring indexed literature (PARSAC, GraphPlanner, Basso DATE'25) still calibrates the
   approach: well-engineered parallel constraint-aware SA beats published GNN/RL floorplanners,
   and the reusable ideas are model-free (spectral seeding, routing-space padding, net
   ordering, repair moves) — §4.
6. **Gap structure has rebalanced**: after the case5 campaign (91.1M→54.9M), the per-case gaps to
   the 0811 champions are case5 +11.7M, **case4 +11.2M**, case3 +5.6M, case2 +4.4M, case7 +3.0M,
   case1/case6 +1.2–1.3M, case0 +0.2M. case4+case3+case2 (+21.2M) now outweigh case5. The final
   week is a **WL/whitespace campaign on cases 2/3/4** plus case5 continuation, plus two new
   cost-model levers (plateau-stop, parallel legs) unlocked by the runtime-penalty confirmation.

## 1. What the 8/17 package contains

Official evaluation of the **8/12 static resubmission** (glibc fix re-link of the 7/31-era
solver — none of L6-auto/P0a/L3bc/L4b/L4c/D2/C1a/D3/B1/notch is in it). All 8 cases CORRECT/PASS,
checker_pass true, no edge violations, no illegal FT.

| case | WL | OA | base (OA+α·WL) | ovf+FT pen | rt pen | official cost | runtime |
|---|---|---|---|---|---|---|---|
| 0 | 1,491,500 | 3,858,480 | 4,305,930 | 0 | 588,988 | 4,894,918 | 599.7 |
| 1 | 10,545,985 | 5,687,600 | 7,269,498 | 0 | 715,537 | 7,985,035 | 600.1 |
| 2 | 93,861,474 | 27,243,702 | 36,629,849 | 0 | 1,537,306 | 38,167,156 | 589.1 |
| 3 | 71,893,228 | 15,891,043 | 23,080,366 | 3,746,633 (ft_excess 1.25M) | 1,171,098 | 27,998,097 | 587.6 |
| 4 | 140,303,704 | 24,816,540 | 38,846,910 | 874,297 (ovf 2,521) | 1,464,502 | 41,185,709 | 588.0 |
| 5 | 980,387,256 | 39,814,170 | 88,833,533 | **95,089,991** (ft_excess 30.93M ×3 = 92.78M + ovf 7,838 → 2.31M) | 861,237 | **184,784,761** | **273.0** |
| 6 | 7,269,700 | 3,901,350 | 5,355,290 | 0 | 591,645 | 5,946,935 | 599.1 |
| 7 | 35,090,090 | 5,040,000 | 10,303,514 | 164,271 (ovf 580) | 672,040 | 11,139,825 | 598.7 |
| Σ | 1,340,842,936 | 126,252,885 | 214,624,890 | 99,875,192 | 7,602,354 | **322,102,436** | 4,435.1 |

Two structural findings:

- **The FT-area-excess penalty is the old binary's dominant loss** (96.5M of the 99.9M penalty
  total). Current HEAD's case5 pipeline (L6 auto-merge + deep mode + L4c) lands ~55M all-in with
  0–208 overflow nets, i.e., this 95M is already recovered in-tree. case3's 3.75M ft penalty is
  also absent from current HEAD's aligned self-score (23.08M includes its own penalty terms,
  golden-rescorer-exact).
- **V5 constants confirmed in production**: α per case (0.3/0.15/0.1/0.1/0.1/0.05/0.2/0.15),
  β_lin 20, β_tail 1.5, β_ft 3, θ 0.05, γ 1, λ_ft 10, **δ 0.5**, M 1.8
  (`calculation.html` header table).

## 2. New intel from the rerun

**Runtime penalty (δ·rt·√OA) is real, on wall-clock, on the official box.** Verified against
`case_result.json` to rounding on all 8 rows. Pricing intuition: 1 second costs `0.5·√OA` µm² —
982/s on case0, ~2,600/s on case2, ~3,150/s on case5-sized outlines. A full 600s run costs
0.35–1.6M per case. This makes *time spent past the plateau pure rank loss*: R2 (08-17 doc §2)
measured that budget past ~600s buys ~0 on 7/8 cases, and the 750s case5 run reproduced the 600s
score exactly. The same logic now applies *within* 600s.

**Official hardware ≈2.6–3.0× our WSL clean-run speed** (case5 deterministic workload: 273s vs
700–830s local). Implications: (a) the 868–899s local walls at 900-budget would be ~300–350s
there — the 2h aggregate is comfortable (measured: 4,435s of 7,200s at 600s budgets); (b) our
600s budget delivers ~2.5–3× the local iteration count, so local A/B deltas transfer
directionally but plateau earlier in wall time on their box — the early-stop lever (§5-P1) has
*more* headroom officially than locally, and per-case budget "knees" measured locally are
conservative; (c) never-timeout posture is now cheap insurance rather than a live risk.

## 3. Ranking position

Field recap (0811 PDF, re-read this session): 41 teams, 131/328 PASS, five 8/8 teams
(overall ranks 1, 2, 4, 5, 7), overall sort consistent with Σ official cost (fails filled with
worst_cost×M). Top-10 overall: r1 8/8 (ΣWL 543.9M / ΣA 109.1M), r2 8/8 (1,189.9M / 121.9M),
r3 7/8 (963.4M / 114.1M), r4 8/8 (895.0M / 109.3M), r5 8/8 (1,794.5M / 148.5M), r6 7/8,
r7 8/8 (1,911.6M / 137.9M), r8 6/8, r9 7/8, r10 6/8.

**The 8/17 evaluated package (ΣWL 1,340.8M / ΣA 126.3M / cost 322.1M) slots ≈ rank 4**: behind
r1/r2/r4 (better on both aggregate axes; r4's 8/8 at 895M/109.3M ranked below 7/8-r3 only because
of masked penalties), ahead of r5/r7 (worse on both axes) and of every non-8/8 team (a single
fail adds ≥1.8×worst_cost; a case5 fail alone ≈ +810M).

Per-case placement of the *evaluated* result vs the 0811 per-case tables (visible basis =
OA+α·WL; official per-case tables mask penalties, so positions are approximate where penalty-
heavy rows sort out of visible order):

| case | champion (OA+α·WL) | evaluated us | ≈pos | current HEAD | ≈pos (HEAD) | gap to champ (HEAD) |
|---|---|---|---|---|---|---|
| 0 | 4,119,777 | 4,305,930 | 3 | 4,305,930 | 3 | +4.5% |
| 1 | 5,953,680 | 7,269,498 | ~6–8 | 7,229,638 (L6) | ~6–7 | +21.4% |
| 2 | 32,182,151 | 36,629,849 | ~6 | 36,629,849 | ~6 | +13.8% |
| 3 | 17,468,456 | 26,826,999 (incl ft pen) | ~8–9 | 23,080,366 | ~7 | +32.1% |
| 4 | 28,486,445 (r2) | 39,721,207 (incl ovf) | ~8–9 | 39,721,207 | ~8–9 | +39.4% |
| 5 | 43,250,763 | 183,923,524 (incl pen) | ~10–11/13 | 54,917,186 | ~4 | +27.0% |
| 6 | 4,153,201 | 5,355,290 | ~6–7 | 5,355,290 | ~6–7 | +28.9% |
| 7 | 7,516,300 | 10,467,784 (incl ovf) | ~3–4 | 10,467,784 | ~3–4 | +39.3% |

**Current-HEAD projection**: Σ aligned ≈ 181.7M + rt ≈ 8.3M (all cases now run ~600s incl.
case5) ≈ **190M all-in** → between r1 (≈151M all-in est) and the r2/r4 cluster — the rank 2–3
zone of the 0811 snapshot. Absolute gaps: case5 +11.7M, case4 +11.2M, case3 +5.6M, case2 +4.4M,
case7 +3.0M, case1 +1.3M, case6 +1.2M, case0 +0.2M. **The former case5 monopoly (once 1.8× the
sum of all others) is over; cases 2/3/4 combined (+21.2M) are now the larger pool.**

Honesty notes: (i) the 0811 snapshot predates everyone's resubmission — treat ranks as
directional, not final; (ii) final evaluation adds hidden cases; (iii) champion per-case costs
are underestimates (their penalties are masked).

## 4. The "GNN-seeded SA" paper — corrected verdict and what the literature gives us

**Verdict (corrected 08-19): the paper is real but unindexed.** First-pass search of arXiv /
DBLP / Semantic Scholar / OpenAlex / Crossref / Scholar found nothing and wrongly concluded
confabulation; the user supplied the primary source: `https://www.vlsicad2026.org/oral-sessions`
— **S30-1, Session 30 "AI-driven design analysis and optimization", 37th VLSI Design/CAD
Symposium (VLSICAD 2026, Taiwan, 2026-08-05/07)**. Authors: Shao-Yu Wei, Hung-Ying Chu, Tzu-Wei
Huang, Chi-Huang Chiang, Wang-Dauh Tseng, Yu-Cheng Lin, **Rung-Bin Lin** (Yuan Ze University —
an established floorplanning/placement group). The domestic symposium publishes no indexed
proceedings, hence the false negative. **The title mirrors Problem E's name exactly — this is
almost certainly contest-adjacent work, plausibly a competing team's method write-up.** Lesson
recorded: for Taiwan-domestic venues, check the venue site directly before declaring a citation
fictitious.

**Full-text hunt result (same day, exhaustive): no public content exists.** No abstract on the
venue site (titles/authors only; proceedings distributed to attendees only), no arXiv/TechRxiv/
S2/Scholar/ResearchGate trace, no NDLTD thesis yet (2026 theses unposted; NDLTD unreachable from
this network). Context found instead: the YZU lab (Yu-Cheng Lin + Rung-Bin Lin) is a **serial
ICCAD-contest team** — arXiv:2504.12076 targets ICCAD 2025 Problem D; arXiv:2509.03554 is ICCAD
2025 contest work sharing an author (Tzu-Wei Huang) with S30-1; and the same group has poster
P2-51 at VLSICAD 2026, "Viability of LLM-Based Soft Block Floorplanning" — Problem E vocabulary.
Their cadence: contest papers hit arXiv ~6–12 months post-contest. **Action: periodically
re-check `arXiv au:"Rung-Bin Lin"` / NDLTD (~Oct 2026+); the only method information available
today is the title itself** — GNN-seeded initialization of a routing-aware SA, i.e., the same
pipeline shape as this repo, differing in the seeder. Our model-free equivalent (`--qp-seed`
Hall/Fiedler seeding, nearmiss brief 1) is already queued for implementation; no further work
should be budgeted against the unobtainable paper text. Session chair trivia: Wai-Kei Mak
(NTHU), himself a floorplanning/routing researcher — the session was competent review.

Closest real work (subagent deep-read; PDFs in session scratchpad `downloads/`):

- **PARSAC** (Intel, arXiv 2405.05495): massively-parallel constraint-aware SA **beats**
  GoodFloorplan (GCN+RL) and Parquet on GSRC fixed-outline; constraints satisfied by
  construction (their anchored pre-placed blocks = our edge anchoring, independently validated
  by our own L2 edge-free rejection) plus rare always-accepted *repair moves* instead of penalty
  escalation. Calibration: GNN/RL floorplanners are **not** the frontier at this scale.
- **GraphPlanner** (TODAES'22): GNN regressing initial coordinates buys ~4% HPWL / 25% runtime —
  the load-bearing part is *connectivity-aware initial coordinates*, whose training-free
  equivalent is **spectral placement** (Laplacian eigenvectors of the bundle-weight matrix).
- **Basso et al.** (DATE'25 + NeurIPS-ws'25, routing-aware analog floorplanning): the
  transferable piece is **Dynamic Routing Resource padding** — reserve per-block-side gap
  ≈ ζ + (side pin/net count)×pitch; their ablation shows routability comes from HPWL-dominant
  weighting + reserved routing space, not from the learned policy. Also: net ordering by
  (past failures, HPWL, pin count) with negotiated history.

Mapped to our solver (all model-free, no training pipeline):

1. **Spectral seed leg** (= survey §2-A2 with stronger evidence): weighted Laplacian of the
   merged bundle graph → Fiedler/2nd-eigenvector coordinates (edge blocks pinned) → sort by
   x/y → seed SP via existing `seedFromPlacement`. One eigensolve at n≤30 is microseconds;
   slots in as one more portfolio/deep-leg seed beside shelfSeed. Effort S.
2. **DRR-style per-side capacity padding** as a cheap-tier term (complements survey B5 stabbing
   LB): per adjacent block pair, charge when gap < ζ + incident-bundle-nets × pitch. Effort S–M.
3. **Bundle route order = (prior failure count, then α·est length, then nets)** in
   `routeProblem` — one-line lever on the existing negotiation loop (B1 flag covers the pricing
   half). Effort S.
4. **Repair moves** (PARSAC): only relevant if any violation class is reachable post-decode;
   our anchoring already satisfies edge constraints by construction — note for archaeology, no
   action.

## 5. Plan to 8/28 (priority order, all flag-gated + A/B per B5a discipline)

Deadline math: today 8/19 + 9 days. Codex returns 8/20 (memory). Target freeze ~8/26 → static
build + rockylinux:8 glibc check (memory: submission-binaries-must-be-static) → upload buffer.

- **P0 — close the in-flight batch-2 A/Bs** (already implemented, unmeasured): `--wl-ladder`
  (D2, 4382f4f), `--wl-redistribute` (C1a, 9781725), `--snap-resize` (D3, e369ede),
  `--const-gap-negotiation` (B1, 39399e8), `--notch-penalty` (D4/A5, 075a192). 600s same-load
  3-seed batteries, case-family split: {2,3,4} for C1a/D2/D3, {4,5,7} for B1/notch, ±8M noise
  discipline on case5. These directly target the new №1 gap pool (cases 2/3/4 WL).
- **P1 — plateau-stop (runtime-penalty harvesting), NEW from this analysis**: bare-budget mode
  only — terminate when trailing best-score improvement rate < `δ·√OA` per second (the official
  marginal price of a second) for k consecutive windows; anytime output already guarantees
  safety, B2 fold guarantees no value regression. Evidence: case0/1/6/7 sit at their final
  score long before 600s; each saved 100s ≈ 0.1–0.3M official cost, potential ~1–2M across the
  small cases — case0's *entire* champion gap is 0.19M. Effort S–M, risk low. Local plateau
  detection transfers conservatively to the 3× faster official box (§2b).
- **P2 — parallel portfolio legs (pthread), decision gate 8/23**: the 0811 checklist's
  recommended compile line includes `-pthread`, no thread rule (08-17 doc §3); with the runtime
  penalty on *wall*, running the 8-leg registry on N threads divides the dominant wall term and
  either banks δ-savings or buys deep-mode depth everywhere. This is the one big architecture
  change worth the extended deadline. Requirements: deterministic fold (fixed leg→thread
  assignment, per-leg RNG already isolated; fold order already fixed), static pthread build +
  rockylinux verification, Codex adversarial review (available 8/20+). If determinism or the
  static build is not airtight by 8/23, drop without regret — P0/P1 don't depend on it.
- **P3 — WL campaign on cases 2/3/4** (now co-equal with case5): whichever of C1a/D2/D3 survives
  P0 goes default; then **C3 LNS exact local re-placement** (survey §4-C3, BonnMacro pattern —
  briefs already at `c3-wlrec-briefs-20260817.md`) and **#8 WL recovery reassignment** (survey
  §3-B3-S). Add the **spectral seed leg** (§4.1 above) as a cheap diversity bet on the same
  battery.
- **P4 — case5 continuation**: C1a/notch A/B on case5 (new-mechanism track per 08-17 §5 —
  knob-turning is exhausted); optionally DRR padding (§4.2) as a case5-targeted cheap-tier
  term (its congestion term is identically zero there — case5-design finding). Keep 3-seed
  same-load protocol; single-cell deltas inside ±8M are noise.
- **P5 — submission hygiene**: refresh `submission/` from HEAD *now* so any surprise freeze
  ships current case5 (the evaluated package is 130M worse than HEAD — the cost of shipping
  stale binaries is proven); static + docker glibc check before every upload; final battery =
  full 8-case 600s same-load + golden rescorer + selfconsistency.

Explicitly **not** doing: GNN/RL anything (fictitious trigger paper; PARSAC evidence; static-
binary + 9-day constraints); 900s budgets (R2: Pareto-bad); more deep-mode knob probes (5
consecutive rejections); per-case adaptive budgets (F1 §5 overfit risk stands).

## Evidence

- `docs/official/beta_test/{cade0002.zip,result.html,calculation.html,case_result.json}` —
  8/17 rerun; V5 constants table; per-case breakdowns (extracted copy in session scratchpad).
- `E_Beta_Ranking(20260811).pdf` images 14/19/20/24/25/29/30/34/35 — overall + 8 per-case
  top-10 tables (re-extraction script pattern: raw PDF object scan → zlib → PNG; ~40 lines).
- Runtime-penalty formula check: all 8 rows reproduce `cost_breakdown.runtime_penalty` via
  0.5·runtime_sec·√outline_area (max rel err <1e-4, rounding-level).
- Subagent paper-search report (session task output): PARSAC / GraphPlanner / Basso / EAGAT /
  GoodFloorplan / FloorSet details + negative search evidence.
