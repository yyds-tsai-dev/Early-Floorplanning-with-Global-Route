### Task 4: One shared outline computation for eval and writeCfg

**Files:**
- Modify: `src/eval.hpp` (export), `src/eval.cpp:6-16` (extend), `src/io.cpp:120-135` (delete local copy, call the shared one)
- Test: `test/regression_p0_20260706.cpp`

**Interfaces:**
- Produces: `std::pair<double, double> usedOutline(const Problem&, const Placement&);` and `std::pair<double, double> usedOutline(const Problem&, const Placement&, const RoutingResult&);` declared in `src/eval.hpp`, defined in `src/eval.cpp` (the 3-arg version also maxes over `routing.channels` rects, then clamps to `problem.outline_w/h` with a `max(1.0, ...)` floor — exactly the semantics currently in `src/io.cpp:120-135`).

- [ ] **Step 1: Write the failing test** (asserts scoring outline == emitted outline):

```cpp
void testOutlineConsistencyEvalVsCfg() {
    Problem p = forcedFtProblem();
    Placement pl = rowPlacement();
    RoutingResult routing;
    Score exact = exactScore(p, pl, &routing);
    auto [w, h] = usedOutline(p, pl, routing);
    assert(near(exact.outline_area, w * h, 1e-6));

    const std::string tmp = "test/p0_outline_tmp.cfg";
    writeCfg(tmp, p, pl, routing);
    std::ifstream in(tmp);
    assert(in.good());
    std::string tok;
    double cfg_w = -1.0, cfg_h = -1.0;
    while (in >> tok) {
        if (tok == "Outline" || tok == "OUTLINE") {
            in >> cfg_w >> cfg_h;
            break;
        }
    }
    std::remove(tmp.c_str());
    assert(near(cfg_w * cfg_h, exact.outline_area, 1e-6));
    std::cout << "testOutlineConsistencyEvalVsCfg ok\n";
}
```

Add `#include <fstream>`, `#include <cstdio>`, `#include "../src/eval.hpp"`, `#include "../src/io.hpp"`. Before writing the parse loop, read `writeCfg` in `src/io.cpp` and match the actual OUTLINE token spelling; adjust the comparison if the writer emits a different keyword.

- [ ] **Step 2: Run to verify it fails.** Expected: compile error — 3-arg `usedOutline` is not exported yet.

- [ ] **Step 3: Implement.** In `src/eval.cpp`, un-static the existing `usedOutline` (move out of the anonymous namespace) and add the 3-arg overload (move the body from `src/io.cpp:120-135` verbatim). Declare both in `src/eval.hpp`. In `src/io.cpp`, delete the local `usedOutline` (lines 120-135) and add `#include "eval.hpp"`. `exactScore` keeps using the 2-arg version to scope the outline BEFORE routing (channels are extracted inside that scope, so post-routing the 3-arg result equals the 2-arg result; the test asserts this equality holds).

- [ ] **Step 4: Run tests.** `bash test/run_p0_regressions.sh` + `make` + the four existing `test/run_*.sh` — Expected: all pass, warning-clean.

- [ ] **Step 5: Commit.**

```bash
git add src/eval.hpp src/eval.cpp src/io.cpp test/regression_p0_20260706.cpp
git commit -m "fix: single shared usedOutline for scoring and cfg output"
```

---

