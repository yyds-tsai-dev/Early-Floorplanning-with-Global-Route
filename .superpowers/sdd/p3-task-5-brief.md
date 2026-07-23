### Task 5: elite-pass hook — design T5 (first Cost-moving task)

**Files:** Modify `src/search.cpp` (elite loop: after outlineSweep, run respaceFixedPoint on the swept winner; adopt via `outlineSweepBetter`); add `#include "respace.hpp"`; extend legacy runner link lines with src/respace.cpp where search.cpp is linked.
- [ ] Failing test: case1 bstar seed1 iters 500 end-to-end ⇒ final `channel_overflow` strictly below the pre-hook value (measure baseline in-test by scoring the same placement without respace), validation ok, open 0. Determinism byte-identical.
- [ ] Implement. GREEN + suites + commit `feat: respace elites to demand`.

