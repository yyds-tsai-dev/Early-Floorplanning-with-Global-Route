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

