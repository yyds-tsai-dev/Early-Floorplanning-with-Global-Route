# P3 Task 1 Report — channelDemands (demand model)

**Status:** Done. GREEN across all suites.

**Commit:** `227b8ee` — `feat: add channel demand model for respacing`

## Files
- `src/respace.hpp` — `ChannelBounds`, `StripDemand`, `channelBounds()`, `channelDemands()`.
- `src/respace.cpp` — implementation (pure, no mutation).
- `test/regression_p3_20260707.cpp` — 4 fixtures (a-d).
- `test/run_p3_regressions.sh` — mirrors `run_p1_regressions.sh`'s g++ src list + `src/respace.cpp`.
- `.gitignore` — added `test/p3_regression_bin`.

## Axis-crossing invariant (empirically verified before pinning)
Confirmed via `routing.cpp:190-191` (`dx>kEps → horizontal_usage`, `dy>kEps → vertical_usage`)
and a throwaway debug harness before writing fixtures:
- Fixture (a): A below / B above, L/R flanking in x → bundle A→B routes **vertically** through
  the channel → pure `vertical_usage` → mapped to **`need_w`** on **(left,right)=(L,R)**.
- Fixture (b): L/R side-by-side, T/Bo flanking in y (transposed) → bundle L→R routes
  **horizontally** → pure `horizontal_usage` → mapped to **`need_h`** on **(below,above)=(Bo,T)**.
- Fixture (c): same geometry as (a) with L removed → channel's left edge at outline wall →
  `channelBounds().left == -1`.
- Fixture (d): two blocks, no bundles → all channels zero-usage → `channelDemands()` returns empty.

## Tests
`bash test/run_p3_regressions.sh` — all 4 fixtures pass (`testVerticalUsageChargesNeedWOnLeftRight`,
`testHorizontalUsageChargesNeedHOnBelowAbove`, `testWallBoundedChannelSideIsMinusOne`,
`testZeroUsageChannelsProduceNoDemand`). Also green: `run_p1_regressions.sh`, `run_p0_regressions.sh`,
`run_case00.sh`, `run_placement_engine_regressions.sh`, `run_20260618_regressions.sh`,
`run_20260608_regressions.sh`. `make` warning-clean (`-Wall -Wextra -pedantic`).

## Concerns
None blocking. Note for Task 2: `channelDemands` currently emits one `StripDemand` per channel
(not yet merged by pair) — Task 2's constraint-graph build must take the max `need_w`/`need_h`
across multiple `StripDemand` entries sharing the same (left,right) or (below,above) pair, per
design §1.2 "Multiple channels per strip."
