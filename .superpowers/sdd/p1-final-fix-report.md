# P1 final-review fix list — report

Branch: `fable5`. Applies the 3-item fix list from the P1 whole-branch final review.

## 1. `outlineSweep` all-rungs-fail guard (Important)

`src/search.cpp` — `outlineSweep` (around what is now line ~186): if every rung in the margin
ladder fails its `anchored` check, the loop body never assigns `best`/`have_best`. Added a
post-loop guard:

```cpp
if (!have_best) {
    Placement fallback = base;
    fallback.outline_w = 0.0;
    fallback.outline_h = 0.0;
    shrinkWrapOutline(problem, fallback);
    RoutingResult routing;
    Score score = exactCached(fallback, &routing);
    best = OutlineSweepResult{score, fallback, routing};
    have_best = true;
}
```

This builds a guaranteed-legal fallback from `base` (outline reset + `shrinkWrapOutline`, whose
MAX-fallback contract guarantees legality) and exact-scores it via the caller's `exactCached`, so
callers never receive a default-constructed `OutlineSweepResult{}` (empty `rects`, `score.value`
0.0, `routing.open_routes` 0 by default) that `outlineSweepBetter` could otherwise prefer over a
legitimate incumbent.

Regression: `test/regression_p1_20260706.cpp` — new `testOutlineSweepNeverReturnsEmptyPlacement()`
(registered in `main()`). Runs `outlineSweep` on an ordinary case0 bstar seed-1 decode with a real
`exactCached` and asserts the structural invariant this bug violates: returned placement has
`outline_w > 0`, `outline_h > 0`, and `rects.size() == problem.blocks.size()`.

## 2. Stale table row (Minor)

`docs/research/2026-07-06-cost-reduction-strategy.md` — Appendix "P1 outline shrink-wrap results"
table, case3/sp row. Re-ran:

```
bin/efgr "test/2026 ICCAD_case.case3.csv" /tmp/p1fix.cfg --engine sp --iters 2000 --seed 1
```

Measured: `score=8.75294e+10 outline_area=1.9629e+07 wirelength=1.57443e+08
channel_overflow=87181.2 feedthrough_mismatch=0 open_routes=0 validation=ok` — matches the
task's expected values exactly. Replaced the stale row and added a footnote marker
`ok [^refreshed]` with a `[^refreshed]: row refreshed post-9c7d267 ladder fix (...)` definition
after the table, preserving the table's 8-column structure (an extra literal column would have
broken the Markdown table).

## 3. Re-stamp symmetry in `finalizeFeedthrough` (Minor)

`src/search.cpp` — `finalizeFeedthrough`, non-compacted commit path, immediately after
`placement = engine.decode(inflated, state);`:

```cpp
// Defensive symmetry with the compacted path below: decode stamps the declared outline
// today, but future engines may not -- guard against committing an unstamped placement.
if (placement.outline_w <= 0.0 || placement.outline_h <= 0.0) {
    shrinkWrapOutline(inflated, placement);
}
```

Mirrors the compacted path's existing defensive re-stamp.

## Verification

- `make`: warning-clean (`-Wall -Wextra -pedantic`), no warnings emitted.
- `bash test/run_p1_regressions.sh` — all 23 tests pass, including the new
  `testOutlineSweepNeverReturnsEmptyPlacement`.
- `bash test/run_p0_regressions.sh` — pass.
- `bash test/run_case00.sh` — pass (OUTLINE/BLOCK/CHANNEL/PATH present, 7 blocks).
- `bash test/run_placement_engine_regressions.sh` — pass.
- `bash test/run_20260618_regressions.sh` — pass.
- `bash test/run_20260608_regressions.sh` — pass.
- Determinism: `bin/efgr "test/2026 ICCAD_case.case0.csv" ... --engine hybrid --seed 1` run twice,
  `diff` reports no differences (byte-identical `.cfg` output).

## Files changed

- `src/search.cpp` (fixes 1 and 3)
- `test/regression_p1_20260706.cpp` (new regression test + includes + `main()` registration)
- `docs/research/2026-07-06-cost-reduction-strategy.md` (fix 2 — refreshed case3/sp row +
  footnote)

## Commit

`fix: harden outlineSweep fallback and finalize re-stamp per P1 final review`
