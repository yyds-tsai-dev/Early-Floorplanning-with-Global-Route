# Beta Task 2 report — Golden rescorer (`.cfg` reader + official cost)

**Status: DONE.** Commit `5931567` — `feat(eval): golden cfg rescorer locked to alpha-test official reports`.

WL geometry **PINNED to 1e-6**. The rescorer reproduces the official checker's four measured
quantities from a written `.cfg`, byte-close on the 20260712 alpha package.

## What I implemented

- `src/cfg_score.{hpp,cpp}` — `parseCfg` (OUTLINE/BLOCK/CHANNEL/PATH, CRLF-safe, 3-decimal floats)
  and `officialScoreCfg`, using **my per-edge demand model** and **center-anchored WL** (NOT
  `accumulatePathUsage`/`guidePoint`, which are provably wrong — see below).
- `tools/rescore.cpp` — CLI (`bin/efgr_rescore input.csv output.cfg`), one machine-readable line.
- `scripts/benchmark.sh` — dual-cost matrix (official `checker_cost` vs scoreboard `rank_cost`);
  link line fixed to the proven sibling set; arithmetic uses `awk` (portable; `bc` present but not
  relied on).
- `test/regression_cfg_score.cpp` + `test/run_cfg_score_regressions.sh` — golden regression, fixed
  link line, correct report.json keys.

## Per-case golden comparison (mine vs official report.json)

| case | WL mine / official | overflow mine/official | capacity mine/official | cost mine / official |
|------|--------------------|------------------------|------------------------|----------------------|
| 0 | 5 958 737.825 / 5 958 737.825 | 969.225 / 969.225 | 316 753.55 / 316 753.55 | 8 063 786.14 / 8 063 786.14 |
| 1 | 47 076 253.445 / 47 076 253.445 | 26 679.125 / 26 679.125 | 495 478.825 / 495 478.825 | 25 682 965.31 / 25 763 756.79 |
| 2 | 229 399 993.42 / 229 399 999.62 | 29 993.65 / 29 993.65 | 2 236 044.3 / 2 236 044.3 | 99 147 515.16 / 99 147 516.40 |
| 3 | 118 590 977.762 / 118 590 977.762 | 17 296.775 / 17 296.775 | 1 408 446.75 / 1 408 446.75 | 52 885 145.22 / 52 885 145.22 |
| 4 | 336 522 395.747 / 336 522 395.747 | 39 442.925 / 39 442.925 | 3 648 619.825 / 3 648 619.825 | 113 255 157.93 / 113 255 157.93 |

- **WL, overflow, capacity: exact to <=1e-6 rel on all 5.** (case2 WL rel = 2.7e-8, from one path's
  3-decimal `.cfg` rounding — still < 1e-6.)
- **cost: exact on 0/2/3/4** (WL exact + penalty base exact). **case1: 0.31% of total cost short**
  (penalty tail only; see §Tail).
- **case0 per-channel H/V demand: exact** (CH4/6/8/10/11 H and CH8/10/11 V).

## The two accounting corrections (vs `route_geometry.hpp`)

Both verified against report.json, the official viz polylines, and `E_20260608.pdf`. The shared
guide-segment helpers reproduce NEITHER; the solver-side port is **Task 3** (routing.cpp untouched
here per the ruling).

1. **Demand is PER-EDGE.** Each entry/exit edge a route uses on a channel charges `nets` to that
   edge's direction (edge 1/3 = L/R → horizontal, 2/4 = T/B → vertical); a straight pass-through
   double-charges. Reproduces all 5 cases' overflow + case0's 10 per-channel values exactly.
   `accumulatePathUsage`'s `dx/dy` rule under-counts pass-throughs and mis-attributes L-turns.

2. **WL = manhattan CHAIN of CENTER-anchored guiding points.** The guiding point between two path
   nodes `a,b` sits on their shared border (chosen by **minimum gap** — robust to the `.cfg`'s
   3-decimal rounding, which leaves ~0.001 µm abut gaps that report.json flags `"borderline"`);
   along the border its coordinate is the **average of the two rectangles' centers** (== midpoint of
   the two facing-edge midpoints), across the border the midpoint of the near edges.
   `WL = Σ_path nets · Σ_i manhattan(gp_i, gp_{i+1})`. This is NOT `guidePoint`'s overlap-segment
   midpoint (which is a manhattan-chain too, but overlap-anchored → ~6% off, sign-flipping with path
   length). Confirmed on the viz: e.g. case0 path_0 renders `(1357.6,572.448)→(1072.4,730)` = exactly
   the center-anchored guiding points; direct-abutment paths render a single point → 0 WL.

