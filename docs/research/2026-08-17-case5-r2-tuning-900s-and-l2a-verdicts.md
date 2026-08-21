# case5 tuning round 2 + 900 s budget probe + L2a A/B (2026-08-17)

Basis: ledger NEXT (shelf-grid shape at fixed hedge, deep-leg budget rebalance) + user-directed
additions (bare-budget 600→900 verification vs the official 900 s cap and 2 h aggregate; L2a
`--ft-area-rung` 600 s A/B supplement). Workspace `~/efgr-0817-battery/` (durable, done-markers,
per-run timeouts, rc+validation checked — memory `detached-runs-need-durable-dirs` +
`anytime-write-partial-cfg-gotcha` discipline). Control = HEAD `2f76d33` (== L4c `59c0719` code).
Protocol: same-load per-seed rounds (all 4 arms of a seed concurrent), metric = stderr `score=`,
rescorer spot-checked (all exact), `validation=ok`, `rc=0` throughout.

## 1. R1 — case5 @600 s, 4 arms × seeds {1,2,3}

| arm | knob | s1 | s2 | s3 | Σ |
|---|---|---|---|---|---|
| control | — | 60,209,118 | 70,533,472 | **54,917,186** | **185.66M** |
| widegrid | shelf grid nrows 2–8 × {0.70,0.85,1.08,1.30,1.50}, hedge kept | 58,668,155 | 68,783,818 | 77,423,269 | 204.88M |
| slicebias | deep-leg slice weights {1.25,1.25,0.5} toward seeded legs | 60,927,471 | 64,981,071 | 72,240,760 | 198.15M |
| l2a | control + `--ft-area-rung` | 60,209,118 | 70,533,472 | 54,917,186 | 185.66M |

- **control s3 = 54,917,185.718 is the new all-time case5 best** (previous 55,866,305), rescorer-
  exact, 208 overflow nets (78K penalty). Champion-gap trajectory: 91.1M submitted (+110%) →
  55.87M (+29%) → **54.92M (+26.8%)**.
- **widegrid REJECT**: 2/3 small wins (−1.5M/−1.8M) then a +22.5M s3 blowup — the wider grid
  changes which shelves win the cheap-score pick and at s3 seeds a bad basin. Same failure family
  as R1's wideshelf/combo (2026-08-15 doc §2): the current 12-cell grid is load-bearing.
- **slicebias REJECT**: 1/3, Σ +12.5M. Slice tilt toward seeded legs starves the hedge without
  reliably deepening the winners. Note slicebias s3 landed on 72,240,760.3 — EXACTLY control@900
  s3's score (12 digits): the plateaued-leg identical-endpoint phenomenon again (2026-08-15 doc
  §2); both cells' winning leg plateaued before either cut point.
- **L2a INERT on case5**: cfg byte-equal to control on all 3 seeds (md5-verified). The
  ft_area_sum rung never changes the adopted outline on this path (deep-mode winners come out of
  shelf-seeded legs whose sweep the rung never wins). Verdict: keep the flag dormant; no adoption,
  no removal (harmless, and other-shaped hidden cases could still trigger it).

## 2. R2 — 900 s budget probe (A3)

case5, control binary:

| budget | s1 | s2 | s3 | Σ |
|---|---|---|---|---|
| 600 (R1 control) | 60,209,118 | 70,533,472 | 54,917,186 | 185.66M |
| 900 | 60,927,471 | 67,781,697 | 72,240,760 | 200.95M |
| 750 (s1 only) | 60,209,118 | | | |

- **900 s does NOT beat 600 s on case5**: Σ +15.3M worse across 3 seeds; every delta inside the
  ±8M noise band. The 750 s run reproduced the 600 s s1 score EXACTLY — deep legs plateau before
  either cut point, so extra budget past ~600 s buys nothing on the current search.
- Non-case5 @900 s vs the 08-15 600 s battery: case2 36.63M→36.36M (−0.27M), case3
  23.08M→22.12M (−0.96M, −4.1%), cases 0/1/4/6/7 byte-identical scores. case4 ran 47 legs /
  148K exact evals at 900 s and improved 0.
