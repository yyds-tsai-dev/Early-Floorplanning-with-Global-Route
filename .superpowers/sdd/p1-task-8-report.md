# P1 Task 8 Report — finalizeFeedthrough outline growth + P1 acceptance

## Status: DONE (verification task — no src/ fix was needed)

## What I found
`finalizeFeedthrough`'s paydown-first guard (`acceptFeedthroughPaydown` + `validateSolution().ok`,
commit `16cf24b`) and T4/T4.5's `shrinkWrapOutline`-inside-`decode` (which grows the outline toward
the inflated Problem's MAX ceiling for free on every re-decode) already jointly implement the plan's
reconciled Task 8 guard rule. No code change was required.

## Verification run (P1 acceptance criteria)
`bin/efgr ... --engine hybrid --iters 2000 --seed 1`:
- case1/hybrid: `feedthrough_mismatch=0 open_routes=0 validation=ok` (score=6.0819e10, outline_area=7.06371e6)
- case3/hybrid: `feedthrough_mismatch=0 open_routes=0 validation=ok` (score=1.10756e11, outline_area=1.98094e7)

Both criteria PASS. Runtime ~1.3s / ~4.2s respectively — no "minutes" concern.

## Changes made
1. `test/regression_p1_20260706.cpp` — added `testFinalizeFeedthroughAcceptanceCase1Hybrid` /
   `Case3Hybrid` (full hybrid search at iters=2000/seed=1, asserting mismatch==0, open_routes==0,
   validateSolution ok). Runs in the permanent suite (whole file ~15s), no separate opt-in script
   needed.
2. `docs/research/2026-07-06-cost-reduction-strategy.md` — appended
   "Appendix: P1 outline shrink-wrap results (2026-07-07)" with the 15-row sweep (5 cases x
   sp/bstar/hybrid, iters=2000, seed=1) vs. the immediate pre-P1 baseline (the cheapScore
   route-aware-penalty-fix appendix's AFTER column). Marked roadmap P1 items 5-6 done.
3. `docs/superpowers/plans/2026-07-06-p1-outline-shrinkwrap.md` — checked off Task 8's boxes, noting
   the guard/growth was already satisfied by prior commits.

## Sweep highlights vs pre-P1 baseline
- All 15 rows now report `feedthrough_mismatch=0` (previously nonzero on case1/sp 6630.59 and
  case2/sp 16714.8 in the immediate baseline).
- `outline_area` shrinks in 11/15 rows, up to -33% (case4/sp), -26% (case3/sp), -19% (case2/bstar).
- Reported `score` rises in 14/15 rows because `channel_overflow` grows as channels tighten — the
  R1 area-vs-overflow trade the design doc flags as gated on P2/P3, not a regression.

## Commits
- `6228bc3` test: lock in P1 feedthrough finalize acceptance criteria
- `ec3d6c4` docs: record P1 outline shrink-wrap results

No `fix:` commit — nothing to fix.

## Concerns
- `channel_overflow` penalty growth on 14/15 rows is expected and explicitly gated on P2 (cost-driven
  routing) / P3 (demand-driven spacing) per the plan's own §5/§6 risk analysis, not a defect here.
- `CLAUDE.md` had pre-existing unstaged changes from another session; left untouched/uncommitted.

Report path: `.superpowers/sdd/p1-task-8-report.md`
