### Task 5: `--time-budget` + default-selection sweep

**Files:**
- Modify: `src/search.hpp` (SearchOptions), `src/search.cpp` (SA loop stop condition, hybrid split), `src/main.cpp` (flag + chosen defaults)
- Create: `scripts/sweep.sh`
- Test: extend none — determinism suites keep running with explicit `--iters` (budget defaults OFF in tests)

**Interfaces:**
- Consumes: `SearchOptions`, `runPlacementSearch`, `runHybridPlacementSearch`.
- Produces: `SearchOptions.time_budget_sec` (double, 0 = disabled) honored by both search entry points; `--time-budget S` CLI flag; final hardcoded beta defaults in `main.cpp` chosen by the sweep.

- [ ] **Step 1: Implement the stop condition.**

`search.hpp`: add to `SearchOptions`:

```cpp
    double time_budget_sec = 0.0;  // 0 = disabled; wall-clock stop, checked every 64 iters
```

`search.cpp`: in the SA loop of `runPlacementSearch`, capture `auto t0 = std::chrono::steady_clock::now();` before the loop; inside, every 64 iterations:

```cpp
        if (options.time_budget_sec > 0.0 && (iter & 63) == 0) {
            double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
            if (elapsed >= options.time_budget_sec) break;
        }
```

When the budget is set, the iteration bound becomes `max(options.iters, 2'000'000)` so time, not the iter default, is the binding stop. `runHybridPlacementSearch`: split the budget 50/50 between the sp and bstar legs (each leg gets `time_budget_sec * 0.5` in its own SearchOptions copy). `main.cpp`: `options.time_budget_sec = argDouble(argc, argv, "--time-budget", 0.0);` and extend the usage string.

- [ ] **Step 2: Sanity run**

Run: `time bin/efgr "test/2026 ICCAD_case.case4.csv" /tmp/tb.cfg --time-budget 10 2>&1 | tail -1`
Expected: wall time ≈ 10–11 s (one in-flight exact eval may overshoot slightly); summary line shows `iters` ≫ 1000; `validation=ok`.

- [ ] **Step 3: Determinism guard** — run `bash test/run_placement_engine_regressions.sh` (explicit `--iters`, budget off) → green, byte-identical behavior.

- [ ] **Step 4: Sweep.** `scripts/sweep.sh` loops `engine ∈ {bstar, hybrid}` × `budget ∈ {0(=iters1000), 30, 60, 90, 120}` × `seed ∈ {1, 2, 3}` over the 5 cases, reusing `benchmark.sh`'s rescore+dual-cost machinery (factor the per-run scoring into a function both scripts source, or just extend benchmark.sh with `--budget`/`--seed` env knobs — implementer's choice, keep it ≤ 80 lines). Output: one CSV row per (engine, budget, seed, case) → `/tmp/sweep_results.csv`, plus an aggregate table of `Σ checker_cost` per (engine, budget) averaged over seeds.

- [ ] **Step 5: Pick and hardcode the default.** Selection rule (user-approved: best measured score): minimize seed-averaged `Σ_case checker_cost`, tie-break lower `Σ rank_cost`, subject to: projected 8-case runtime = `(Σ_5 runtime) × 8/5 × 1.5 (unknown-case factor) ≤ 30 min`, and per-case `open_routes == 0` on every seed. Hardcode the winner in `main.cpp` (`--engine` default string and/or `time_budget_sec` default when no flag is given — keep `--iters`/`--time-budget` overridable for tests). Paste the aggregate table into the commit message.

- [ ] **Step 6: Full suites + commit**

```bash
for s in run_case00 run_placement_engine_regressions run_20260618_regressions run_20260608_regressions \
         run_p0_regressions run_p1_regressions run_p2_regressions run_p3_regressions \
         run_testdata_regressions run_cfg_score_regressions run_integer_output_regressions; do
  bash test/$s.sh || exit 1
done
git add src/search.hpp src/search.cpp src/main.cpp scripts/sweep.sh
git commit -m "feat(search): wall-clock time budget + sweep-selected beta defaults"
```

---

