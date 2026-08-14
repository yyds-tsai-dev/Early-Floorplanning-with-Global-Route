# case5 batch 1 — L4a + L2a + L2b + L3a

**Basis**: `.superpowers/sdd/case5-design.md` §4 (L4a, L2a, L2b, L3a) + §5 schedule row "8/15 AM",
pulled forward to 8/14. Read the design doc sections first; this brief only adds the implementation
decisions the design left open. Each lever = its own commit (Conventional Commits, feat/fix scope,
`Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>` trailer), committed only after its own gates
pass. Order: L4a → L2a+L3a (shared outlineSweep plumbing, two commits, L2a first) → L2b.

## L4a — `rememberBounded` early-out (no flag; provable no-op)

Design §4-L4a. At the top of `rememberBounded` (`search.cpp:206-216`) add the strictly-worse
early-out. BEFORE writing it, read the function and confirm the pool's sort order (which end is
worst) and the exact comparator — the early-out must use a STRICT inequality against the worst kept
element so tie behaviour under the non-stable `std::sort` is untouched, and must fire only when
`pool.size() >= cap`. Doc comment cites case5-design.md §4-L4a (per-iteration ostringstream/copy/sort
on a full pool, ~15-25% of SA iteration cost).
**Gate**: this is a NO-OP by proof — byte-identity is the whole acceptance (see the batch identity
protocol below). No flag.

## L2a — `ft_area_sum` outline rung (flag: `Problem.ft_area_rung`, hidden `--ft-area-rung`)

Design §4-L2a. In `outlineSweep` (`search.cpp:290-439`): capture the tight rung's `RoutingResult`
(the `margin == 0.0` evaluation at ~`search.cpp:398`) — small change to `tryTarget` or its lambda to
hand the routing back. After the margin ladder, before the pads, behind `if (problem.ft_area_rung)`:
if `tight_routing.ft_area_sum > tight_w * tight_h`, add ONE rung at
`(min(MAX_w, ceil(tight_w*s)), min(MAX_h, ceil(tight_h*s)))` with `s = sqrt(ft_area_sum/(tight_w*tight_h))`,
adjudicated by the existing `outlineSweepBetter` gate (Pareto-safe). New bool on `Problem`
(model.hpp, default false, doc comment citing case5-design.md §3.1/§4-L2a) + hidden flag in main.cpp
next to `--undirected-bundles` (same not-in-banner precedent). The tight-routing capture must be
structured so L3a below reuses it (implement the capture once, in the L2a commit).
**Gate**: flag-off byte-identity (batch protocol); flag-on smoke: case0 `--ft-area-rung --iters 50
--seed 1` → rc==0, `validation=ok`.

## L3a — overflow-gated pad ladder (flag: `Problem.overflow_gated_pads`, hidden `--overflow-gated-pads`)

Design §4-L3a. In `outlineSweep`, run the 21-rung pad loop (`search.cpp:409-417`) only when
`!(tight_routing.open_routes == 0 && tight_routing.channel_overflow_nets == 0)` — gated behind
`if (problem.overflow_gated_pads)` so flag-off keeps the pads unconditionally (today's behaviour).
Uses the tight-routing capture from L2a. Doc comment: pads are a pure capacity-denominator play
(officialOverflowPenalty's R); when the tight rung is already overflow-free they can only raise
outline_area — case5-design.md §4-L3a. Field name/flag EXACTLY as above (L3c will later force it in
deep mode).
**Gate**: flag-off byte-identity (batch protocol); flag-on smoke: case0 as above, plus record wall
time of case5 `--overflow-gated-pads --iters 50 --seed 1` vs the flag-off case5 run from the identity
protocol (first M1 data point — report both walls; do NOT block the commit on a wall target).

## L2b — `--ft-aggressive` CLI flag

Design §4-L2b part 1 only (NO deepVariants reorder — that is L3c's). `ft_pricing_aggressive` already
exists on `Problem` (model.hpp:149 area) and is OVERWRITTEN per leg by `runLeg`
(`search.cpp:~1104`: `p.ft_pricing_aggressive = variant.ft_pricing_aggressive;`). So: main.cpp sets
`problem.ft_pricing_aggressive = hasFlag(argc, argv, "--ft-aggressive");` (hidden flag, same
precedent block), and `runLeg` becomes an OR:
`p.ft_pricing_aggressive = variant.ft_pricing_aggressive || problem.ft_pricing_aggressive;` with a
doc comment (the CLI flag forces the aggressive FT marginal on ALL legs for A/B isolation;
case5-design.md §4-L2b — registry legs 8-9 are exactly the ones P0a's phase-1 gate cuts first on
case5). Flag-off: the OR's right operand is the default-false Problem field → identical values.
**Gate**: flag-off byte-identity (batch protocol); flag-on smoke: case0 `--ft-aggressive --iters 50`
rc==0 `validation=ok`.

## Batch byte-identity protocol (run ONCE, after all four commits)

Baseline = the pre-batch HEAD commit (record its hash before starting). Build baseline binary in a
scratchpad worktree (`git worktree add <scratchpad>/batch1-base <hash>`). With NO new flags, run both
binaries and md5-compare output cfgs on ALL 8 cases:
- cases 0-4: `test/2026 ICCAD_case.caseN.csv`, `--iters 200 --seed 1`. Each case its own Bash call;
  case4 needs a ≥400000ms timeout (previous SIGTERM lesson).
- cases 5-7: `test/E_testcase_20260811/testcase_upload_20260712/2026 ICCAD_case.caseN.csv`;
  case6/7 at `--iters 200 --seed 1`; **case5 at `--iters 50 --seed 1`** (its wall is dominated by
  iters-independent elite machinery, ~10 min per run — budget a ≥900000ms timeout per run, record
  the wall times of both runs).
All 8 must match. Any mismatch: bisect by rerunning the identity case against each of the four
commits, report which lever broke it, and STOP (revert is the controller's call — do not debug under
time pressure, design §6.3).

## Suites (after the batch, once)

`make` warning-clean (each commit must also build clean before committing);
`bash test/run_b2_regressions.sh`, `run_f1_regressions.sh`, `run_case00.sh`,
`run_p1_regressions.sh` (outlineSweep touched), `run_undirected_regressions.sh`. All green.

## Out of scope

L1 (cheap_proxy_v2), L3b/L3c, L4b, any defaultVariants/deepVariants change, any adoption/auto-rule
wiring — measurement decides adoption later (design §6.2).
