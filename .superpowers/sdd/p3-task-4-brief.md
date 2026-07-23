### Task 4: route↔respace fixed point (≤3 iters, monotone) — design decision 5

**Files:** Modify `src/respace.hpp/.cpp`.
**Interfaces →:** `struct RespaceResult { Placement placement; RoutingResult routing; Score score; bool improved = false; };` `RespaceResult respaceFixedPoint(const Problem&, const Placement&, const ExactCachedFn&);` — iterate: route (via exactCached), demands, respace, re-route; keep best-legal by `outlineSweepBetter`; stop when overflow == 0, no demand growth, or 3 iters. Monotone-widen within the loop.
- [ ] Failing test: case1 fixture ⇒ fixed point reduces `channel_overflow` vs the un-respaced route (strict), `open_routes == 0`, deterministic across two runs.
- [ ] Implement. GREEN + suites + commit `feat: route-respace fixed point loop`.

