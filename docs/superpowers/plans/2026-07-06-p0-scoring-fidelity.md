# P0 Scoring Fidelity Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make the solver's scoring and emitted `.cfg` match the official 20260608 spec + 20260618 Q&A exactly: correct FT area math (ceil + 2-decimal rounding), FT area written back into output block dims, zero wirelength for direct block abutment, and one shared outline computation for scoring and output.

**Architecture:** Add a small header-only FT-math module (`src/ft.hpp`) used by routing, eval, and a new post-search "feedthrough finalize" pass in `search.cpp` that re-decodes the best placement with inflated soft-block areas via the existing `PlacementEngine::seedFromPlacement`/`decode`. Outline computation moves to one exported function in `eval`. No new TUs (header-only helpers), so the Makefile and regression link lines stay valid.

**Tech Stack:** C++17, `make` (g++ `-std=c++17 -O2 -Wall -Wextra -pedantic`), shell regression scripts under `test/`.

## Global Constraints

- Build must stay warning-clean under `-Wall -Wextra -pedantic`.
- 4-space indent, code in `namespace efgr`, decls in `.hpp` / impls in `.cpp` (header-only inline helpers acceptable for `ft.hpp`).
- Deterministic: fixed seeds everywhere, no wall-clock dependence.
- Official FT math (spec p.8 + QA A34): per-side extend `= ceil((crossing_nets / 25) × tier_rate / 2)` (unconditional ceil), tier by crossing nets (≤3000, ≤6000, ≤9000, >9000), new area `= (sqrt(base_area) + extend)²` rounded to 2 decimals. Base area is ALWAYS the block's original input area (never compounded).
- Official examples that MUST pass (base area 87500, rates 20/40/80/100%): nets 1000 → extend 4, area 89882.43; nets 3500 → 28, 104849.02; nets 6711 → 108, 163057.66; nets 12013 → 241, 288158.52.
- Direct abutment path (2 rects, 1 guiding point) has TotalWireLength contribution 0 (cost formula sums HPWL over consecutive guiding-point pairs; with 1 point the sum is empty).
- Commits: lightweight Conventional Commits (`fix:`, `feat:`, `test:`). Never commit `bin/`, `*.o`, `*.cfg`.
- Run everything from a WSL shell at `/home/yyds/Early-Floorplanning-with-Global-Route`.

---

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

### Task 3: Direct abutment = zero wirelength

**Files:**
- Modify: `src/routing.cpp:186` (delete the 1-guide special case)
- Test: `test/regression_p0_20260706.cpp`

- [ ] **Step 1: Write the failing test:**

```cpp
void testDirectAbutmentZeroWirelength() {
    Problem p;
    auto addSoft = [&](const std::string& name, double x_hint) {
        (void)x_hint;
        Block b;
        b.name = name;
        b.area = 10000.0;
        b.type = BlockType::Soft;
        b.ar_min = 0.25;
        b.ar_max = 4.0;
        b.ft_rates = {0.2, 0.4, 0.8, 1.0};
        p.block_index[name] = static_cast<int>(p.blocks.size());
        p.blocks.push_back(b);
    };
    addSoft("BLK01", 0.0);
    addSoft("BLK02", 100.0);
    p.outline_w = 200.0;
    p.outline_h = 100.0;
    p.alpha = 1.0;
    Bundle bd;
    bd.source = 0;
    bd.target = 1;
    bd.nets = 100;
    p.bundles.push_back(bd);

    Placement pl;
    pl.rects = {Rect{0.0, 0.0, 100.0, 100.0}, Rect{100.0, 0.0, 100.0, 100.0}};
    pl.aspect = {1.0, 1.0};

    RoutingResult r = routeProblem(p, pl);
    assert(r.open_routes == 0);
    assert(r.routes.size() == 1);
    assert(r.routes[0].node_path.size() == 2);  // direct block->block, no channel
    assert(near(r.routes[0].wirelength, 0.0));  // 1 guiding point -> empty HPWL sum
    assert(near(r.total_wirelength, 0.0));
    std::cout << "testDirectAbutmentZeroWirelength ok\n";
}
```

