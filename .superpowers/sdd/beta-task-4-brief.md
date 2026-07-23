### Task 4: Integer decode (QA A43) — may run in parallel with Task 3 (file-disjoint except tests)

**Files:**
- Modify: `src/placement_engine.cpp` (`dimensionFor` at :10-14, edge anchoring in `placeEdge`/`legalizeEdgeBlocks`, `shrinkWrapOutline`/`anchorAtOutline` outline stamping)
- Test: `test/regression_integer_output.cpp`, `test/run_integer_output_regressions.sh`

**Interfaces:**
- Consumes: `Block` (area, ar_min/ar_max, ft_base_area), `Rect`, `Placement`.
- Produces: every decoded `Rect` has integer `x, y, w, h`; every stamped `outline_w/h` integer; `writeCfg` output therefore all-integer with no code change there (`setprecision(3)` prints `640.000` — acceptable; optionally switch to integer printing, see Step 5).

**Integerization rules (contest-derived):**
1. Soft block, base sizing: for clamped target aspect `ar`, find integer `(w, h)` maximizing `w·h ≤ area` with `ar_min ≤ w/h ≤ ar_max`. Search: for `h` in `[max(1, floor(h0)−3), ceil(h0)+3]` where `h0 = sqrt(area/ar)`, take `w = floor(area/h)` clamped so `w/h` stays in `[ar_min, ar_max]`; keep the best `w·h`, tie-break on `|w/h − ar|`. (A43.3: undersize when equality impossible; the SA's aspect variable keeps exploring shapes, so a local integer optimum per aspect is sufficient.)
2. Soft block, FT-inflated re-decode: minimal integer `w·h ≥ required_area` within the AR range (same window search with `w = ceil(required_area/h)`), so no feedthrough-mismatch is charged for rounding.
3. Macro/Edge: dims are already integers in the 20260712 CSVs — assert, don't round.
4. Positions: decode produces `x, y` as sums of integer dims (contour/longest-path) — integer by construction once dims are integers; edge-block anchors computed from outline fractions (thirds of 2917 are fractional) must be `std::llround`ed then clamped so the block stays inside `[0, outline]` and keeps its required LOCATION overlap (1D check, QA A20 — a 1um shift cannot break an overlap that was ≥ 1um; add the assert).
5. Outline stamps: `shrinkWrapOutline`/`anchorAtOutline` results `ceil`ed to integers, clamped to MAX (MAX is integer).

- [ ] **Step 1: Write the failing regression**

`test/regression_integer_output.cpp`:

```cpp
#include <cassert>
#include <cmath>
#include <cstdio>
#include <memory>
#include "../src/io.hpp"
#include "../src/placement_engine.hpp"
#include "../src/search.hpp"

using namespace efgr;

static bool isInt(double v) { return std::abs(v - std::llround(v)) < 1e-9; }

int main() {
    for (int c = 0; c < 5; ++c) {
        Problem problem = parseProblem("test/2026 ICCAD_case.case" + std::to_string(c) + ".csv");
        SearchOptions opt; opt.iters = 200; opt.seed = 1;
        std::unique_ptr<PlacementEngine> engine = makePlacementEngine("bstar");
        SearchResult r = runPlacementSearch(problem, *engine, opt);
        assert(r.best_routing.open_routes == 0);
        for (const Rect& rect : r.best_placement.rects) {
            assert(isInt(rect.x) && isInt(rect.y) && isInt(rect.w) && isInt(rect.h));
        }
        if (r.best_placement.outline_w > 0.0) {
            assert(isInt(r.best_placement.outline_w) && isInt(r.best_placement.outline_h));
        }
        // Soft blocks must not exceed their base area unless FT-inflated (rule 1 vs rule 2).
        for (std::size_t i = 0; i < problem.blocks.size(); ++i) {
            const Block& b = problem.blocks[i];
            if (!isSoft(b.type)) continue;
            double placed = r.best_placement.rects[i].w * r.best_placement.rects[i].h;
            double ft_nets = i < r.best_routing.soft_feedthrough_nets.size()
                                 ? r.best_routing.soft_feedthrough_nets[i] : 0.0;
            if (ft_nets <= 0.0) assert(placed <= b.area + 1e-6);
        }
        std::printf("case%d integer decode ok\n", c);
    }
    std::puts("integer output regressions passed");
    return 0;
}
```

Runner `test/run_integer_output_regressions.sh` — same pattern as `run_p1_regressions.sh` (link `src/routing.cpp src/eval.cpp src/io.cpp src/validate.cpp src/placement_engine.cpp src/search.cpp src/floorplan.cpp src/respace.cpp src/ft.cpp` — copy the exact list from `test/run_p1_regressions.sh` plus any files it names).

- [ ] **Step 2: Run to verify it fails** — expect the `isInt` assertions to abort on soft-block dims (e.g. case0 BLK02 is currently 640.3007×640.3007).

- [ ] **Step 3: Implement rules 1–5 in `placement_engine.cpp`.** The single choke point for dims is `dimensionFor(const Block&, double aspect)` (:10) — implement the integer window search there (rule 1) and add a required-area variant for the FT re-decode path (rule 2; find its caller by grepping `ftRequiredArea` in placement_engine.cpp/search.cpp). Anchors and outline stamps per rules 4–5.

- [ ] **Step 4: Run regression + full suites + benchmark delta**

Run: `bash test/run_integer_output_regressions.sh`, the 8 suites, then `bash scripts/benchmark.sh --iters 1000`.
Expected: all green. Benchmark `checker_cost` moves only marginally (integer quantization loses < 0.1% area utilization); open_routes stays 0. Verify the emitted cfg has no fractional values: `grep -E "\.[0-9]*[1-9]" /tmp/bench_bstar_case0.cfg | grep -v "^PATH"` → empty.

- [ ] **Step 5 (cosmetic, optional): integer printing.** In `writeCfg` (io.cpp:201), values are now integral; `%.3f` prints `640.000`. The alpha checker accepted decimals, so leaving it is safe. If changing: print with `std::llround` for OUTLINE/BLOCK/CHANNEL lines only — PATH lines contain names/edges, untouched. Re-run the cfg_score golden regression afterward (the reader must still parse both styles — it must, for the alpha-package files).

- [ ] **Step 6: Commit**

```bash
git add src/placement_engine.cpp test/regression_integer_output.cpp test/run_integer_output_regressions.sh src/io.cpp
git commit -m "feat(placement): integer-quantized decode per QA A43"
```

---