- **Rank arithmetic kills the gains**: δ·rt·√A for +~300 s ≈ +790K on case2 (vs −270K checker)
  and ≈ +615K on case3 (vs −960K checker) — only case3 nets positive, ~+0.35M rank, while every
  unchanged case eats a pure runtime penalty.
- **Aggregate wall**: per-case walls at 900 budget measured 868–899 s ⇒ Σ(8 cases) ≈ 7.0K s
  against the official 2 h = 7,200 s hard limit (E_Contestant Compilation Checklist 20260811) —
  ~2% margin on OUR machine; the official RHEL 8.8 box is slower. (Batch-2's four logged walls
  ~26,015 s are a WSL clock-jump artifact — constant offset on 4 concurrent runs; solver budget
  uses steady_clock, runs and scores unaffected, batch-3 walls 868–879 s are representative.)
- **A3 verdict: bare-budget stays 600 s.** 900 is Pareto-bad (case5 noise-worse, rank-negative on
  7/8, zero aggregate margin). A case3-only refinement (adaptive per-case budget) remains the
  known F1 §5 overfit risk — untouched.

## 3. Official 0811 alignment check (user-directed)

Extraction from `docs/official/beta_test/E_reference(20260811).html` + the 20260811 Compilation
Checklist (full agent report in session; key confirmations):

- **α values in the 20260811 CSVs already match the official per-case table** (case5 0.05, case6
  0.2, case7 0.15) — solver parses α from CSV ⇒ no misalignment. (Official may override via
  `--alpha-override`; CSV values = official defaults.)
- **900 s per-case cap + 2 h aggregate confirmed** (beta case5 PASS max 810.1 s). Timeout ⇒ 0 for
  the case + incompletion worst_cost×1.8 (case5 worst_cost 449.9M — by far the largest exposure;
  reinforces the never-timeout + anytime-output posture).
- V5 formula + all constants unchanged 0727→0811; new in 0811: beta stats section, case5/6/7 α
  rows, beta-measured runtime table with worst_cost column.
- **NEW intel: official recommended compile line includes `-pthread`; no single-thread rule and
  no core-count stated.** Parallel portfolio legs are a legal-looking lever (wall-clock ÷ N with
  runtime penalty measured on wall would be a direct rank win) — HIGH-RISK/HIGH-REWARD candidate,
  needs its own design + determinism story; queued as a research question, not acted on.
- Output contract (flat dir, `.csv`→`.cfg` replacement, exe-dir output) — evidenced compatible by
  the passing beta submission.

## 4. B2-incremental (#5 v3 shortlist) — trigger evaluation

The lever's own precondition is "exact tier becomes the bottleneck again". Measured now:
case5@600 s sa_wall share 65–72% (SA dominates; L3b governor + deep mode fixed the old 11/89
inversion); case3/case4@900 machinery share ~54–56% BUT +300 s of pure machinery budget bought 0
improvement on case4 — throughput is not the binding constraint, basin quality is. **Verdict:
trigger NOT met; defer** (re-evaluate only if a future lever makes exact evals the measured
limiter again).

## 5. State + next

- case5 record 54,917,186 (control@600 s seed 3, measurement draw; shipping bare seed remains 1).
- Deep-mode tuning is at diminishing returns: 5 consecutive one-knob probes (deep5, wideshelf,
  combo, widegrid, slicebias) all rejected; the L4c configuration is locally optimal within the
  ±8M noise band. Case5 attack continues via NEW mechanisms (C1a WL redistribution, notch pack)
  rather than more knob turns.
- v3-survey execution in flight this session: D2 `--wl-ladder` LANDED (4382f4f), C1a
  `--wl-redistribute` implementing (.superpowers/sdd/c1-design-20260817.md), D3/B1/notch briefs
  ready (.superpowers/sdd/levers-batch2-briefs-20260817.md), #5 verdict above; C3/#8 + near-miss
  group queued.
