# Task 1 Report: FT math helpers (`src/ft.hpp`) + official-example tests

## Status: DONE

## What I did

Followed the brief (`.superpowers/sdd/task-1-brief.md`) verbatim, TDD-style.

1. Checked repo preconditions first:
   - `src/model.hpp` already has `enum class BlockType { Edge, Macro, Soft };` (line 15) and `struct Block` with `std::vector<double> ft_rates;` (line 38) — matches the brief's test fixture.
   - `src/ft.hpp` did not exist yet.
   - Reviewed `test/run_20260618_regressions.sh` and `test/run_placement_engine_regressions.sh` for style; both use `make clean && make` plus a direct `g++ ... -o /tmp/...` compile+link of the regression `.cpp` against a specific `src/*.cpp` subset. The brief's script instead uses `make >/dev/null` (no clean) and outputs to `test/p0_regression_bin` — used the brief's script exactly as given since it was explicit and self-contained.

2. **Step 1 (RED):** Created `test/regression_p0_20260706.cpp` and `test/run_p0_regressions.sh` exactly as specified in the brief.

3. **Step 3 (verify RED):** Ran `bash test/run_p0_regressions.sh` in WSL — failed with the expected compile error:
   ```
   test/regression_p0_20260706.cpp:5:10: fatal error: ../src/ft.hpp: No such file or directory
       5 | #include "../src/ft.hpp"
         |          ^~~~~~~~~~~~~~~
   compilation terminated.
   ```

4. **Step 4 (implement):** Created `src/ft.hpp` with `ftTierRate`, `ftExtend`, `round2`, `ftRequiredArea` exactly as specified in the brief (all `inline`, in `namespace efgr`, `#pragma once`, includes `<cmath>` and `model.hpp`).

5. **Step 5 (verify GREEN):** Ran `bash test/run_p0_regressions.sh` again:
   ```
   testFtMathOfficialExamples ok
   p0 regressions passed
   P0 regressions passed
   ```

6. Ran the full existing regression suite to confirm no regressions:
   - `make clean && make` — warning-clean under `-Wall -Wextra -pedantic`.
   - `bash test/run_case00.sh` — passed (validation=ok, both bstar runs).
   - `bash test/run_placement_engine_regressions.sh` — `placement_engine_regression: ok`.
   - `bash test/run_20260618_regressions.sh` — `regression_20260618: ok`.
   - `bash test/run_20260608_regressions.sh` — `regression_20260608: ok`.

7. **Cleanup before commit:** `test/run_p0_regressions.sh` builds a binary at `test/p0_regression_bin` (per the brief's exact script), which is not under `bin/` and thus wasn't covered by the existing `.gitignore`. Added `test/p0_regression_bin` to `.gitignore` (one line, next to the existing `bin/` build-output entry) and deleted the stray binary before staging, consistent with the repo's existing "don't commit build outputs" convention. This is a minimal, in-scope hygiene fix directly required by running the brief's own script — not a design change.

8. **Committed** (from WSL, working tree `/home/yyds/Early-Floorplanning-with-Global-Route`):
   ```
   3a04376 feat: add spec-exact FT area math helpers with official-example tests
   ```
   Files: `src/ft.hpp`, `test/regression_p0_20260706.cpp`, `test/run_p0_regressions.sh`, `.gitignore`. Trailer `Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>` included as instructed.

9. **Self-review:** Re-ran `bash test/run_p0_regressions.sh` post-commit — output is pristine (`testFtMathOfficialExamples ok` / `p0 regressions passed` / `P0 regressions passed`, no warnings). Confirmed `git status --short` shows nothing but the pre-existing untracked `.superpowers/` (present before this task started, out of scope). Confirmed all new files are LF-terminated ASCII with no BOM, and `test/run_p0_regressions.sh` has the executable bit set (mode 100755 in the commit).

## Files changed
- `src/ft.hpp` (new) — header-only FT math helpers.
- `test/regression_p0_20260706.cpp` (new) — official-example regression test.
- `test/run_p0_regressions.sh` (new, executable) — build+run script.
- `.gitignore` (+1 line) — ignore `test/p0_regression_bin` build artifact.

## Concerns
None blocking. Two small notes for the orchestrator's awareness (not defects):
- The brief's `run_p0_regressions.sh` does `make >/dev/null` (not `make clean && make` like the other two runner scripts), so it can pick up a stale `bin/` if headers changed without touching `.cpp` mtimes; this matches the brief exactly and is consistent with how `make`'s dependency tracking is expected to work, so left as specified.
- `ft.hpp` is not yet included by any `src/*.cpp` (expected — Tasks 2/5 will consume it), so it currently only participates in the new regression binary's link line, which is why the brief's superset source list is required for `Block`/`BlockType` and any other symbols the linker needs.
