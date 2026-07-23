# Task 3 (AMENDED): Official accounting in the solver — C1 restore + per-edge demand + guiding-point WL

This amended brief SUPERSEDES `.superpowers/sdd/beta-task-3-brief.md` (the original assumed the solver's accounting was already faithful; Task 2 disproved that). Read the original for the stash-handling steps it retains (pop guard, risk fuse, Codex review) — this file defines the full scope.

## Goal

Make `exactScore` charge what the official checker charges, in three moves:
1. **Restore the C1 stash** (`git stash pop` of `stash@{0}` "WIP on fable5: 6d2de22"): official β mapping in eval.cpp (`kBetaLin·A·R + kBetaTail·A·max(0,R−θ)` + `kBetaFt·ft_mismatch`), constants + `RoutingResult::channel_overflow_nets`/`channel_capacity_total` in model.hpp, nets-capacity accumulation in routing.cpp, route_cost.hpp weight rework.
2. **Port the per-edge demand model into routing.cpp**: channel usage/overflow must be charged the official way — for each channel traversal, the entry edge AND the exit edge each charge the full net count to that edge's axis (edges 1/3 = left/right → `horizontal_usage`; 2/4 = top/bottom → `vertical_usage`; a straight pass-through charges its axis twice). `channel_overflow_nets = Σ_ch [max(0, H_ch − 25·h_ch) + max(0, V_ch − 25·w_ch)]`, `channel_capacity_total = Σ_ch [25·h_ch + 25·w_ch]`.
3. **Port the guiding-point-chain WL into routing.cpp**: route wirelength = manhattan chain of center-anchored guiding points — per hop, the midpoint of the two nodes' facing-edge midpoints, shared border chosen by min-gap (the reference implementation is `guidingPoint` in `src/cfg_score.cpp:29-47`; keep cfg_score's copy untouched, but factor the shared function into `src/route_geometry.hpp` if that avoids duplication — reviewer will judge duplication vs. coupling).

`accumulatePathUsage` in `src/route_geometry.hpp` is now the WRONG model on both counts — replace its role in routing.cpp (per-edge usage from the node/step sequence + guiding-point WL). Either rewrite it to the official rules or retire it; do not leave dead code.

## What the stash contains (verified hunks)

- eval.cpp `routeAndScoreAt`: `double R = routing.channel_capacity_total > 0.0 ? routing.channel_overflow_nets / routing.channel_capacity_total : 0.0; score.penalty += kBetaLin * score.outline_area * R + kBetaTail * score.outline_area * std::max(0.0, R - kOverflowRateThreshold); score.penalty += kBetaFt * routing.feedthrough_mismatch;`
- eval.cpp cheapScore proxy: `penalty += overflow * kBetaLin;` (route-aware proxy rescale)
- model.hpp: `constexpr double kBetaLin = 20.0; kBetaTail = 1.5; kOverflowRateThreshold = 0.05; kBetaFt = 3.0;` + the two RoutingResult fields
- routing.cpp: capacity-total accumulation; route_cost.hpp: ~20 lines reworked
The stash was cut BEFORE Task 1's extraction — expect a conflict in routing.cpp; resolve keeping the current structure and layering the stash's intent on top.

## Non-negotiable gates (all must pass before commit)

