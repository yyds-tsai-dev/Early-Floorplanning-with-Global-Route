# Task 0 report: Testcase refresh + diagonal-entry guard

## What I implemented

1. **Refreshed 5 testcase CSVs** (`test/2026 ICCAD_case.case{0,1,2,3,4}.csv`) from the already-extracted
   20260712 upload (scratchpad path given by the orchestrator), copied byte-for-byte via PowerShell
   `Copy-Item` to preserve UTF-8 BOM + CRLF. Confirmed via `file` and `git diff --stat`:
   - `case0.csv`: BLK01 width 419.5→420, area 353638.5→354060; MAX outline 2916×2220→2917×2220.
   - `case1.csv`: BLK11 PORT EDGE blank→3.
   - `case2/3/4.csv`: byte-identical (0 diff lines), replaced anyway for provenance as the brief specified.

2. **New regression** `test/regression_testdata_20260712.cpp` — exact content from the brief (verbatim),
   asserting (a) case0 BLK01 dims/area + MAX outline, (b) case1 BLK11 `port_edge`, (c) no bundle with
   `source == target` in case3 (QA A44: BLK09→BLK09=63, BLK13→BLK13=123 diagonal errata).

3. **New runner** `test/run_testdata_regressions.sh` — brief's content, **with one deviation**: the link
   line. See "Deviation from brief" below.

4. **Diagonal-entry guard** in `src/io.cpp:185` (connection-matrix loop, inside the
   `if (dst_it != p.block_index.end() && nets > 0)` block, before the `push_back`):
   ```cpp
   if (src_it->second == dst_it->second) continue;  // QA A44: diagonal entries are data errata
   ```
   Step 3 confirmed this was needed (not a no-op) — see TDD evidence below.

## Deviation from brief (and why)

The brief's Step 2 g++ line (`test/regression_testdata_20260712.cpp src/io.cpp -o ...`) does not link.
`io.cpp` defines `writeCfg`, which calls `usedOutline` (declared in `src/eval.hpp`, defined in
`src/eval.cpp`) — an external symbol the linker must resolve for the whole `io.o` translation unit even
though the test's `main()` never calls `writeCfg`. This is a genuine link-time requirement, not
optional, and every sibling regression script that links `io.cpp` already accounts for it (e.g.
`run_20260618_regressions.sh` links `eval.cpp io.cpp placement_engine.cpp respace.cpp routing.cpp
search.cpp validate.cpp`).

I treated this as a mechanical build-dependency fix (not a design/judgement call) and updated the link
line in `test/run_testdata_regressions.sh` to the same proven file set:
```
g++ -std=c++17 -O2 -Wall -Wextra -pedantic \
    -I. \
    test/regression_testdata_20260712.cpp \
    src/eval.cpp src/io.cpp src/placement_engine.cpp src/respace.cpp src/routing.cpp src/search.cpp src/validate.cpp \
    -o test/testdata_regression_bin
```
Everything else in both new files matches the brief verbatim (including the em-dash in the code comment
on line 18 of the `.cpp`).

## TDD evidence