Call it from `main()`.

- [ ] **Step 2: Run to verify it fails.** Expected: assert fails — current code adds `nets` (100) for 1-guide routes.

- [ ] **Step 3: Implement.** In `src/routing.cpp` delete the line `if (guides.size() == 1) route.wirelength += nets;` (line 186).

- [ ] **Step 4: Run tests.** `bash test/run_p0_regressions.sh` and the four existing `test/run_*.sh` — Expected: all pass.

- [ ] **Step 5: Commit.**

```bash
git add src/routing.cpp test/regression_p0_20260706.cpp
git commit -m "fix: direct block abutment routes have zero wirelength per cost spec"
```

---

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

### Task 6: Full verification + cost sweep

**Files:**
- Modify: `docs/research/2026-07-06-cost-reduction-strategy.md` (mark P0 items done)

- [ ] **Step 1: Full rebuild + all regressions.**

```bash
make clean && make
bash test/run_case00.sh
bash test/run_placement_engine_regressions.sh
bash test/run_20260618_regressions.sh
bash test/run_20260608_regressions.sh
bash test/run_p0_regressions.sh
```

Expected: everything passes, build warning-clean.

- [ ] **Step 2: Before/after cost table.** Run on every public testcase CSV in the repo (check `test/` and repo root for `*.csv`; at minimum case00):

```bash
for eng in sp bstar hybrid; do
  bin/efgr <case>.csv /tmp/p0_${eng}.cfg --engine $eng --iters 2000 --seed 1 2>&1 | grep -o 'score=.*'
done
```

Record `score`, `outline_area`, `wirelength`, `channel_overflow`, `feedthrough_mismatch`, `validation` per engine. Compare against the same commands on the pre-P0 commit (`git stash` or a worktree of the parent commit). Expected direction: `feedthrough_mismatch` becomes 0 everywhere; `wirelength` drops where abutment exists; `score` should not regress materially (small outline growth from FT write-back is the honest cost that judging would have charged anyway).

- [ ] **Step 3: Update the strategy doc.** In `docs/research/2026-07-06-cost-reduction-strategy.md` §4 P0: mark items 1-3 done with commit hashes; item 4 (graded overflow) — annotate: "deferred to P4: exact overflow penalty is already amount-proportional (1e6 × overflow um); only the SA weight *schedule* remains, which is search-engineering, not scoring fidelity."

- [ ] **Step 4: Commit.**

```bash
git add docs/research/2026-07-06-cost-reduction-strategy.md
git commit -m "docs: record P0 scoring-fidelity completion and cost sweep results"
```

---

## Self-Review

- **Spec coverage:** P0-1 (write-back) → Task 5; P0-2 (ceil + 2-dec) → Tasks 1-2; P0-3 (outline unify) → Task 4; P0-4 (graded overflow) → explicitly deferred with justification in Task 6 (exact penalty is already proportional — the strategy doc's "flat 1e6" claim was imprecise). Bonus spec fix found during code read: direct-abutment WL must be 0 → Task 3.
- **Placeholder scan:** no TBDs; the two "adjust if X" notes (regression g++ source list, OUTLINE token spelling, wall geometry) are explicit instructions to reconcile with existing code the worker can read, each with the exact file to check.
- **Type consistency:** `ftTierRate(const Block&, double)`, `ftExtend(double, double)`, `ftRequiredArea(double, double)`, `round2(double)` used identically in Tasks 1/2/5; `Block::ft_base_area` introduced in Task 2, consumed in Task 5; `usedOutline` 2-arg/3-arg per Task 4; `finalizeFeedthrough(const Problem&, const PlacementEngine&, SearchResult&)` declared and called with matching signature.
