### Task 6: Beta packaging, final review, organizer QA draft

**Files:**
- Create: `docs/official/qa-draft-20260722.md`
- Modify: `.superpowers/sdd/progress.md` (append plan outcome)

- [ ] **Step 1: Whole-branch final review.** Dispatch code review over `6d2de22..HEAD` (all commits from this plan): one `deep-reasoner` pass + one Codex `review` (per CLAUDE.md, run Codex from the WSL native shell). Blocking findings fixed before packaging.

- [ ] **Step 2: Package.**

```bash
make clean && make
for i in 0 1 2 3 4; do
  bin/EarlyFloorplanning_with_GlobalRoute "test/2026 ICCAD_case.case$i.csv" || exit 1
done
ls -la bin/EarlyFloorplanning_with_GlobalRoute
```

Expected: exit 0 on all five (validation ok, no open routes); the binary at the contest-required name. Note: the harness invokes with only the CSV — the default output path (`<input>.cfg`, main.cpp:37-44) is the submission contract; the loop above exercises exactly that call shape. Delete the generated `test/*.cfg` afterward (gitignored, but keep the tree clean): `rm -f "test/2026 ICCAD_case.case"*.cfg`.

- [ ] **Step 3: Organizer QA draft** — write `docs/official/qa-draft-20260722.md` (Traditional Chinese) with three questions, each with the evidence one line each: (1) α 矛盾：A35.5 說以 testcase 檔為準（五案皆 0.2），但 alpha 排行榜 `case_result.json` 使用 per-case α 0.3/0.15/0.1/0.1/0.1 並含 `runtime_penalty`（δ=0.5）—— 正式評分以何者為準？(2) case3 對角自連接（BLK09→BLK09=63、BLK13→BLK13=123）：A44 已承諾修正，但 20260712 測資仍含兩筆 —— 是否會再更新？(3) A38 承諾的 3 筆新測資（ETA 7/15）尚未見公布 —— 預計何時提供？

- [ ] **Step 4: Close out.** Append to `.superpowers/sdd/progress.md`: plan name, per-task commits, sweep-chosen default (engine/budget), before/after benchmark totals, tail-form finding, risk-fuse status. Update the `official-cost-function` memory accordingly. Push via `scripts/update.sh` only if the user asks.

---

## Execution notes

- **Critical path:** T0 → T1 → T2 → T3 → T5 → T6, with T4 parallel to T3 (different files; both touch tests only additively). Day 1 (today): T0–T3. Day 2: T4 finish, T5 sweep (the 90 sweep runs at ≤ 120 s each dominate — start it early), T6.
- **Delegation (CLAUDE.md orchestration):** T0, T1, T4 → `fast-worker` (mechanical, specs above are complete). T2, T3 → `deep-reasoner` implements with the plan + Codex adversarial review on T3. T5 sweep runs → `fast-worker`; default selection → orchestrator. T6 review → deep-reasoner + Codex; QA draft → orchestrator.
- **Parallel-session caution:** the C1 stash and the vanished `test/run_*.sh` modifications from the session-start snapshot suggest another session touched this tree today. Before T3's pop and before any `git add`, re-check `git status` matches expectations; on surprise, stop and surface to the user.
