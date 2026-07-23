# P2 Task 1 Report — pure cost-unit routing edge functions

## Status: DONE

## Commit
`6ba8adc` — `feat: cost-unit routing edge functions`
Files: `src/route_cost.hpp` (new, 69 lines), `test/regression_p2_20260707.cpp` (+133/-1, appended
T1 tests after the existing T0 tests, did not overwrite them).

## What was added
`src/route_cost.hpp` (header-only, mirrors `ft.hpp`, includes `model.hpp` + `ft.hpp`):
- `kOverflowAreaWeight = 1e6`, `kFtMismatchWeight = 1e6` — named constants matching eval.cpp:145-146
  (eval.cpp itself untouched, per brief).
- `channelOverflowArea(ch, horizontal, usage)` — file-local helper reproducing T0's exact area
  formula (missing dim = max(0, usage-cap)/25, spans the *other* dimension).
- `channelMarginalCost(ch, horizontal, usage, n)` = `kOverflowAreaWeight * (overflowArea(usage+n) -
  overflowArea(usage))`. Slack -> 0; saturated/straddling -> prices only the incremental overage.
- `softFtMarginalCost(b, acc, n, placed_area)` = `kFtMismatchWeight * (mismatch(acc+n) -
  mismatch(acc))` where `mismatch(a) = max(0, ftRequiredArea(base, ftExtend(a, ftTierRate(b,a))) -
  placed_area)`, `base = ft_base_area>0 ? ft_base_area : area`. Tier jumps (3000/6000/9000) price
  exactly via `ft.hpp`; free while `placed_area` covers the requirement.
- `wireCost(alpha, n, dist) = alpha*n*dist`.

## TDD
Appended to `test/regression_p2_20260707.cpp` (T0's 3 tests kept, +7 new): slack channel -> 0 (both
directions); saturated vertical (w=80,h=610,usage=2000,n=100) -> 2.44e9; saturated horizontal
symmetric fixture -> 2.44e9; straddling cap (usage=1900,n=200) -> only 100-over priced; FT tier-jump
(base=372949, acc=2900->tier 0.2, acc+n=3100->tier 0.4) priced exactly via ft.hpp, `placed_area`
exactly covering acc's requirement; generous `placed_area` -> 0; `wireCost` sanity. All 10 tests
(3 T0 + 7 T1) pass GREEN.

## Build / test results (verbatim tails)
- `make clean && make`: warning-clean, no output beyond compile/link lines.
- `bash test/run_p2_regressions.sh`: all 10 assertions printed `ok`, `P2 T0+T1 regressions passed`,
  `P2 regressions passed`.
- `bash test/run_p3_regressions.sh`: `P3 T1+T2+T3+T4+T5+T6 regressions passed`, `P3 regressions passed`.
- `bash test/run_p1_regressions.sh`: all listed, `p1 regressions passed`, `P1 regressions passed`.
- `bash test/run_p0_regressions.sh`: all listed, `p0 regressions passed`, `P0 regressions passed`.
- `bash test/run_case00.sh`: EXIT_OK, `validation=ok` on both printed lines.
- `bash test/run_placement_engine_regressions.sh`: EXIT_OK, `placement_engine_regression: ok`.
- `bash test/run_20260618_regressions.sh`: EXIT_OK, `regression_20260618: ok`.
- `bash test/run_20260608_regressions.sh`: EXIT_OK, `regression_20260608: ok`, `validation=ok`.

## Concerns
None. Pure addition, no existing file modified except the p2 test file (as scoped). CLAUDE.md's
pre-existing modified state was left untouched/unstaged, per instructions. `route_cost.hpp` is not
yet wired into `routing.cpp` (that's T2's job) — it is dead code from the build's perspective except
via the new test binary, exactly as scoped for this task.

## Report path
`\\wsl.localhost\ubuntu-24.04\home\yyds\Early-Floorplanning-with-Global-Route\.superpowers\sdd\p2-task-1-report.md`
