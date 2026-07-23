### Task 5: Feedthrough write-back (finalize pass)

**Files:**
- Modify: `src/search.hpp` (declare), `src/search.cpp` (implement + hook), uses `Block::ft_base_area` from Task 2
- Test: `test/regression_p0_20260706.cpp`

**Interfaces:**
- Consumes: `ftTierRate`/`ftExtend`/`ftRequiredArea` (Task 1), `Block::ft_base_area` (Task 2), `PlacementEngine::seedFromPlacement(const Problem&, const Placement&, std::mt19937&)` and `decode` (existing, `src/placement_engine.hpp:32-35`).
- Produces: `void finalizeFeedthrough(const Problem& problem, const PlacementEngine& engine, SearchResult& result);` declared in `src/search.hpp`. Called at the end of `runPlacementSearch` (4-arg overload, before `return result;` at `src/search.cpp:178`), so `sp`, `bstar`, and both hybrid halves all get it.

- [ ] **Step 1: Write the failing test:**

```cpp
void testFeedthroughFinalizeWritesBackArea() {
    Problem p = forcedFtProblem();
    p.outline_w = 620.0;  // headroom so the inflated decode still fits
    p.outline_h = 340.0;

    std::unique_ptr<PlacementEngine> engine = makePlacementEngine("bstar");
    SearchOptions options;
    options.iters = 200;
    options.seed = 1;
    options.exact_interval = 20;
    SearchResult result = runPlacementSearch(p, *engine, options);

    // After the finalize hook, the emitted solution must not owe FT area.
    assert(result.best_routing.feedthrough_mismatch <= 1e-6);
    // And if FT was used, the soft block's dims must cover the ceil'd required area.
    double nets = result.best_routing.soft_feedthrough_nets.size() > 1 ? result.best_routing.soft_feedthrough_nets[1] : 0.0;
    if (nets > 0.0) {
        double required = ftRequiredArea(90000.0, ftExtend(nets, ftTierRate(p.blocks[1], nets)));
        const Rect& r = result.best_placement.rects[1];
        assert(r.w * r.h + 1e-6 >= required);
    }
    auto validation = validateSolution(p, result.best_placement, result.best_routing);
    assert(validation.ok);
    std::cout << "testFeedthroughFinalizeWritesBackArea ok\n";
}
```

Add `#include <memory>`, `#include "../src/search.hpp"`, `#include "../src/placement_engine.hpp"`, `#include "../src/validate.hpp"`. Call it from `main()`.

Note: with outline 620×340 the SA may find a channel route (no FT) — then `nets == 0` and the test still passes via the mismatch/validation asserts. To make the FT branch deterministic, ALSO keep a direct unit test of the finalize function itself:

```cpp
void testFinalizeFeedthroughDirect() {
    Problem p = forcedFtProblem();  // 500x300, FT forced
    p.outline_w = 620.0;
    p.outline_h = 340.0;
    Placement pl = rowPlacement();
    // Wall off the top so channel routing cannot connect BLK01->BLK03 (forces FT):
    Block wall;
    wall.name = "BLK04";
    wall.area = 620.0 * 40.0;
    wall.width = 620.0;
    wall.height = 40.0;
    wall.type = BlockType::Macro;
    wall.ft_rates = {0.2, 0.4, 0.8, 1.0};
    p.block_index[wall.name] = static_cast<int>(p.blocks.size());
    p.blocks.push_back(wall);
    pl.rects.push_back(Rect{0.0, 300.0, 620.0, 40.0});
    pl.aspect.push_back(1.0);

    SearchResult result;
    result.best_placement = pl;
    result.best_score = exactScore(p, pl, &result.best_routing);
    assert(result.best_routing.feedthrough_mismatch > 0.0);  // precondition: FT happened, under-provisioned

    std::unique_ptr<PlacementEngine> engine = makePlacementEngine("bstar");
    finalizeFeedthrough(p, *engine, result);
    assert(result.best_routing.feedthrough_mismatch <= 1e-6);
    std::cout << "testFinalizeFeedthroughDirect ok\n";
}
```

