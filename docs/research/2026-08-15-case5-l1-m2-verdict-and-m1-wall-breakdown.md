# case5: L1 (--cheap-proxy-v2) M2 verdict + M1 wall breakdown (2026-08-15)

Basis: `.superpowers/sdd/case5-design.md` §5 (M1/M2), `.superpowers/sdd/case5-l1-brief.md`
(measurement protocol), `.superpowers/sdd/case5-l3bc-brief.md` (M1-completion). L1 landed at
`a9e70db` (flag-off byte-identity 8/8, all suites green, flag-on selfconsistency exact).

## 1. M2: v2 does NOT improve case5 — abort criterion fires, no adoption

Protocol: case5, `--iters 1000`, budget OFF, seeds 1/2/3, v2 on vs off. Same-load pairing (all
arms of one seed concurrent; seeds sequential; 22-core box). Every score below confirmed by the
golden rescorer (`checker_cost` == solver score on all 7 cfgs).

| seed | off | v2-on | Δ on-vs-off | off wall | on wall |
|---|---|---|---|---|---|
| 1 | 88,290,296 | 92,168,762 | **+4.4%** | 536.4 s | 544.2 s |
| 2 | 99,230,820 | 86,304,776 | **−13.0%** | 555.1 s | 552.0 s |
| 3 | 74,652,218 | 91,313,117 | **+22.3%** | 559.1 s | 566.7 s |
| Σ | 262,173,334 | 269,786,655 | **+2.9% (worse)** | | |

Per design §6.3's abort criterion ("if v2 does not improve case5 here, STOP — report, do not
tune"): **flag stays default-off, no adoption wiring, no tuning.** The code stays as an archived
measurement lever (same status as `--edge-free`).

### Why this is still an informative result, not a wash

- **The proxy IS more faithful, exactly as designed.** Elite-pool rank fidelity `pool_tau`:
  v2 arms 0.50 / 0.70 / 0.57 vs v1 arms 0.18 / 0.10 / −0.08; the exact-best candidate's cheap
  rank is **1** in the pool on every v2 arm vs 10/16/23 under v1. L1 fixed the thing it aimed at.
- **Fidelity is not what case5 rewards at iters 1000.** v2 arms cluster tightly (86.3–92.2M,
  spread 5.9M): faithful gradients pull every seed into the same mid-80s/low-90s basin family
  (compact OA ~41–51M, the designed direction — seed 1 hit OA 41.2M, FT mismatch −44%). v1 arms
  scatter widely (74.7–99.2M, spread 24.5M): the broken FT-bbox term acts as noise-driven
  exploration, and one seed in three struck a far deeper basin — **74.65M with OA 32.6M (our best
  case5 result ever; champion ≈ 43.3M)** — accepting 547 overflow nets at a trivial 168K penalty.
- Implication for the remaining week: the case5 gap is a **basin-discovery problem, not a
  gradient-fidelity problem**. Levers that generate structurally different starts (L4b shelf
  seed) or buy more independent attempts (L3c deep mode / B2 seed sweep at budget) are the ones
  aligned with this evidence. A future v2 experiment worth one cheap probe: v2-guided SA over
  L4b-seeded starts (fidelity may pay once the right basin is already in the pool) — but only
  after L4b lands, and only as a probe.

## 2. M1-completion: where a case5 iters-1000 run's wall actually goes

New permanent telemetry: `sa_wall=` in the stderr summary (`SearchResult.sa_wall_time_sec`,
summed across folded legs by `PortfolioFold`) — the SA-vs-machinery split now comes from ONE run.

- seed-3 flag-off run: `sa_wall=61.3s` of 559.1 s → **SA loop ≈ 11%, machinery ≈ 89%** of a
  case5 iters-1000 run (design §2.3 estimated 77% — direction confirmed, magnitude higher).
- v2 proxy per-iteration cost: `sa_wall=113.6s` on the seed-3 v2 arm — the L1c cut-line sweep is
  ≈1.85× v1's cheap-score cost per iteration.
- **L3a (pads gate) is worth ~0.6% at iters 1000** (536.4 → 533.1 s, same-load triple), vs 21%
  at iters 50. The pad-rung cost design §2.3 attributed to the elite machinery largely vanishes
  once the SA loop has warmed the exact cache and the elite pool holds well-routing placements.
  L3a remains correct (Pareto-safe, byte-identical outcome on seed 1) and is still forced inside
  deep legs, but it is not a material iters-1000 wall lever.
- L3c arithmetic at measured numbers: T0 ≈ 56 s (this machine, lightly loaded) → trigger
  10·T0 ≈ 560 vs 600 borderline here, decisive on the official-eval-class machine (0811 bare
  binary: phase 1 alone 828.8 s). M0 ≈ 50 s → N_deep = 3, ≈106 s SA slice per deep leg,
  projected deep-mode case5 total ≈ 525 s — under the 700 s ceiling with margin.

