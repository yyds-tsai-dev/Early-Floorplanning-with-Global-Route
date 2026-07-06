# P1 Task 5 Report — exactScore routes inside declared outline + 1-step corrective grow

## Status: DONE (with scope note below)

## Files changed
- `src/eval.cpp` — `exactScore` now scopes routing to `placement.outline_w/h` (stamped) when set;
  when unset, recomputes via `shrinkWrapOutline` on a local copy (never mutates the caller's
  `placement`, per its `const&` signature). Extracted the route+score tail into a static
  `routeAndScoreAt` helper shared by the stamped path, the unset-recompute path, and the one-step
  corrective grow. Corrective grow: if `routing.open_routes > 0`, grow toward MAX by
  `max(5%, channel_overflow/25/max(w,h))`, re-legalize edges via `shrinkWrapOutline` scoped to a
  `Problem` whose ceiling *is* the grown target (so its bisection settles there rather than
  re-shrinking), re-route once, and keep the grown result iff it has fewer open routes or (tied)
  lower `score.value`.
- `src/search.cpp` — `compactTowardOrigin` (line ~58-61): resets `placement.outline_w/h = 0.0`
  after compaction (the required binding carry-over fix). **Scope note:** this alone left
  `compacted`/`best_placement` inconsistent (stamp 0 falls back to MAX in `validateSolution`, but
  the stored edge rects were never re-anchored to the smaller outline `exactScore` actually scored
  against internally) — this surfaced as real `edge location violation` failures in
  `run_placement_engine_regressions.sh` (sp engine, exit 1) once built. Fixed by re-stamping
  `compacted` via `shrinkWrapOutline` immediately at both call sites (`finalizeFeedthrough` and the
  elite pass) before it is cached/committed as `best_placement` — restores the "a placement's own
  stamp is always consistent with its own rects" invariant every other caller relies on. This is a
  second `search.cpp` change beyond the literally-scoped "only search.cpp change" (the outline_w/h
  reset); flagging it explicitly since it wasn't pre-authorized, but it was required for the
  smoke-test acceptance gate (`validation=ok`) and is minimal/mechanical (2 call sites, no new
  logic, reuses `shrinkWrapOutline`).
- `src/search.hpp` — exposed `compactTowardOrigin` (moved out of `search.cpp`'s anonymous
  namespace) so the new regression test can call it directly.
- `test/regression_p1_20260706.cpp` — two new tests (see below).
- `test/run_20260608_regressions.sh` — added missing `src/placement_engine.cpp` to the link line;
  this suite's g++ command didn't reference it before and now fails to link (`exactScore` calls
  `shrinkWrapOutline`). Pure build-plumbing fix, no test logic changed.

## Tests
- `testExactScoreRoutesInsideStampedDeclaredOutlineCase0`: case0 bstar seed 1 + 50 mutations ⇒
  stamped area < 0.95×MAX and `exactScore(...).outline_area == stamped W×H`. **Note:** verified
  empirically that `usedOutline`'s bbox always equals the stamp after any real `decode()` call
  (structurally guaranteed — `shrinkWrapOutline` re-anchors edges flush to the committed border,
  so the bbox of committed rects equals the committed (W,H) by construction). This means the test
  passes both before and after the `exactScore` fix on this specific input — it is a
  characterization test proving the invariant holds, not a RED-before/GREEN-after regression for
  the *natural decode* path. The genuine bug this task fixes (stamp vs. stale/recomputed geometry
  diverging) only manifests via `compactTowardOrigin`-produced or hand-built placements, which is
  exactly what test (b) below exercises, and what the `run_placement_engine_regressions.sh`
  failure above confirmed empirically (validation=fail, edge location violation, pre-fix).
- `testCompactTowardOriginResetsStampedOutline`: genuine RED→GREEN — asserts
  `compactTowardOrigin(...).outline_w/h == 0`; failed before the `search.cpp` fix (stamp was
  never reset), passes after.

## Suite results (all green)
- `run_p1_regressions.sh` — 15/15 ok
- `run_p0_regressions.sh` — 8/8 ok
- `run_case00.sh` — exit 0, both invocations `validation=ok`
- `run_placement_engine_regressions.sh` — exit 0 (was exit 1 with `edge location violation:
  BLK01/BLK04` on the `sp` engine before the search.cpp re-stamp fix)
- `run_20260618_regressions.sh` — exit 0
- `run_20260608_regressions.sh` — exit 0 (after adding `placement_engine.cpp` to its link line)

## Smoke tests (case0, seed 1, 2000 iters)
```
engine=bstar iters=2000 exact_evals=402 blocks=7 bundles=11 channels=8 routes=13 score=9.20647e+06 outline_area=6.46288e+06 wirelength=7.30996e+06 channel_overflow=0 feedthrough_mismatch=0 open_routes=0 validation=ok
engine=hybrid iters=2000 exact_evals=1177 blocks=7 bundles=11 channels=8 routes=13 score=1.26215e+07 outline_area=5.53447e+06 wirelength=4.77539e+06 channel_overflow=0 feedthrough_mismatch=0 open_routes=0 validation=ok
```
`hybrid` shows a real area win (5.53M vs 6.47M MAX, ~14.5% smaller). `bstar` at seed 1 lands at
essentially MAX (`6.46288e+06`) with `channel_overflow=0` — spot-checked seeds 2/3 and both DO
shrink substantially (`4.73e6`, `5.17e6`) but with nonzero `channel_overflow` (2643, 930). This
matches the design doc's own §6 R1 caveat verbatim: the `1e6×` overflow penalty dominates any area
term, so SA (without T7's elite outline sweep, explicitly out of scope for this task) settles at
whatever zero-overflow local optimum it finds first, which for seed 1's specific tree topology
happens to be near-MAX (same known pathology already documented in
`testShrinkWrapOutlineBisectionFindsNearMinimalOutlineCase0Raw`'s comments re: BLK01 vs BLK07).
Not a regression — T7 (elite-pass area↔overflow trade-off sweep) is the mechanism designed to fix
this and is a separate task.

## Concerns
1. Scope: fixing the stamp/rects consistency gap required a second `search.cpp` change (2-line
   re-stamp calls) beyond the pre-authorized outline reset. Flagged above; verified necessary via
   direct repro (placement_engine regression failed without it) and minimal in nature.
2. `bstar` seed-1 area win is negligible in this smoke run; real wins depend on T7 (not this task).
3. `exactScore`'s corrective-grow path is exercised by the design's contract but not hit by any of
   the 5 public cases at these seeds (`open_routes` was already 0 in every natural-decode
   scenario tried) — it remains defense-in-depth per the design, not empirically load-bearing yet.

## Report path
`.superpowers/sdd/p1-task-5-report.md` (this file, WSL path
`/home/yyds/Early-Floorplanning-with-Global-Route/.superpowers/sdd/p1-task-5-report.md`)
