### Task 3: respaceToDemand single pass (integrate + legality gate) — design T3

**Files:** Modify `src/respace.hpp/.cpp`; tests in p3 file.
**Interfaces →:** `bool respaceToDemand(const Problem&, Placement&, const RoutingResult&);` — demands → relax → reset stamp → `shrinkWrapOutline` (re-anchor edges + fresh stamp) → full legality (placementLegalAt-equivalent via the anchor result + containment). On any failure: restore input placement, return false.
- [ ] Failing tests: case1 bstar seed1 decode+route (known overflow) ⇒ respaceToDemand true, placement legal (validateSolution ok vs its stamp), stamped area grew, core order preserved; failure-path test (impossible demand fixture) ⇒ placement byte-identical to input.
- [ ] Implement. GREEN + suites + commit `feat: demand-driven respace single pass`.

