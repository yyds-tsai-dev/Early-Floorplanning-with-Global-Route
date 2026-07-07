# P2 Task 0 report — area-denominate channel overflow (design §8.1 R0)

## Change

`src/routing.cpp:221-239` (overflow accrual loop): converted `channel_overflow` from
nets-over-capacity to um^2, per plan Global Constraint 2 / design §8.1:

- horizontal overflow (`horizontal_usage > hcap`, `hcap = 25*h`): `missing_height =
  (horizontal_usage - hcap) / 25`; charged area = `missing_height * ch.rect.w` (span = channel
  WIDTH).
- vertical overflow (`vertical_usage > vcap`, `vcap = 25*w`): `missing_width = (vertical_usage -
  vcap) / 25`; charged area = `missing_width * ch.rect.h` (span = channel HEIGHT).
- Added a comment citing design §8.1 and the axis-crossing invariant (mirrors respace.cpp's
  `channelDemands` need_w/need_h mapping, but converts to area instead of a linear demand).

`src/eval.cpp:142-145`: added a one-line comment noting `channel_overflow` and
`feedthrough_mismatch` are now both um^2 so the shared `1e6` weight (`eval.cpp:143-144`) compares
like units. No formula change needed there — both terms were already `x 1e6`.

`.gitignore`: added `test/p2_regression_bin` (was missing; p0/p1/p3 binaries were already listed).

## New tests

`test/regression_p2_20260707.cpp` + `test/run_p2_regressions.sh` (mirrors the p3 pair's g++ src
list: routing/eval/io/validate/placement_engine/search/floorplan/respace).

- `testVerticalOverflowIsAreaDenominatedByChannelHeight`: channel w=80, span(h)=610, vertical usage
  3600 vs vcap=2000 -> expects `channel_overflow == (3600-2000)/25*610 == 39,040` (old code: 1600).
  Also asserts `> 1600` as a direction check against the pre-fix denomination.
- `testHorizontalOverflowIsAreaDenominatedByChannelWidth`: symmetric fixture, span = channel width.
- `testOverflowAreaComparableToFtMismatchMagnitude`: computes both `ftRequiredArea`-derived mismatch
  (base=372,949, tier 40% at 3600 nets, via `ft.hpp` functions, not hardcoded) and the fixture (a)
  overflow area, and asserts the ratio is within 1.3x (both directions) — actual ratio ~1.076,
  consistent with the design doc's ~1.08 worked-example ratio.

RED confirmed: stashed the `routing.cpp` fix, reran `run_p2_regressions.sh`, got an assertion
failure on the expected 39,040 value (old code produced 1600) — exit 134. Restored the fix,
rebuilt, reran: GREEN.

## Dependent audit

- **Test asserts on `channel_overflow`** (grepped all `test/*.cpp`): only two files reference it —
  `test/regression_20260608.cpp` (2 asserts, both `> 1e-6` zero-checks) and
  `test/regression_p3_20260707.cpp` (10 asserts, all `>`, `<`, `<=`, `==` threshold/direction/
  determinism/self-consistency comparisons — no fixed-literal magnitudes). None needed updates:
  zero stays zero in both denominations, and every non-zero assert is relative
  (baseline-vs-candidate, before-vs-after, or a plain nonzero precondition), never a hardcoded
  number.
- **`src/respace.cpp` `channelDemands`** (`respace.cpp:323-349`): reads `ch.horizontal_usage` /
  `ch.vertical_usage` directly (nets) to compute `need_w`/`need_h` — never reads
  `channel_overflow`. Confirmed unaffected by inspection; the full p3 suite (which exercises
  `channelDemands` extensively) still passes unchanged.
- **Paydown/sweep preference rules** (p0/p1/p3 finalize/respace/sweep logic): compare
  score/overflow values relatively (`<`, `<=`, best-of) — unaffected, per brief; confirmed by full
  green suite run, no rule touched.
- **docs**: none this task, per brief (T7 re-baselines the strategy doc's sweep numbers).

## Build & test results (WSL, warning-clean `-Wall -Wextra -pedantic`)

All green: `run_p2_regressions.sh` (new), `run_p3_regressions.sh`, `run_p1_regressions.sh`,
`run_p0_regressions.sh`, `run_case00.sh`, `run_placement_engine_regressions.sh`,
`run_20260618_regressions.sh`, `run_20260608_regressions.sh`.

## Commit

`fix: denominate channel overflow in area units` (body notes sweep re-baseline at T7).