If the precondition assert fails (router found a channel path around the wall), extend the wall geometry until `soft_feedthrough_nets[1] > 0` — the goal is a deterministic FT-forced fixture, adjust freely and note the final geometry in the commit message.

- [ ] **Step 2: Run to verify it fails.** Expected: compile error — `finalizeFeedthrough` undeclared.

- [ ] **Step 3: Implement.** In `src/search.hpp` add after `runHybridPlacementSearch`:

```cpp
void finalizeFeedthrough(const Problem& problem, const PlacementEngine& engine, SearchResult& result);
```

In `src/search.cpp` add `#include "ft.hpp"` and implement (file scope, above `runPlacementSearch`):

```cpp
void finalizeFeedthrough(const Problem& problem, const PlacementEngine& engine, SearchResult& result) {
    if (result.best_routing.feedthrough_mismatch <= kEps) return;
    Problem inflated = problem;
    Placement placement = result.best_placement;
    RoutingResult routing = result.best_routing;
    std::mt19937 rng(0x9e3779b9u);
    for (int pass = 0; pass < 3; ++pass) {
        bool grew = false;
        for (std::size_t i = 0; i < inflated.blocks.size(); ++i) {
            if (!isSoft(inflated.blocks[i].type)) continue;
            double nets = i < routing.soft_feedthrough_nets.size() ? routing.soft_feedthrough_nets[i] : 0.0;
            if (nets <= 0.0) continue;
            double base = problem.blocks[i].ft_base_area > 0.0 ? problem.blocks[i].ft_base_area : problem.blocks[i].area;
            double required = ftRequiredArea(base, ftExtend(nets, ftTierRate(problem.blocks[i], nets)));
            if (required > inflated.blocks[i].area + kEps) {
                inflated.blocks[i].area = required;
                grew = true;
            }
        }
        if (!grew) break;
        PlacementState state = engine.seedFromPlacement(inflated, placement, rng);
        placement = engine.decode(inflated, state);
        RoutingResult new_routing;
        Score exact = exactScore(inflated, placement, &new_routing);
        routing = new_routing;
        if (new_routing.feedthrough_mismatch <= kEps && new_routing.open_routes == 0 &&
            exact.value < result.best_score.value) {
            result.best_score = exact;
            result.best_placement = placement;
            result.best_routing = new_routing;
        }
    }
}
```

Hook it up: in the 4-arg `runPlacementSearch`, immediately before `return result;` (currently `src/search.cpp:178`, after the top-candidates loop) add:

```cpp
    finalizeFeedthrough(problem, engine, result);
```

Why `exactScore(inflated, ...)` is comparable to `result.best_score`: both are `outline_area + alpha·WL + penalty` on the same outline basis, and the mismatch term inside `routeProblem` uses `ft_base_area` (the original area) in both, so inflating `Block::area` (used only for decode sizing) does not change the mismatch baseline. The old best carries `mismatch × 1e6` penalty, so any legal inflated variant with sane geometry wins.

- [ ] **Step 4: Run tests.** `bash test/run_p0_regressions.sh` — Expected: PASS (both new tests). Then `make` + all four existing `test/run_*.sh` — Expected: pass, warning-clean. If `validateSolution` rejects inflated soft dims (e.g. an area-equals-input check), report the exact failing message and relax that specific check for soft blocks to `>= input area` semantics — QA A1.2 requires output dims to REFLECT the FT increase, so a strict equality check is itself a spec bug. Keep any such validator change in this commit and call it out.

- [ ] **Step 5: Commit.**

```bash
git add src/search.hpp src/search.cpp test/regression_p0_20260706.cpp
git commit -m "feat: feedthrough finalize pass writes required FT area back into output dims"
```

---