1. **Pop guard**: `git stash list` must still show the stash; `git status --short -- src/` must be empty. On surprise: STOP, report BLOCKED.
2. **Build warning-clean** (`-Wall -Wextra -pedantic`).
3. **Self-consistency gate (NEW, the strongest)**: for each of the 5 cases, run the solver (`--seed 1 --iters 1000`), then `bin/efgr_rescore` on its output cfg. The solver's reported `wirelength` and `channel_overflow` (stderr summary) must match the rescorer's `wl` and `overflow` to ≤1e-6 relative, and the solver's score must equal `checker_cost + 1e15·open_routes + kBetaFt·ft_mismatch − (rescorer ft term)` — i.e., on a zero-open, zero-FT-mismatch run, `score == checker_cost` to ≤1e-6 relative. This proves the port: solver and golden judge agree on the same artifact. Automate it as `test/run_selfconsistency_regressions.sh` + a small driver (pattern: existing run_*.sh; the driver may be a bash script comparing the two outputs with awk — no C++ needed if bash suffices; keep it deterministic).
4. **All 10 suites green**: run_case00, run_placement_engine_regressions, run_20260618_regressions, run_20260608_regressions, run_p0/p1/p2/p3_regressions, run_testdata_regressions, run_cfg_score_regressions. Suites asserting absolute internal scores may legitimately need value updates — list every such edit in the report with old→new.
5. **Benchmark gate**: BEFORE any code change, run `bash scripts/benchmark.sh --iters 1000 | tee /tmp/bench_pre_t3.txt` at the current HEAD (5931567). AFTER: `... | tee /tmp/bench_post_t3.txt`. Requirement: TOTAL checker_cost (both engines) post ≤ pre, and open_routes == 0 on all rows (solver exits nonzero otherwise — the script aborts). Expect a LARGE improvement: pre-T3 the SA optimizes phantom overflow (~2000× overweighted) and a different WL metric.
6. **Codex adversarial review**: from the WSL native shell, `node "$CLAUDE_PLUGIN_ROOT/scripts/codex-companion.mjs" adversarial-review --background` scoped to the T3 diff (poll status/result). Address Critical/Important findings before finishing. If Codex auth/sandbox fails (known env issue), note it and proceed — do not block the task on tooling.
7. **Risk fuse** (controller-approved): if the benchmark gate regresses and you cannot resolve it same-day, re-stash the whole T3 change (`git stash`), write findings to the report, and report DONE_WITH_CONCERNS with the fuse pulled.

## Coupling map (reason through each; the suites are the net)

- `route_cost.hpp` marginal pricing must price in the official currency: the marginal cost of pushing n nets through a channel = Δ(β_lin·A_est·R) — the stash reworks this; reconcile with per-edge accounting (a hop now adds demand at BOTH its entry and exit edges of the same channel; entry-axis-only pricing is a known pre-existing gap — measure, don't silently widen it).
- `respace.cpp` (P3) reads `horizontal_usage`/`vertical_usage` — per-edge charging roughly doubles pass-through demand; P3 suite must stay green and respace behavior stays sane (its axis-crossing invariant reads the same fields).
- eval.cpp's one-step corrective grow reads `routing.channel_overflow` (um² today): the stash moves scoring to nets; keep the grow-margin formula dimensionally sensible (overflow_nets/25 = missing um of channel dimension — same as before with nets input).
- `routing.cpp` negotiation/history (P2 T4) bumps on per-direction overcapacity — fields keep semantics, magnitudes shift; kHistoryBump is a tuning knob, do NOT retune in this task.
- cheapScore's proxies (routeAwareProxyPenalty, channelMarginProxy) are gradients, not the scored quantity — the stash rescales one term; leave further proxy calibration alone (T5's sweep will judge).

## Deliverables

- The port + reconciliation, committed as ONE commit: `feat(eval): official checker accounting in solver — C1 + per-edge demand + guiding-point WL`, body ends `Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>` (message via /tmp/msg.txt, `git commit -F`).
- `test/run_selfconsistency_regressions.sh` (+ driver) included in the same commit.
- Stage only: `src/eval.cpp src/model.hpp src/route_cost.hpp src/routing.cpp src/route_geometry.hpp test/run_selfconsistency_regressions.sh` (+ driver file, + any suite .cpp whose asserted values you legitimately updated). NEVER stage CLAUDE.md, CONTEXT.md, docs/, .superpowers/.
- Report: per-case self-consistency table (solver vs rescorer), pre/post benchmark totals, every suite-value edit old→new, Codex findings + resolutions, coupling-map notes.
