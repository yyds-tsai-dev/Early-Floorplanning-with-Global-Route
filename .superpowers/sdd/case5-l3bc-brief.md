# case5 L3b/L3c — exact-tier budget governor + deep mode (M1 numbers filled 2026-08-15; implemented)

**Basis**: `.superpowers/sdd/case5-design.md` §4-L3b/§4-L3c. **Status**: draft; the design's own M1
gate FAILED as predicted-sized — L3a measured **−21%** case5 wall at iters50 (474→373s,
batch-1 report) vs the design's "~55% of the whole run" estimate. Most likely cause: §2.3's
accounting counted 16 elites × 2 sweeps × 28 rungs as distinct exact evals, but `exactCached`'s
placementKey cache dedupes repeated (placement, outline-target) pairs — the second sweep and
cross-elite repeats are largely cache hits, so the elite-sweep share of wall is far below 77%.
Per design §5 M1 gate: **re-derive the §2.3 wall breakdown from measurement before implementing
L3c's arithmetic.**

## M1-completion (prerequisite, cheap)

Using M2's off-arm data (case5 `--iters 1000` budget-off, flag-off walls, 3 seeds — produced by the
L1 task) plus TWO extra runs:
1. case5 `--iters 1000 --seed 1 --overflow-gated-pads` (pads-gated share at real iters), and
2. case5 `--iters 50 --seed 1` (already have: 474s flag-off / 373s pads-on at iters50).
Derive: SA-loop share (wall(iters1000) − wall(iters50) ≈ cost of 950 extra iters × legs),
machinery share (wall(iters50) ≈ almost pure machinery), pads share (off vs pads-on delta at both
iters). THEN fix L3c's constants:
- `T0` (leg-0 probe wall with pads gating on) — from measurement, not §2.3's 35s guess.
- Machinery share `M0` per leg — same.
Update THIS brief's numbers before implementation; the structure below stands.

**M1-COMPLETION RESULTS (2026-08-15, measured — supersedes the derivation plan above).** The
implementation added `sa_wall=` to the stderr summary (SearchResult.sa_wall_time_sec summed across
folded legs), which measures the SA-vs-machinery split DIRECTLY in one run instead of subtracting
differently-loaded runs:
- case5 `--iters 1000` budget-off walls (M2 battery, same-load pairs): 536.4 / 555.1 / 559.1 s
  (seeds 1/2/3, 2-3 runs concurrent on a 22-core box).
- **`sa_wall=61.3s` of 559.1s total (seed 3, flag-off)** → SA loop ≈ **11%**, everything else
  (seed evals + elite machinery + finalize + rungs) ≈ **89%** — §2.3's "machinery dominates"
  confirmed, magnitude even higher than its 77% estimate. Per leg: T ≈ 56 s, S ≈ 6 s, M ≈ 50 s.
- v2-proxy cost check (same battery, seed 3 on-arm): `sa_wall=113.6s` — the L1c cut-line sweep
  ≈ 1.85× the v1 per-iteration cheap-score cost.
- **Pads share at iters1000 is ~0.6%** (seed-1 same-load triple: 536.4 off vs 533.1 pads-on),
  vs 21% at iters50 (474→373, batch-1) — the L3a win shrinks dramatically once the SA loop has
  warmed the exact cache / the elite pool holds well-routing placements. L3a stays correct and
  forced in deep legs, but is NOT a material iters-1000 wall lever.
- L3c arithmetic at these numbers: T0 ≈ 56 s → trigger 10·T0 ≈ 560 vs 600 — borderline on this
  (fast, idle) machine, decisively fired on slower/loaded machines (the 0811 bare binary measured
  828.8 s phase 1). That is the intended adaptivity: the trigger uses the RUNTIME-measured T0.
  M0 ≈ 50 s → N_deep = 3, per-leg SA slice ≈ 106 s, projected deep-mode total ≈ 525 s < 700
  ceiling. No compile-time constant needed T0/M0 — both are measured per run; the governor's
  kDeepMachineryShare=0.5 / kDeepRungsPerElite=14 / kDeepRepairShare=0.1 stand as implemented.

## L3b — cost-aware elite/repair budgets (design §4-L3b)

Make `kFullTreatmentSize`-driven elite count and `kFinalizeRepairAttempts` functions of the measured
seed exact-eval wall (`search.cpp:632-638` seed eval — time it) — ONLY when
`options.time_budget_sec > 0.0` (first operand, P0a precedent); budget-off keeps the constants
verbatim. Clamps per design: elites `[3,16]`, repairs `[4,32]`. Machinery-share constant from
M1-completion. New SearchOptions plumbing as needed; no Problem changes.

## L3c — deep mode (design §4-L3c, all five steps)

- `SearchResult.sa_wall_time_sec` stamped around the SA loop, summed across hybrid sub-runs.
- Leg 0 runs exactly as today (probe + anytime guarantee). `T0`, `S0`, `M0` from it.
- Trigger (budget-on only): `variants.size() * T0 > options.time_budget_sec`.
- `N_deep = clamp(floor((budget - T0) / (kPhase2LegMargin * M0)), 1, 3)` legs from a new
  `deepVariants()`: `{(official,v1,credit-off,moves-off,ftagg-ON), (official,v2,credit-off,
  moves-off,ftagg-ON), (official,v2,credit-off,moves-on,ftagg-off)}` (design §4-L3c step 4 — the
  ftagg-ON entries are the P0a-cuts-legs-8/9 mitigation, design §3.3 item 2).
- Deep legs: `iters = 2000000`, per-leg `time_budget_sec = max(0,(budget-elapsed)/N_deep -
  kPhase2LegMargin*M0)`; runPlacementSearch's existing `(iter & 63)` wall check ends the loop.
- Deep legs run with `overflow_gated_pads = true` forced on their leg-local Problem copy (the L3a
  gate is part of the deep-mode governor; the flag-off global default is untouched).
- Phase 2 skipped in deep mode (extend the phase1_complete gate).
- Anytime write must fire after leg 0 and after every improving deep leg (PortfolioFold::admit
  unchanged).

## Gates

Warning-clean make; budget-off byte-identity 8/8 vs pre-L3bc HEAD (batch protocol — every L3b/L3c
branch begins with `options.time_budget_sec > 0.0 &&`); b2 + f1 + case00 + p1 suites; new b2-style
tests: (i) budget-off → sa_wall_time_sec stamped but no deep mode, full registry; (ii) tiny budget
on a small case → deep mode may or may not trigger, structurally valid result, phase1_legs/legs_run
sane; (iii) case5-shaped trigger unit test if feasible without a long run. Measurement M3 (design §5):
case5 600s arms {base, +L3a, +L1, +L1+L2, +L1+L2+L3}, same-load batch, golden rescore; case5 wall
≤ 700s hard ceiling check.

## Out of scope

Adoption/auto-rules (8/19 scoping), L4b, defaultVariants() reorder (deepVariants is a separate list).
