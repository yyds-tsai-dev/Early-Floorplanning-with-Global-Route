# Task 5 (AMENDED): `--time-budget` + sweep-selected beta defaults + engine adjudication

Supersedes `.superpowers/sdd/beta-task-5-brief.md` (mechanics unchanged; the sweep design and a new adjudication charge are amended). Beta deadline is TOMORROW (2026-07-24) — wall-clock discipline is part of the spec.

## Part A — time-budget mechanics (as original brief Step 1–3)

- `SearchOptions.time_budget_sec` (double, 0 = disabled), `--time-budget S` flag in main.cpp, usage string updated.
- In `runPlacementSearch`'s SA loop: `std::chrono::steady_clock` start before the loop; every 64 iterations (`(iter & 63) == 0`) check elapsed ≥ budget → break. When budget > 0, the iteration bound becomes `max(options.iters, 2'000'000)` so time is the binding stop.
- `runHybridPlacementSearch`: split the budget 50/50 between the sp and bstar legs (each leg's SearchOptions copy gets half). Legs that already take `iters` keep semantics when budget is 0.
- Sanity: `time bin/efgr "test/2026 ICCAD_case.case4.csv" /tmp/tb.cfg --time-budget 10` → wall ≈ 10–12 s, `iters` in stderr ≫ 1000, `validation=ok`.
- Determinism guard: all existing suites pass explicit `--iters` (budget off) — run the full 12-suite battery once before commit: run_case00, run_placement_engine_regressions, run_20260618_regressions, run_20260608_regressions, run_p0/p1/p2/p3_regressions, run_testdata_regressions, run_cfg_score_regressions, run_selfconsistency_regressions, run_integer_output_regressions.

## Part B — sweep design (amended)

Constraints: total sweep wall-clock ≤ 90 minutes; every number comes from `scripts/benchmark.sh`-style runs (solver + `bin/efgr_rescore`, checker_cost with CSV α; also record the ranking cost per the script's RANK_ALPHA/runtime-penalty columns). Write `scripts/sweep.sh` (reuse benchmark.sh machinery — env knobs or flags for engine/budget/seed; keep ≤ ~80 lines).

Suggested shape (you may redesign within the 90-min cap; justify in the report):
1. Screen at seed 1: engine ∈ {bstar, hybrid} × budget ∈ {0 (= iters 1000 reference), 15, 45, 90} over the 5 cases (~25–30 min).
2. Confirm: take the best (engine, budget) and its runner-up; re-run both at seeds 2 and 3 (~25 min).
3. Selection rule: minimize seed-averaged Σ_case checker_cost; tie-break lower Σ ranking cost; hard constraints: open_routes == 0 on every run; projected 8-case runtime = (Σ_5case runtime) × 8/5 × 1.5 ≤ 30 min.
4. Hardcode the winner as the no-flag default in main.cpp (engine default string and/or default time_budget_sec). Keep `--iters`/`--time-budget`/`--engine` overridable. Paste the aggregate table into the commit message body.

## Part C — engine adjudication charge (NEW)

Context you must weigh: Task 4 (integer decode, commit 2791587) regressed bstar checker_cost +14.5% at iters=1000 (case2 +28%, case4 +32%) while hybrid improved −16.1% (totals now: bstar ≈273.5M, hybrid ≈252.7M). The T4 implementer's investigated attribution: discretized geometry interacting with search.cpp's cheap-score top-16 elite selection — NOT an integerization-rule defect (legality/open_routes clean; seed-independent; non-monotonic in iters).

Your charge:
1. The sweep's screening data doubles as the measurement: if hybrid-at-budget dominates bstar-at-budget across cases, ship hybrid as default and record the bstar regression as a known issue for post-beta (no code change).
2. If bstar-at-budget would plausibly win but for the integer-decode interaction (e.g., it still leads on case0/1/3 and only case2/4 collapsed), you MAY spend up to ~60 minutes investigating the search.cpp interaction (start from the T4 report's evidence, `.superpowers/sdd/beta-task-4-report.md`; the lead is the cheap-score top-16 elite pass ranking candidates whose cheap proxy no longer correlates under integer geometry). A fix is admissible only if: bounded (search.cpp/eval.cpp proxy-side, no routing/scoring semantics), all 12 suites + self-consistency stay green, and the decisive sweep cells re-run confirm the improvement. Otherwise: revert to option 1 (ship the empirical winner) — do NOT burn the timeline chasing it.
3. Either way, the report must state the final default (engine + budget + expected 8-case projection) and the evidence table.

## Part D — integer-leak rider (from Task 4 review, Important; fix in this task because it lives in your files)

`anchorAtOutline`/`commitOutlineAt` intentionally preserve exact fractional targets (T7.1 contract), so integer-ness relies on CALLERS passing integral targets. Two caller sites leak fractional geometry into the final output when their candidate wins:
- `search.cpp` `outlineSweep`: margin-rung variants compute fractional target outlines; if such a variant wins, the final `.cfg` outline stamp AND re-anchored edge-block x/y are fractional (placeEdge's rounding guard is gated on `isIntegral(outline)`).
- `eval.cpp` corrective grow (:190-217): `grown_w/h = min(MAX, w*(1+margin))` is fractional; same leak via `anchorAtOutline`.

Fix: at BOTH call sites, ceil the target to integers before calling (`std::ceil` clamped to MAX — MAX is integer). Do NOT touch `anchorAtOutline`/`commitOutlineAt` themselves (their exact-target contract has its own test). This matters NOW because your time-budget raises iteration counts, increasing the odds a margin-rung/grow candidate wins. Verification: `bash test/run_integer_output_regressions.sh` stays green, plus one high-iters spot check (`--time-budget 30`, case2 and case4) asserting the emitted cfg has no fractional OUTLINE/BLOCK values (grep as in the T4 brief).

## Deliverables

- One commit: `feat(search): wall-clock time budget + sweep-selected beta defaults` (+ optional separate `fix(search): ...` commit if the Part C item 2 fix happens — two commits max), message(s) via /tmp/msg.txt with `Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>` trailer.
- Stage only: src/search.hpp, src/search.cpp, src/main.cpp, scripts/sweep.sh (+ the Part C fix files if any). NEVER stage CLAUDE.md, CONTEXT.md, docs/, .superpowers/, /tmp artifacts.
- Report to `.superpowers/sdd/beta-task-5-report.md`: sweep tables (screen + confirm), selection reasoning, Part C adjudication (path taken + evidence), 12-suite results, sanity-run output, files changed, self-review, concerns.
