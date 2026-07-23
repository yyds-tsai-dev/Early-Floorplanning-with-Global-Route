### Task 1: FT math helpers (`src/ft.hpp`) + official-example tests

**Files:**
- Create: `src/ft.hpp`
- Create: `test/regression_p0_20260706.cpp`
- Create: `test/run_p0_regressions.sh`

**Interfaces:**
- Produces: `double efgr::ftTierRate(const Block& block, double nets)`, `double efgr::ftExtend(double nets, double rate)`, `double efgr::ftRequiredArea(double base_area, double extend)`, `double efgr::round2(double v)` — all `inline` in `src/ft.hpp`. Tasks 2 and 5 call these.

- [ ] **Step 1: Write the failing test.** Create `test/regression_p0_20260706.cpp`:

```cpp
#include <cassert>
#include <cmath>
#include <iostream>

#include "../src/ft.hpp"

namespace {
using namespace efgr;

bool near(double a, double b, double tol = 1e-6) { return std::abs(a - b) <= tol; }

Block softBlockWithRates() {
    Block b;
    b.type = BlockType::Soft;
    b.ft_rates = {0.2, 0.4, 0.8, 1.0};
    return b;
}

void testFtMathOfficialExamples() {
    Block b = softBlockWithRates();
    assert(near(ftTierRate(b, 1000.0), 0.2));
    assert(near(ftTierRate(b, 3500.0), 0.4));
    assert(near(ftTierRate(b, 6711.0), 0.8));
    assert(near(ftTierRate(b, 12013.0), 1.0));

    assert(near(ftExtend(1000.0, 0.2), 4.0));
    assert(near(ftExtend(3500.0, 0.4), 28.0));
    assert(near(ftExtend(6711.0, 0.8), 108.0));   // 107.376 -> ceil
    assert(near(ftExtend(12013.0, 1.0), 241.0));  // 240.26 -> ceil

    assert(near(ftRequiredArea(87500.0, 4.0), 89882.43));
    assert(near(ftRequiredArea(87500.0, 28.0), 104849.02));
    assert(near(ftRequiredArea(87500.0, 108.0), 163057.66));
    assert(near(ftRequiredArea(87500.0, 241.0), 288158.52));
    std::cout << "testFtMathOfficialExamples ok\n";
}
}  // namespace

int main() {
    testFtMathOfficialExamples();
    std::cout << "p0 regressions passed\n";
    return 0;
}
```

- [ ] **Step 2: Create `test/run_p0_regressions.sh`** (mirror the style of `test/run_20260618_regressions.sh` — rebuild, compile test linking the src files it needs, run):

```bash
#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."

make >/dev/null

g++ -std=c++17 -O2 -Wall -Wextra -pedantic \
    test/regression_p0_20260706.cpp \
    src/routing.cpp src/eval.cpp src/io.cpp src/validate.cpp \
    src/placement_engine.cpp src/search.cpp src/floorplan.cpp \
    -o test/p0_regression_bin

./test/p0_regression_bin
echo "P0 regressions passed"
```

Note: check first whether `src/search.cpp` links against `src/floorplan.cpp`; copy the exact source list from `test/run_placement_engine_regressions.sh` and add `src/io.cpp` if missing. Adjust the `g++` line so it links cleanly — the list above is the expected superset.

- [ ] **Step 3: Run to verify it fails.** `bash test/run_p0_regressions.sh` — Expected: compile error `ft.hpp: No such file or directory`.

- [ ] **Step 4: Implement `src/ft.hpp`:**

```cpp
#pragma once

#include <cmath>

#include "model.hpp"

namespace efgr {

// Official FT tiers (20260608 spec): <=3000, <=6000, <=9000, >9000.
inline double ftTierRate(const Block& block, double nets) {
    if (block.ft_rates.empty()) return 0.0;
    if (nets <= 3000.0) return block.ft_rates[0];
    if (nets <= 6000.0) return block.ft_rates.size() > 1 ? block.ft_rates[1] : block.ft_rates.back();
    if (nets <= 9000.0) return block.ft_rates.size() > 2 ? block.ft_rates[2] : block.ft_rates.back();
    return block.ft_rates.size() > 3 ? block.ft_rates[3] : block.ft_rates.back();
}

// Per-side extend, rounded up unconditionally (QA A34 / spec p.8).
inline double ftExtend(double nets, double rate) {
    return std::ceil((nets / 25.0) * rate * 0.5);
}

inline double round2(double v) { return std::round(v * 100.0) / 100.0; }

// New required area from the block's ORIGINAL input area (QA A32), 2-decimal rounded.
inline double ftRequiredArea(double base_area, double extend) {
    double side = std::sqrt(std::max(1.0, base_area)) + extend;
    return round2(side * side);
}

}  // namespace efgr
```

- [ ] **Step 5: Run to verify it passes.** `bash test/run_p0_regressions.sh` — Expected: `testFtMathOfficialExamples ok`, `P0 regressions passed`.

- [ ] **Step 6: Commit.**

```bash
git add src/ft.hpp test/regression_p0_20260706.cpp test/run_p0_regressions.sh
git commit -m "feat: add spec-exact FT area math helpers with official-example tests"
```

---

