### Task 2: Use FT helpers in routing + eval (kill duplicated, un-ceiled math)

**Files:**
- Modify: `src/routing.cpp:37-43` (delete local `ftRateFor`), `src/routing.cpp:234-242` (mismatch math)
- Modify: `src/eval.cpp:18-24` (delete `ftRateForProxy`), `src/eval.cpp:54-58` (proxy math)

**Interfaces:**
- Consumes: `ftTierRate`, `ftExtend`, `ftRequiredArea` from Task 1.

- [ ] **Step 1: Add a failing test** to `test/regression_p0_20260706.cpp` — routing's mismatch must use ceil'd math. Build a 3-blocks-in-a-row problem that forces feedthrough (no channel path), then assert `feedthrough_mismatch == required - actual` with the ceil'd required:

```cpp
// Add to the anonymous namespace:
Problem forcedFtProblem() {
    Problem p;
    auto addBlock = [&](const std::string& name, double area, double w, double h, BlockType type) {
        Block b;
        b.name = name;
        b.area = area;
        b.width = w;
        b.height = h;
        b.type = type;
        b.ar_min = 0.25;
        b.ar_max = 4.0;
        b.ft_rates = {0.2, 0.4, 0.8, 1.0};
        p.block_index[name] = static_cast<int>(p.blocks.size());
        p.blocks.push_back(b);
    };
    addBlock("BLK01", 30000.0, 100.0, 300.0, BlockType::Macro);
    addBlock("BLK02", 90000.0, 300.0, 300.0, BlockType::Soft);
    addBlock("BLK03", 30000.0, 100.0, 300.0, BlockType::Macro);
    p.outline_w = 500.0;  // exactly the row bbox: no channels anywhere
    p.outline_h = 300.0;
    p.alpha = 1.0;
    Bundle bd;
    bd.source = 0;
    bd.target = 2;
    bd.nets = 900;  // single chunk, tier 1
    p.bundles.push_back(bd);
    return p;
}

Placement rowPlacement() {
    Placement pl;
    pl.rects = {Rect{0.0, 0.0, 100.0, 300.0}, Rect{100.0, 0.0, 300.0, 300.0}, Rect{400.0, 0.0, 100.0, 300.0}};
    pl.aspect = {1.0, 1.0, 1.0};
    return pl;
}

void testRoutingMismatchUsesCeiledMath() {
    Problem p = forcedFtProblem();
    Placement pl = rowPlacement();
    RoutingResult r = routeProblem(p, pl);
    assert(r.open_routes == 0);
    assert(r.soft_feedthrough_nets[1] > 0.0);  // FT through BLK02 was forced
    double required = ftRequiredArea(90000.0, ftExtend(900.0, 0.2));  // extend = ceil(7.2) = 8 -> (300+8)^2 = 94864.00
    assert(near(required, 94864.00));
    assert(near(r.feedthrough_mismatch, required - 90000.0, 1e-3));
    std::cout << "testRoutingMismatchUsesCeiledMath ok\n";
}
```

Add `#include "../src/routing.hpp"` at the top and call `testRoutingMismatchUsesCeiledMath();` in `main()`.

- [ ] **Step 2: Run to verify it fails.** `bash test/run_p0_regressions.sh` — Expected: assert fails on `feedthrough_mismatch` (current code computes extend 7.2 un-ceiled → required 94412.6...).

- [ ] **Step 3: Implement.** In `src/routing.cpp`: add `#include "ft.hpp"`, delete the local `ftRateFor` (lines 37-43), and replace the mismatch block (lines 234-242) with:

```cpp
    for (std::size_t i = 0; i < result.soft_feedthrough_nets.size(); ++i) {
        double nets = result.soft_feedthrough_nets[i];
        if (nets <= 0.0 || !isSoft(problem.blocks[i].type)) continue;
        double base = problem.blocks[i].ft_base_area > 0.0 ? problem.blocks[i].ft_base_area : problem.blocks[i].area;
        double required_area = ftRequiredArea(base, ftExtend(nets, ftTierRate(problem.blocks[i], nets)));
        double actual_area = round2(placement.rects[i].w * placement.rects[i].h);
        if (actual_area + kEps < required_area) result.feedthrough_mismatch += required_area - actual_area;
    }
```

`ft_base_area` does not exist yet — add it now (it is also needed by Task 5): in `src/model.hpp` `struct Block`, after `double area = 0.0;` add `double ft_base_area = 0.0;  // original input area; FT required-area base (never inflated)`. In `src/io.cpp` `parseProblem`, right after `b.area = ...` is parsed (around line 156), add `b.ft_base_area = b.area;`.

In `src/eval.cpp`: add `#include "ft.hpp"`, delete `ftRateForProxy` (lines 18-24), and replace lines 54-58 with:

```cpp
            double base = problem.blocks[i].ft_base_area > 0.0 ? problem.blocks[i].ft_base_area : problem.blocks[i].area;
            double required_area = ftRequiredArea(base, ftExtend(bundle.nets, ftTierRate(problem.blocks[i], bundle.nets)));
            double actual_area = placement.rects[i].w * placement.rects[i].h;
            penalty += std::max(0.0, required_area - actual_area) * 1000.0;
```

- [ ] **Step 4: Run tests.** `bash test/run_p0_regressions.sh` — Expected: PASS. Also `make` — Expected: warning-clean.

- [ ] **Step 5: Run existing regressions.** `bash test/run_case00.sh && bash test/run_placement_engine_regressions.sh && bash test/run_20260618_regressions.sh && bash test/run_20260608_regressions.sh` — Expected: all pass (scores may shift slightly; these scripts assert structure/legality, not exact scores — if one asserts an exact score, update the expected value and say so in the commit).

- [ ] **Step 6: Commit.**

```bash
git add src/ft.hpp src/routing.cpp src/eval.cpp src/model.hpp src/io.cpp test/regression_p0_20260706.cpp
git commit -m "fix: use spec-exact ceil'd FT area math in routing mismatch and eval proxy"
```

---