## 3. Decisions taken

1. **L1: measured, rejected for adoption, archived** (flag kept for archaeology + future probes).
2. **L3b/L3c: implemented as briefed** (budget-gated only — no new CLI flag; budget-off provably
   untouched, 8/8 byte-identity battery vs `a9e70db`). Deep-mode trigger/count/slice are pure
   unit-tested helpers (`deepModeTrigger`/`deepLegCount`/`deepLegSlice`, search.hpp).
3. **Next: M3** (case5 600 s, old-binary base vs new-binary deep mode, same-load pair, wall ≤ 700
   check), then **L4b shelf seed** — now the highest-leverage open lever given §1's
   basin-discovery evidence.

## 4. M3: deep mode is a −7.0% case5 win — and the fits-once trigger had a bad middle regime

Same-load triple, case5, seed 1: old = `a9e70db`, new = `4a656f2` (L3b/L3c, pre-L4b/pre-trigger-fix).

| arm | budget | wall | score | telemetry |
|---|---|---|---|---|
| old600 | 600 s | 573 s | 88,290,296 | phase1 10/10 + 1 phase-2 leg |
| new600 | 600 s | 500 s | 94,805,126 | deep=0 (10·T0≈500 ≤ 600), **phase1 9/10 — P0a gate cut leg 9**, no phase 2 |
| **new450** | **450 s** | **432 s** | **82,082,608** | **deep=1, 3 deep legs, 41,748 iters (2.7×), sa_wall 245.7 s (57% SA)** |

- **Deep mode works exactly as designed**: SA share went from 11% to 57%, iterations 2.7×, and the
  value-gated fold turned that into −7.0% vs old600 — at 25% less budget. Wall 432 s ≪ the 700 s
  ceiling. Best case5 600s-class result in this repo's history.
- **new600 exposed the middle regime**: with T0 ≈ 50 s on this fast machine the fits-once trigger
  stays off, yet the registry only "fits" in the P0a-gated sense — 9 shallow legs consumed the
  budget, phase 2 never ran, and the launch gate cut leg 9 (an ftagg leg, the likely 88.29M
  winner): 94.8M, WORSE than old600's noise-lucky 10/10. The old binary sat at the same cliff and
  happened not to trip. The regime between "registry fits comfortably" and "registry provably
  can't fit" is exactly where case5@600s lives on a fast machine, and it is bad on both sides of
  the trip.
- **Decision (L3c.1, `d82e05f`)**: trigger fires unless the registry fits TWICE
  (`kDeepTriggerFitFactor = 2.0`) — a healthy budget run wants repeat registry passes (B2 phase-2)
  anyway, so a budget that can't fit two passes is better spent on deep legs. case5@600s now
  deterministically enters deep mode (2·10·50 = 1000 > 600) on fast and slow machines alike;
  cheap cases are structurally unaffected (case4, heaviest non-case5: T0 ≈ 12 s → 240 ≪ 600).
- **L4b landed on top** (`d8e75df`): deep legs 1–2 now start from the best 2 of a 12-cell
  deterministic shelf-seed sweep (aspect-stamped so the shelf shapes survive seeding); the new450
  M3 arm predates L4b, so its 82.08M is deep mode WITHOUT shelf seeds — L4b's increment is
  measured by M4.

## 5. M4 (close-out): 600 s A/B with everything landed — case5 −13.6%, case4 exactly unchanged

Same-load pairs, seed 1, old = `a9e70db`, new = `d82e05f` (L3b/L3c + L4b + L3c.1). Scores are
solver stderr; case5's M3 arms were additionally golden-rescored bit-exact, and case4's two arms
agree to the last digit with each other.

| pair | old | new | Δ |
|---|---|---|---|
| case5 @600 s | 88,290,296.1 (10 legs + 2 phase-2) | **76,249,439.4** (deep=1, 3 shelf-seeded deep legs, 538,451 iters = 32×, sa_wall 436.7 s, OA 35.9M, 2,412 ovf nets) | **−13.6%** |
| case4 @600 s (guard) | 53,784,961.8911 (29 legs) | 53,784,961.8911 (29 legs, deep=0) | 0 — byte-equal trajectory |

Walls (mtime-derived; the scratch runner's own case5 wall arithmetic glitched): case5 old ≈ 573 s
(matches M3's old600 exactly), case5 new ≈ 598 s — **under the 700 s ceiling**. case4 552/554 s.

Cumulative case5 trajectory this week: submitted 0811 binary 91.1M (+110% vs champion ≈43.3M) →
old 600 s path 88.3M → **76.25M (+76% vs champion)**. The remaining gap is still
basin-shaped (champion's k≈0.85 feedthrough-dominant regime, design §3.1); the deep-mode + shelf
machinery is now the vehicle for closing it further (more deep legs / better seeds / longer
official 900 s budget all flow through the same code path).
