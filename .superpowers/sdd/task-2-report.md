# Task 2 Report: Use FT helpers in routing + eval

## Status: DONE

## Brief discrepancy found and resolved (with coordinator confirmation)

Brief Step 1's worked example for `testRoutingMismatchUsesCeiledMath` contained an
arithmetic slip: it computed `ftExtend(900.0, 0.2)` as `ceil(7.2) = 8` (giving
`required = (300+8)^2 = 94864.00`), but `7.2 = (900/25)*0.2` **omits the `*0.5`
per-side-split factor** that Task 1's `ft.hpp` actually applies:

```
ftExtend(nets, rate) = ceil((nets / 25.0) * rate * 0.5)
```

Task 1's `ft.hpp` was already validated against official spec examples in
`testFtMathOfficialExamples` (e.g. `ftExtend(1000.0, 0.2) == 4.0`,
`ftExtend(3500.0, 0.4) == 28.0`), so the `*0.5` term is correct and `ft.hpp`
needed no change. I ran the test as specified (calling the real `ftExtend`/
`ftRequiredArea` helpers rather than hardcoding numbers) and confirmed
`ftExtend(900.0, 0.2) = ceil(3.6) = 4`, giving `required = (300+4)^2 = 92416.00`.

Per my task rules I stopped and reported `NEEDS_CONTEXT` instead of guessing.
Coordinator confirmed: "Option 1 is correct — the brief's comment had an
arithmetic slip... ft.hpp is right as-is." I updated the test's expected value
and inline comment accordingly and proceeded.

## TDD evidence

### RED (before Step 3 implementation, after fixing the test's expected value to 92416.00)

Command:
```
wsl.exe -d Ubuntu-24.04 -- bash -l
cd /home/yyds/Early-Floorplanning-with-Global-Route
bash test/run_p0_regressions.sh
```
Output:
```
p0_regression_bin: test/regression_p0_20260706.cpp:83: void {anonymous}::testRoutingMismatchUsesCeiledMath(): Assertion `near(r.feedthrough_mismatch, required - 90000.0, 1e-3)' failed.
test/run_p0_regressions.sh: line 13: 59474 Aborted                 (core dumped) ./test/p0_regression_bin
```
This confirms: `required` itself (92416.00, computed via the real `ftExtend`/
`ftRequiredArea` helpers) passed its own assert; the failure is precisely on
`feedthrough_mismatch` comparing against `routing.cpp`'s then-current un-ceiled
local math, as intended by the brief.

### GREEN (after Step 3 implementation)

Command:
```
bash test/run_p0_regressions.sh
```
Output:
```
testFtMathOfficialExamples ok
testRoutingMismatchUsesCeiledMath ok
p0 regressions passed
P0 regressions passed
```

## Implementation (per brief Step 3, verbatim)

- `src/routing.cpp:1-9` — added `#include "ft.hpp"`.
- `src/routing.cpp` (was lines 37-43) — deleted local `ftRateFor`.
- `src/routing.cpp` (was lines 234-242, mismatch block) — replaced with the
  brief's exact code using `ftRequiredArea(base, ftExtend(nets, ftTierRate(...)))`
  and `round2(actual_area)`.
- `src/eval.cpp:1-5` — added `#include "ft.hpp"`.
- `src/eval.cpp` (was lines 18-24) — deleted local `ftRateForProxy`.
- `src/eval.cpp` (was lines 54-58, proxy block) — replaced with the brief's
  exact code.
- `src/model.hpp:29-33` — added `double ft_base_area = 0.0;` field to `struct
  Block`, right after `area`, with the brief's exact comment.
- `src/io.cpp:156-157` — added `b.ft_base_area = b.area;` immediately after
  `b.area` is parsed.
- `test/regression_p0_20260706.cpp` — added `#include "../src/routing.hpp"`,
  `forcedFtProblem()`, `rowPlacement()`, `testRoutingMismatchUsesCeiledMath()`
  (with the corrected expected value/comment), and the call in `main()`.

## Unplanned but necessary fallout: positional `Block{...}` initializers

