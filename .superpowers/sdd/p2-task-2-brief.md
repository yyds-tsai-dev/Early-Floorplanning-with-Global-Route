### Task 2: cost-driven Dijkstra pass (design §Q1-Q2 core)

**Files:** `src/routing.cpp` `routeChunk` — replace heuristic weights (center-manhattan + nets·0.05 + 10000·usage/cap) with T1 functions; single unified pass (soft blocks always allowed, priced); keep guiding-point WL accounting unchanged.
- [ ] RED: fixture where a saturated channel and an abutting soft block both connect source→target ⇒ router picks FT once channel saturates (assert soft_feedthrough_nets>0 and overflow lower than channel-forced route); zero-WL abut fixture still routes direct.
- [ ] Implement behind comparison gate (route both old+new per bundle set? too heavy — gate at routeProblem level: env/flag param defaulting OFF, exactScore keeps old path until T6; the p2 tests call the new path explicitly). GREEN + suites + commit `feat: cost-driven routing pass`.

