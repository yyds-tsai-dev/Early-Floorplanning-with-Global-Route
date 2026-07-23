# P2 Task 2 Report — cost-driven Dijkstra pass

**Status:** DONE.

**Commit:** `ffceae6` "feat: cost-driven routing pass" (branch `fable5`), 3 files changed
(`src/routing.cpp`, `src/routing.hpp`, `test/regression_p2_20260707.cpp`), CLAUDE.md left uncommitted
per instructions.

## Change summary

- `routeProblem(problem, placement, bool cost_driven = false)` — new defaulted param
  (`src/routing.hpp:12`); all existing callers (`eval.cpp:139`, `search.cpp`, `respace.cpp`) compile
  unchanged and get byte-identical legacy behavior.
- `routeChunk` (`src/routing.cpp`, inside `routeProblem`) branches on `cost_driven`:
  - `false`: unchanged legacy two-pass (pass 0 channels-only, pass 1 adds soft-block FT), unchanged
    heuristic weights.
  - `true`: single pass, `allow_soft` always true. Edge cost = `wireCost(alpha, nets, center-manhattan)`
    plus, for a soft-block intermediate, `softFtMarginalCost(block, acc, nets, placed_area)` (acc =
    `result.soft_feedthrough_nets[v]`, the call-local running total so far this `routeProblem` call);
    plus, for a channel hop, `channelMarginalCost(ch, horizontal, usage, nets)`.
  - Usage/overflow/FT-mismatch accrual after path commit is untouched (exact T0 code, same
    guiding-point WL computation).

## Tests (`test/regression_p2_20260707.cpp`, `bash test/run_p2_regressions.sh`)

- `testCostDrivenPrefersFtOverSaturatedChannel`: fixture with A/B connected via a saturated vertical
  channel (vcap=2000, 3600 nets) AND an abutting soft block S (`ft_base_area=372949`, official
  worked-example base). Hand-verified via `ft.hpp` that FT marginal (36,261.34 um^2 @ 40% tier) <
  channel marginal (39,040 um^2) before asserting router behavior.
  - `cost_driven=false` (legacy): routes through channel — `channel_overflow = 39040` (matches T0
    fixture exactly), `soft_feedthrough_nets[S] == 0`.
  - `cost_driven=true`: `soft_feedthrough_nets[S] > 0`, `channel_overflow = 0` (strictly lower than
    legacy's 39040 — router fully diverted to FT since S's tier covers the full 3600 nets more
    cheaply than any channel use).
- `testCostDrivenZeroWlDirectAbutUnchanged`: two blocks side-by-side, no gap, `cost_driven=true` →
  2-node path, `wirelength == 0`, `channel_overflow == 0`, `feedthrough_mismatch == 0` (GC5 preserved).
- `testCostDrivenDeterministic`: same fixture routed twice with `cost_driven=true` → identical
  `total_wirelength`, `channel_overflow`, `feedthrough_mismatch`, and per-route `node_path`/`wirelength`
  (GC6 purity — no static/global state).

All 13 P2 assertions pass (`P2 T0+T1+T2 regressions passed`). Full suite run on the committed HEAD:
`run_case00.sh`, `run_placement_engine_regressions.sh`, `run_20260618_regressions.sh`,
`run_20260608_regressions.sh`, `run_p0_regressions.sh`, `run_p1_regressions.sh`,
`run_p2_regressions.sh`, `run_p3_regressions.sh` — all green. `make clean && make` warning-clean
(`-Wall -Wextra -pedantic`).

## Edge-geometry approximation choice

Per-hop direction classification uses `edgeFromTo(rects[u], rects[v])` (existing helper, already used
for port-edge legality) rather than a post-hoc guide-point delta: `edgeFromTo` returns 1/3 when the
rects share a vertical edge (side-by-side touch, x differs) and 2/4 when they share a horizontal edge
(stacked touch, y differs). This is an **exact** match (not merely a monotone proxy) to the existing
`dx>kEps`/`dy>kEps` post-commit usage-accrual classification, because both read the same
`positiveTouch` rect-adjacency geometry and a single rect-to-rect touch can only be axis-aligned on one
axis (no single-hop diagonal/L case exists in this graph — an L-shaped route is expressed as separate
hops, each priced on its own axis). Documented inline at the channel-hop cost site in `routing.cpp`.

## Concerns

- Not yet default-on (per plan, that's T6); `cost_driven=false` remains the production path until the
  comparison gate flips.
- Chunking is still the legacy fixed `ceil(nets/1000)` split (T3's job) — cost-driven pricing currently
  sees a whole chunk's `nets` at one marginal price, so within-chunk bottleneck splitting isn't yet
  expressed; this is expected and deferred per the plan.

Report path: `.superpowers/sdd/p2-task-2-report.md`