Inserting `ft_base_area` between `area` and `width` in `struct Block` shifted
the aggregate-init field order, breaking every positional `Block{"...", ...}`
literal in three existing regression files (compile errors: "cannot convert
`efgr::BlockType` to `double`"). Fixed by inserting the matching `ft_base_area`
value (mirroring `area`, per `io.cpp`'s parse convention) in each literal:

- `test/placement_engine_regression.cpp:34-36,48-49` (5 literals)
- `test/regression_20260618.cpp:48-49,93,118-119` (5 literals)
- `test/regression_20260608.cpp:39-40,63-65,93-95` (8 literals)

Verified via `grep -rn 'Block{"' --include="*.cpp" --include="*.hpp" .` across
the whole repo that no other positional `Block{...}` sites were missed.

## Full verification run (all green)

```
make clean && make            -> CLEAN BUILD - no warnings/errors
bash test/run_p0_regressions.sh                        -> P0 regressions passed
bash test/run_case00.sh                                -> validation=ok (both engine runs)
bash test/run_placement_engine_regressions.sh          -> placement_engine_regression: ok
bash test/run_20260618_regressions.sh                  -> regression_20260618: ok
bash test/run_20260608_regressions.sh                  -> regression_20260608: ok
```

No existing regression asserts an exact score value (they check
`open_routes == 0`, `channel_overflow` thresholds, `feedthrough_mismatch >
1e-6` boolean-style checks, `validation=ok`, etc.), so none needed an
expected-value update for the ceil'd-math score shift — noted in the commit
message body regardless, per the brief's instruction to say so if applicable
(none applied here).

## Files changed

- `\\wsl.localhost\ubuntu-24.04\home\yyds\Early-Floorplanning-with-Global-Route\src\routing.cpp`
- `\\wsl.localhost\ubuntu-24.04\home\yyds\Early-Floorplanning-with-Global-Route\src\eval.cpp`
- `\\wsl.localhost\ubuntu-24.04\home\yyds\Early-Floorplanning-with-Global-Route\src\model.hpp`
- `\\wsl.localhost\ubuntu-24.04\home\yyds\Early-Floorplanning-with-Global-Route\src\io.cpp`
- `\\wsl.localhost\ubuntu-24.04\home\yyds\Early-Floorplanning-with-Global-Route\test\regression_p0_20260706.cpp`
- `\\wsl.localhost\ubuntu-24.04\home\yyds\Early-Floorplanning-with-Global-Route\test\placement_engine_regression.cpp`
- `\\wsl.localhost\ubuntu-24.04\home\yyds\Early-Floorplanning-with-Global-Route\test\regression_20260618.cpp`
- `\\wsl.localhost\ubuntu-24.04\home\yyds\Early-Floorplanning-with-Global-Route\test\regression_20260608.cpp`

## Concerns / notes for follow-up

1. **Brief arithmetic bug**: flagged and corrected with coordinator sign-off
   (see above). `ft.hpp` needed no change — its formula was already
   spec-validated in Task 1.
2. **`ft_base_area` fallback**: both call sites use `ft_base_area > 0.0 ?
   ft_base_area : area` as a safety net for ad-hoc test fixtures / any future
   `Block` construction path that doesn't go through `io.cpp`'s parser (where
   `ft_base_area` would otherwise default to `0.0`). This matches the brief's
   exact code; flagging only so it's visible that this fallback exists.
3. **Task 5 dependency**: `Block::ft_base_area` is now populated from parse
   but not yet consumed anywhere except these two call sites — Task 5 (per
   the task brief context) is expected to make further use of it (e.g. if
   soft-block area is inflated elsewhere during search, `ft_base_area` is the
   stable reference for FT math). No action taken here beyond what Task 2
   specifies.
4. **Test fixture blast radius**: the `Block` struct's positional-init
   sensitivity (breaking on any field insertion) is a pre-existing pattern in
   this codebase's test fixtures, not introduced by this task — flagging in
   case future tasks add more `Block` fields and hit the same fallout.

## Commit

```
262e77c fix: use spec-exact ceil'd FT area math in routing mismatch and eval proxy
```