**Red (Step 3, before the `src/io.cpp` guard)** — compiled with the corrected link line, ran the binary
directly:
```
EXIT_CODE=134
test/regression_testdata_20260712.cpp:25: int main(): Assertion `b.source != b.target' failed.
```
This confirms assertion (c) fails without the guard — Step 4 was required, not a no-op.

**Green (Step 5, after the guard, via the actual `test/run_testdata_regressions.sh`)**:
```
testdata 20260712 regressions passed
testdata regressions passed
```
Exit code 0, no compiler warnings in the build log.

**`bash test/run_case00.sh`** (clean `make clean && make`, both binaries):
```
engine=bstar iters=500 exact_evals=351 blocks=7 bundles=11 channels=8 routes=11 score=9.02874e+06 outline_area=6.46288e+06 wirelength=6.42132e+06 channel_overflow=0 feedthrough_mismatch=0 open_routes=0 validation=ok
engine=bstar iters=25 exact_evals=204 blocks=7 bundles=11 channels=10 routes=11 score=1.32509e+07 outline_area=5.52633e+06 wirelength=6.48272e+06 channel_overflow=0 feedthrough_mismatch=0 open_routes=0 validation=ok
```
Exit code 0, `validation=ok` / `open_routes=0` both runs, no compiler warnings.

**`bash test/run_placement_engine_regressions.sh`** (clean `make clean && make`, sp/bstar/hybrid loop):
```
placement_engine_regression: ok
```
Exit code 0, no compiler warnings. (The per-engine `sp`/`bstar`/`hybrid` loop with its `test -s` /
`grep -q` checks ran silently to completion under `set -euo pipefail`, consistent with all checks
passing.)

All three suites were also run chained (`bash test/run_testdata_regressions.sh && bash
test/run_case00.sh && bash test/run_placement_engine_regressions.sh`) with a clean exit; I re-ran them
individually afterward (logged to files) purely to get non-interleaved verbatim output for this report,
since the chained run's live terminal output was truncated/interleaved by the tool's output buffering
(a display artifact, not a process issue — confirmed by re-running each script alone with output
redirected to a log file and an explicit exit-code marker).

## Files changed

- `\\wsl.localhost\ubuntu-24.04\home\yyds\Early-Floorplanning-with-Global-Route\test\2026 ICCAD_case.case0.csv` — replaced (BLK01 dims/area, MAX outline)
- `\\wsl.localhost\ubuntu-24.04\home\yyds\Early-Floorplanning-with-Global-Route\test\2026 ICCAD_case.case1.csv` — replaced (BLK11 PORT EDGE)
- `\\wsl.localhost\ubuntu-24.04\home\yyds\Early-Floorplanning-with-Global-Route\test\2026 ICCAD_case.case2.csv` — replaced, byte-identical
- `\\wsl.localhost\ubuntu-24.04\home\yyds\Early-Floorplanning-with-Global-Route\test\2026 ICCAD_case.case3.csv` — replaced, byte-identical
- `\\wsl.localhost\ubuntu-24.04\home\yyds\Early-Floorplanning-with-Global-Route\test\2026 ICCAD_case.case4.csv` — replaced, byte-identical
- `\\wsl.localhost\ubuntu-24.04\home\yyds\Early-Floorplanning-with-Global-Route\test\regression_testdata_20260712.cpp` — new, verbatim from brief
- `\\wsl.localhost\ubuntu-24.04\home\yyds\Early-Floorplanning-with-Global-Route\test\run_testdata_regressions.sh` — new, link line adjusted (see deviation above)
- `\\wsl.localhost\ubuntu-24.04\home\yyds\Early-Floorplanning-with-Global-Route\src\io.cpp:185` — added diagonal-entry guard

Commit `412eeb7` — `chore(test): refresh testcases to 20260712 upload + diagonal-entry guard` (+
`Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>` trailer per orchestrator instruction). Exactly
the 5 files listed above plus `src/io.cpp` are in the commit (case2-4 show 0 diff since content is
identical, so `git add` on them was a no-op — nothing to commit for those three beyond re-confirming
they match).

`CLAUDE.md` and `CONTEXT.md` (pre-existing dirty files) and all `.superpowers/` scratch files were left
untouched and unstaged, as instructed.

## Self-review

- **Completeness**: all 6 steps done — CSV refresh verified, regression + runner created, red confirmed,
  guard added, green confirmed (+ case00 + placement-engine suites), committed with exactly the
  specified file set.
- **Quality**: the guard is a single-line, minimal, in-place `continue` matching the existing loop's
  style and variable names (`src_it->second`, `dst_it->second`) — no refactor of the surrounding parse
  loop.
- **Discipline**: no unrelated files touched or staged; no architectural changes; the one deviation
  (link line) is mechanical and matches an existing, proven pattern elsewhere in the same directory —
  documented above rather than silently changed.
- **Testing**: red before green for assertion (c); all three required suites pass warning-clean.

## Concerns

- **Minor / non-blocking**: `test/testdata_regression_bin` (the compiled test binary, per the brief's
  own `-o test/testdata_regression_bin`) lands inside `test/` rather than `/tmp` like every sibling
  script (`run_20260618_regressions.sh`, `run_placement_engine_regressions.sh`, etc. all compile to
  `/tmp/...`), and unlike the `p0`-`p3` binaries it is **not** covered by `.gitignore` (which has
  explicit entries for `test/p0_regression_bin` through `p3_regression_bin` but nothing for this new
  name). I did not add a `.gitignore` entry since it wasn't in my authorized file list for this task —
  I only deleted the leftover binary before staging so it wasn't accidentally committed. Worth a
  one-line `.gitignore` addition (`test/testdata_regression_bin`) in a follow-up if this runner sticks
  around long-term.
- The brief's Step 2 g++ line, as literally written, does not compile against the current `io.cpp`
  (needs `eval.cpp` + its transitive dependencies). Flagged above with the fix applied; worth relaying
  back in case the brief template is reused for future test additions that link `io.cpp` alone.
