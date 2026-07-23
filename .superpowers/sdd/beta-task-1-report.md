# Beta Task 1 report: Extract shared route-accounting helpers (byte-identical refactor)

## What I implemented

Verified the real code at `src/routing.cpp` matched the brief's description exactly before touching
anything (`edgeFromTo` at :24, `guidePoint` at :31, `guides.push_back(guidePoint(...))` at :316,
`dx/dy` usage stamps at :331-333, steps emission at :342-343) — no discrepancy found, proceeded per
plan.

1. **Created `src/route_geometry.hpp`** (new file, 75 lines): `#pragma once` header, `namespace efgr`,
   includes `<algorithm> <cmath> <cstddef> <utility> <vector>` plus `"model.hpp"` (consumes `Rect`,
   `Channel`, `kEps`, `manhattan` as specified). Contains three `inline` functions matching the brief's
   exact signatures:
   - `int edgeFromTo(const Rect& a, const Rect& b)` — moved verbatim from routing.cpp:24-29.
   - `std::pair<double, double> guidePoint(const Rect& a, const Rect& b)` — moved verbatim from
     routing.cpp:31-42.
   - `double accumulatePathUsage(const std::vector<Rect>& node_rects, const std::vector<Channel*>& node_channels, double nets)`
     — new function implementing exactly the WL + per-channel usage accumulation formerly inlined in
     `commitRoutePath` (guides = midpoints of consecutive shared borders; per-segment `dx>kEps` charges
     `horizontal_usage`, `dy>kEps` charges `vertical_usage`; `ch->usage = std::max(h, v)` refreshed on
     every charge; returns `nets * Sum manhattan(consecutive guides)`). Same iteration order, same
     `kEps` comparisons, same `ch.usage` update as the original — verbatim transplant, restructured only
     to take `node_rects`/`node_channels` as parameters instead of closing over `rects`/`rr.channels`.
     Soft-feedthrough-net accrual and RouteStep emission are explicitly NOT part of this helper (per the
     brief) and stayed in routing.cpp.

2. **Modified `src/routing.cpp`**:
   - `src/routing.cpp:12` — added `#include "route_geometry.hpp"`.
   - Removed the `edgeFromTo`/`guidePoint` definitions from the anonymous namespace (were routing.cpp
     original :24-42); all existing call sites (`portAllows`-adjacent checks, the Dijkstra edge-cost
     lambda, `commitRoutePath`'s steps loop, `pathSlack`) resolve unchanged via unqualified lookup into
     the enclosing `efgr` namespace, since the header's functions are also declared at `namespace efgr`
     scope.
   - `commitRoutePath` (was routing.cpp:306-347, now :287-325): replaced the guides-build loop +
     WL-accumulation loop + the channel-usage half of the third loop with: build `node_rects`
     (`vector<Rect>`) and `node_channels` (`vector<Channel*>`, non-null iff `node >= n_blocks`, matching
     the original `node >= n_blocks` test exactly) from `route.node_path`, then
     `route.wirelength = accumulatePathUsage(node_rects, node_channels, nets);`. The soft-feedthrough-net
     accrual (`node < n_blocks && isSoft(...)`) was split into its own standalone loop over the same
     index range (safe: the two conditions are mutually exclusive on `node`, so splitting one
     interleaved loop into two independent loops over disjoint state produces identical final state).
     The steps-emission loop and `rr.total_wirelength += route.wirelength;` are untouched.

## Byte-identity verification (Step 3)

```
/tmp/post_case0.cfg: OK
/tmp/post_case1.cfg: OK
/tmp/post_case2.cfg: OK
/tmp/post_case3.cfg: OK
/tmp/post_case4.cfg: OK
```

Baseline (pre-refactor, captured before any edits) vs. post-refactor md5s matched on all five
`test/2026 ICCAD_case.case{0..4}.csv` runs (`--seed 1 --iters 200`). The transplant is behavior-preserving.

## Suite results (Step 4 — 9 suites, per orchestrator's environment facts superset of the brief's 8)

All PASS:
- `run_case00.sh` — PASS (`validation=ok`, `open_routes=0` for both engine runs)
- `run_placement_engine_regressions.sh` — PASS (`placement_engine_regression: ok`)
- `run_20260618_regressions.sh` — PASS (`regression_20260618: ok`)
- `run_20260608_regressions.sh` — PASS (`regression_20260608: ok`, `validation=ok`)
- `run_p0_regressions.sh` — PASS (8/8 tests ok, `P0 regressions passed`)
- `run_p1_regressions.sh` — PASS (22/22 tests ok, `P1 regressions passed`)
- `run_p2_regressions.sh` — PASS (24/24 tests ok, `P2 T0+T1+T2+T3+T4+T5+T5.1+T6 regressions passed`)
- `run_p3_regressions.sh` — PASS (18/18 tests ok, `P3 T1+T2+T3+T4+T5+T6 regressions passed`)
- `run_testdata_regressions.sh` — PASS (`testdata 20260712 regressions passed`)

Full verbatim output captured in this session's transcript (each suite rebuilds from clean first via
its own `make clean && make`, so build warnings would have surfaced 9x over — none did).

Build: `make clean && make` with the full `-Wall -Wextra -pedantic` flag set produced zero warnings
across all 9 object files, both before running suites and standalone.

## Files changed

- `src/route_geometry.hpp` (new, 75 lines)
- `src/routing.cpp` (-35/+? net: 2 files changed, 89 insertions(+), 35 deletions(-) per `git show --stat`)

## Self-review findings

- Grepped `src/` and `test/` for pre-existing `edgeFromTo`/`guidePoint`/`accumulatePathUsage` symbol
  collisions before promoting the two functions out of the anonymous namespace (internal linkage) to
  the header (external linkage via `inline`) — none found; only routing.cpp defined/used them, and two
  test files (`regression_p3_20260707.cpp`, `regression_p2_20260707.cpp`) reference `edgeFromTo` only in
  comments, not code.
- Confirmed the `int nets` (routing.cpp) → `double nets` (header signature) implicit conversion at the
  call site is exact for these magnitudes (net counts are well within double's 53-bit exact-integer
  range), so `manhattan(...) * nets` and `ch->horizontal_usage += nets` produce bit-identical results
  to the pre-refactor `int`-typed arithmetic — the md5 gate confirms this empirically as well.
  Reviewed the `i - 1 < guides.size() && i < guides.size()` guard in the original loop: given the outer
  loop bound (`i` ranges `[1, node_path.size()-2]`) and `guides.size() == node_path.size()-1`, the guard
  is always satisfied in practice; carried it over unchanged into the helper rather than dropping it, to
  stay a strict verbatim transplant rather than a "provably equivalent" rewrite.
- Verified splitting the original single loop (soft_feedthrough_nets + channel usage interleaved) into
  two independent loops is safe: the two branches operate on disjoint state (`node < n_blocks` vs.
  `node >= n_blocks` are mutually exclusive) with no shared mutable data between them, so loop order
  doesn't affect the final result — confirmed by the md5 gate.
- Verified the commit contains exactly the two intended files (`git status --porcelain` before/after
  staging showed only `src/route_geometry.hpp` (new) and `src/routing.cpp` (modified) staged; all other
  dirty tree state — `CLAUDE.md`, `CONTEXT.md`, `docs/`, `.superpowers/`, `tools/`, `test/testdata_regression_bin`
  — remained untouched/unstaged).
- Re-read the full `git show` diff for both files post-commit; matches the intended transplant with no
  stray changes.

## Concerns

None. Byte-identity gate passed 5/5, all 9 regression suites green, build warning-clean, commit scoped
to exactly the two named files.
