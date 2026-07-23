### Task 1: channelBounds + channelDemand (pure demand model) — design T1, decisions 1-3

**Files:** Create `src/respace.hpp`, `src/respace.cpp`, `test/regression_p3_20260707.cpp`, `test/run_p3_regressions.sh`.
**Interfaces →:** `struct StripDemand { int left = -1, right = -1, below = -1, above = -1; double need_w = 0.0, need_h = 0.0; };` and `std::vector<StripDemand> channelDemands(const Problem&, const Placement&, const RoutingResult&);` — for each channel with usage > 0, geometrically derive bounding core-block indices (or wall = -1) and directional needs `need_w = vertical_usage/25`, `need_h = horizontal_usage/25`.
- [ ] Failing tests FIRST pin the axis-crossing invariant: hand-built 2-block problem with a routed vertical-usage channel between them ⇒ demand appears as `need_w` on the (left,right) pair; horizontal-usage fixture ⇒ `need_h` on (below,above). RED = missing module.
- [ ] Implement per design decisions 1-3. GREEN + all suites + commit `feat: add channel demand model for respacing`.

