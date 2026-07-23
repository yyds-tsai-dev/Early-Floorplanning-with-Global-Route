### Task 3: Restore C1 (official cost in `exactScore`) and prove no regression

**Files:**
- Modify (via `git stash pop`): `src/eval.cpp`, `src/model.hpp`, `src/route_cost.hpp`, `src/routing.cpp`
- Possibly modify: tail form per Task 2 Step 5's finding

**Interfaces:**
- Consumes: `channel_overflow_nets` / `channel_capacity_total` (added to `RoutingResult` by the stash), `kBetaLin`/`kBetaTail`/`kOverflowRateThreshold`/`kBetaFt` (added to `model.hpp` by the stash), the proven tail form from Task 2.
- Produces: `exactScore` whose penalty is `kBetaLin·A·R + kBetaTail·A·max(0,R−θ) + kBetaFt·feedthrough_mismatch + 1e15·open_routes` — the same formula `officialScoreCfg` computes, so solver-reported score components and the rescorer agree on identical solutions.

- [ ] **Step 1: Baseline benchmark BEFORE popping**

Run: `bash scripts/benchmark.sh --iters 1000 | tee /tmp/bench_pre_c1.txt`

- [ ] **Step 2: Pop the stash (guarded)**

```bash
git stash list   # must still show stash@{0}: WIP on fable5: 6d2de22
git status --short -- src/   # must be empty
git stash pop
git diff --stat  # expect: src/eval.cpp src/model.hpp src/route_cost.hpp src/routing.cpp
```

If the stash is missing or `src/` is dirty: STOP, report to the user (another session owns the state).

- [ ] **Step 3: Reconcile with Task 1/2.** The stash predates Task 1's extraction — expect a conflict in `routing.cpp` (stash adds `channel_capacity_total` accumulation near the overflow loop at :408-425). Resolve keeping BOTH: the extraction structure and the stash's nets-denominated `channel_overflow_nets`/`channel_capacity_total` fields. If Task 2 proved a tail form ≠ H1, port it into `routeAndScoreAt` now. Reviewer focus: `route_cost.hpp`'s marginal-cost weights must price in the SAME currency the new `exactScore` charges (the stash edits both — verify the router's `channelMarginalCost`/`softFtMarginalCost` callers still compare like units; this is the P2 §7 router-scorer consistency invariant).

- [ ] **Step 4: Build + full suites**

Run: `make` then all 8 suite scripts (Global Constraints list), plus `bash test/run_testdata_regressions.sh && bash test/run_cfg_score_regressions.sh`
Expected: warning-clean build; all green. Suites that assert absolute internal score values may need updating — legitimate (the objective changed); each such edit must be listed in the commit message.

- [ ] **Step 5: Cross-check solver vs rescorer on live output**

```bash
bin/efgr "test/2026 ICCAD_case.case0.csv" /tmp/c1_case0.cfg --seed 1 --iters 1000 2>/tmp/c1_case0.log
bin/efgr_rescore "test/2026 ICCAD_case.case0.csv" /tmp/c1_case0.cfg
grep -o "score=[0-9.e+]*" /tmp/c1_case0.log
```

Expected: solver `score` ≈ rescorer `checker_cost` (small drift from FT/edge terms is explainable; overflow penalty component must match to float noise — print both if in doubt).

- [ ] **Step 6: AFTER benchmark + regression gate**

Run: `bash scripts/benchmark.sh --iters 1000 | tee /tmp/bench_post_c1.txt` then compare:
- `checker_cost` TOTAL (both engines): post ≤ pre required. Expect a **large** drop — pre-C1 the SA sacrificed area/WL to phantom overflow (the alpha data shows the old internal penalty overweighted overflow ~2000×).
- open_routes must stay 0 on all 10 rows (the solver exit code enforces it — any nonzero exits 1 and the script aborts).
- If TOTAL regresses: investigate (most likely `route_cost.hpp` currency mismatch). If unresolvable within the day → **risk fuse**: `git stash` the C1 changes again, record in progress.md, continue to Task 5 with the 1e6 mapping.

- [ ] **Step 7: Codex adversarial review (high-stakes scoring change)**

Dispatch per CLAUDE.md: `node "$CLAUDE_PLUGIN_ROOT/scripts/codex-companion.mjs" adversarial-review --background` from the WSL shell, scoped to the C1 diff + `cfg_score.cpp`. Address Critical/Important findings before commit.

- [ ] **Step 8: Commit**

```bash
git add src/eval.cpp src/model.hpp src/route_cost.hpp src/routing.cpp
git commit -m "feat(eval): official checker cost mapping in exactScore (C1)"
```

Also: update the `official-cost-function` memory (C1 is now COMMITTED, not stashed; record the proven tail form) — the orchestrator does this, not a subagent.

---