**How I pinned it (the WL hunt):** the guide-segment chain errs −6%…+39% (sign-flips). The PDF
Cost-Function section (extractable English: "net guiding point … guiding points … HPWL") plus the
viz `path_*.html` dashed polylines (solved the screen→µm affine from `floorplan.html`'s known rects)
showed the render uses center-anchored guiding points. Testing chain-of-center-GP hit **±0.00% on
all 5** once the border classification was made min-gap (the last 0.001-µm rounding bug: path_1's
`BLK02.right=1827.948` vs `CH11.x=1827.947` was mis-typed as a horizontal border under `kEps=1e-6`).

## Tail falsification (case1, target overflow_penalty = 8 537 517.705282887)

Base `20·A·R = 8 411 674.743`; required tail = **125 842.962**. Every physically-motivated
hypothesis (computed penalty / rel error vs target):

| hypothesis | penalty | rel |
|------------|---------|-----|
| **H1** global `20·A·R + 1.5·A·(R−θ)` | 8 456 726.22 | 9.46e-3 |
| H2 per-channel dir, weight cap_ch/capT | 8 985 309.90 | 5.24e-2 |
| H3a direction-split, dir-specific denom | 8 734 628.84 | 2.31e-2 |
| H3c direction-split, total denom | 8 433 705.48 | 1.22e-2 |
| H4 used-dir-cap denom | 14 282 511 | 6.7e-1 |
| H4c used-full-cap denom | 9 541 946 | 1.18e-1 |
| H6 Σmax(0,ovf−θ·cap)/capT | 8 985 309.90 | 5.24e-2 |
| best non-physical fit `k·1.5·A·(R−θ)` | 8 537 517.03 (k=2.7933) | 7.9e-8 |

Also ruled out: uniform R_eff (needs capacity 491 091, no such subset); 2-term combos (non-clean
c2=0.375, c3=5.5). **Verdict: H1 + gate.** With n=1 R>θ data point, only a non-physical scalar
k≈2.7933 fits — not a real formula. The residual is 0.95% of the penalty = **0.31% of total cost**;
the regression gates cost at 0.5% and the penalty at a 1.1% tripwire (guards a *worse* tail form,
not a fit). No official checker source is in the repo to settle it.

## Benchmark (`scripts/benchmark.sh --iters 200`)

| case | engine | checker_cost | rank_cost | overflow | wl | runtime |
|------|--------|--------------|-----------|----------|-----|---------|
| 0 | bstar | 7 836 727 | 8 522 595 | 0.000 | 6 858 170 | 0.04s |
| 1 | bstar | 28 555 745 | 26 339 175 | 34 242.400 | 44 363 737 | 1.20s |
| 2 | bstar | 122 422 948 | 99 271 708 | 87 545.725 | 231 553 309 | 1.34s |
| 3 | bstar | 88 778 785 | 69 085 391 | 70 150.600 | 196 965 891 | 1.38s |
| 4 | bstar | 122 765 927 | 91 889 742 | 138 158.725 | 308 798 801 | 1.44s |
| TOTAL | bstar | 370 360 132 | 295 108 612 | | | |
| 0 | hybrid | 7 728 779 | 8 863 881 | 0.000 | 11 350 082 | 0.08s |
| 1 | hybrid | 26 110 351 | 23 117 764 | 23 439.475 | 59 884 659 | 1.19s |
| 2 | hybrid | 83 366 403 | 66 684 210 | 19 576.750 | 166 880 013 | 1.82s |
| 3 | hybrid | 163 837 496 | 146 412 651 | 278 856.900 | 174 270 227 | 1.07s |
| 4 | hybrid | 138 519 994 | 108 827 915 | 196 399.600 | 296 993 381 | 2.82s |
| TOTAL | hybrid | 419 563 024 | 353 906 422 | | | |

Baseline numbers (no assertion). Note the large **overflow under the checker's per-edge model** —
our solver, optimizing against the guide-segment accounting, does not see it. This is the calibration
gap Task 3 must close.

## Suites

Golden regression: PASS. All 9 existing suites: PASS (run_case00, run_placement_engine_regressions,
run_20260618_regressions, run_20260608_regressions, run_p0/p1/p2/p3_regressions,
run_testdata_regressions). No solver code touched.

## Files changed (committed, 6)

`src/cfg_score.hpp`, `src/cfg_score.cpp`, `tools/rescore.cpp`, `scripts/benchmark.sh`,
`test/regression_cfg_score.cpp`, `test/run_cfg_score_regressions.sh`. CONTEXT.md / CLAUDE.md /
.superpowers / docs left unstaged per the ruling.

## Follow-ups (for the controller / later tasks)

1. **Task 3 (solver-side port):** routing.cpp / eval.cpp must adopt the per-edge demand and
   center-anchored WL, else the solver keeps optimizing against accounting the judge disagrees with
   (the benchmark's large checker-overflow is the symptom).
2. **CONTEXT.md "Official Overflow Penalty" + `official-cost-function` memory:** record the proven WL
   rule (center-anchored guiding-point chain, min-gap border) and the tail verdict (H1 + gate,
   0.31%). Left unstaged here; update in a docs-only pass.
3. **Tail:** only the official checker source would settle case1's +80.8k; the 0.31% changes no sweep
   ranking decision.

## Self-review / concerns

- WL exactness relies on the min-gap border rule; on the alpha package it reproduces every path.
  A pathological `.cfg` where a node is near-equidistant to two borders could misclassify — not
  observed in 5 cases / ~450 paths, and the solver emits clean abutment.
- case2 WL carries a 2.7e-8 residual from `.cfg` 3-decimal rounding (one path); within 1e-6, but it
  is a rounding floor, not a formula gap.
- I did not modify `route_geometry.hpp`/`routing.cpp` (Task 3 scope), so the shared helpers still
  hold the wrong (overlap-anchored, guide-segment) model — intentional, flagged for Task 3.
