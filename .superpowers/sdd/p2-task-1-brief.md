### Task 1: pure cost-unit edge functions (design §Q1)

**Files:** `src/routing.cpp` (file-local helpers) or new `src/route_cost.hpp` (header-only, mirrors ft.hpp pattern — implementer's choice, note it). Tests in p2 file.
- [ ] RED: `channelEdgeCost(channel, dir, usage, n, α, dist)` — slack channel ⇒ α·n·dist only; saturated ⇒ + 1e6 × marginal area (per T0 denomination); `softFtEdgeCost(block, acc, n, placed_area, α, dist)` — below-slack free; tier-jump priced (fixture at acc=2900, n=200 crossing the 3000 boundary); direct-abut ⇒ 0.
- [ ] Implement pure functions. GREEN + suites + commit `feat: cost-unit routing edge functions`.

