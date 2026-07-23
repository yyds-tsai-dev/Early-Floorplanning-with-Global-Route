### Task 0: Testcase refresh + diagonal-entry guard

**Files:**
- Modify: `test/2026 ICCAD_case.case0.csv`, `test/2026 ICCAD_case.case1.csv` (replace with 20260712 versions; case2/3/4 are byte-identical, replace anyway for provenance)
- Test: `test/regression_testdata_20260712.cpp`, `test/run_testdata_regressions.sh`
- Keep: `test/E_testcase_20260712.rar` (source archive, already in repo)

**Interfaces:**
- Consumes: `parseProblem(const std::string& path)` from `src/io.hpp`; `Problem`/`Bundle` from `src/model.hpp`.
- Produces: refreshed CSVs every later task runs against; a regression asserting (a) case0 BLK01 = 420×843 / MAX 2917×2220, (b) case1 BLK11 `port_edge == 3`, (c) **no bundle with `source == target`** on case3 (matrix has BLK09→BLK09=63 and BLK13→BLK13=123 — organizer-confirmed errata, QA A44, still present in 20260712).

- [ ] **Step 1: Extract the rar over `test/`**

```bash
cd /home/yyds/Early-Floorplanning-with-Global-Route
mkdir -p /tmp/tc20260712 && cd /tmp/tc20260712
# bsdtar reads this rar (WSL has no unrar; Windows tar.exe proved it readable)
tar -xf /home/yyds/Early-Floorplanning-with-Global-Route/test/E_testcase_20260712.rar
cp "testcase - upload - 20260712"/*.csv /home/yyds/Early-Floorplanning-with-Global-Route/test/
cd /home/yyds/Early-Floorplanning-with-Global-Route && git diff --stat -- test/
```

Expected: only `case0` and `case1` CSVs show content changes. If WSL `tar` cannot read the rar, run the extraction from the Windows side (`tar -xf` in PowerShell) into a temp dir and copy over.

- [ ] **Step 2: Write the failing regression**

Create `test/regression_testdata_20260712.cpp`:

```cpp
#include <cassert>
#include <cstdio>
#include <cmath>
#include "../src/io.hpp"

using namespace efgr;

int main() {
    // (a) case0: BLK01 fixed dims + MAX outline per 20260712 upload (QA A39)
    Problem p0 = parseProblem("test/2026 ICCAD_case.case0.csv");
    assert(std::abs(p0.outline_w - 2917.0) < 1e-9);
    assert(std::abs(p0.outline_h - 2220.0) < 1e-9);
    const Block& blk01 = p0.blocks.at(p0.block_index.at("BLK01"));
    assert(std::abs(blk01.width - 420.0) < 1e-9);
    assert(std::abs(blk01.height - 843.0) < 1e-9);
    assert(std::abs(blk01.area - 354060.0) < 1e-9);

    // (b) case1: BLK11 PORT EDGE now 3 (QA A36 — blank was erroneous)
    Problem p1 = parseProblem("test/2026 ICCAD_case.case1.csv");
    const Block& blk11 = p1.blocks.at(p1.block_index.at("BLK11"));
    assert(blk11.port_edge == 3 || (!blk11.port_edges.empty() && blk11.port_edges[0] == 3));

    // (c) case3: diagonal matrix entries must NOT become self-bundles (QA A44 errata)
    Problem p3 = parseProblem("test/2026 ICCAD_case.case3.csv");
    for (const Bundle& b : p3.bundles) assert(b.source != b.target);

    std::puts("testdata 20260712 regressions passed");
    return 0;
}
```

Create `test/run_testdata_regressions.sh` (mirror the existing runner pattern):

```bash
#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."

make >/dev/null

g++ -std=c++17 -O2 -Wall -Wextra -pedantic \
    test/regression_testdata_20260712.cpp \
    src/io.cpp \
    -o test/testdata_regression_bin

./test/testdata_regression_bin
echo "testdata regressions passed"
```

- [ ] **Step 3: Run it — assertion (c) is the one that may fail**

Run: `bash test/run_testdata_regressions.sh`
Expected: if `parseProblem` currently creates self-bundles from diagonal entries, assertion (c) aborts. If it already skips them (`r == c` guard in the matrix loop around `src/io.cpp:183`), the test passes — then Step 4 is a no-op; keep the regression either way.

- [ ] **Step 4 (only if Step 3 failed): Skip diagonal entries in the matrix parse**

In `src/io.cpp`, in the connection-matrix loop that creates `Bundle`s (around line 183 where `nets` is read), add the guard before pushing a bundle:

```cpp
if (source_index == target_index) continue;  // QA A44: diagonal entries are data errata
```

(Adapt the variable names to the loop's actual row/column index names.)

- [ ] **Step 5: Re-run + full suites**

Run: `bash test/run_testdata_regressions.sh && bash test/run_case00.sh && bash test/run_placement_engine_regressions.sh`
Expected: all pass. (case00 smoke uses its own bundled CSV; unaffected by the refresh.)

- [ ] **Step 6: Commit**

```bash
git add "test/2026 ICCAD_case.case0.csv" "test/2026 ICCAD_case.case1.csv" \
        "test/2026 ICCAD_case.case2.csv" "test/2026 ICCAD_case.case3.csv" \
        "test/2026 ICCAD_case.case4.csv" \
        test/regression_testdata_20260712.cpp test/run_testdata_regressions.sh src/io.cpp
git commit -m "chore(test): refresh testcases to 20260712 upload + diagonal-entry guard"
```

---

