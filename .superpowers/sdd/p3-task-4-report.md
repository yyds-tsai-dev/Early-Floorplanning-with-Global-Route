# P3 Task 4 report: route<->respace fixed point loop

**Status:** GREEN. Implemented `RespaceResult` / `respaceFixedPoint` in `src/respace.hpp`/`.cpp`
per the design (decision 5, plan Task 4). `make` warning-clean; all suites pass (p3, p1, p0,
run_case00, placement_engine, 20260618, 20260608).

**Commit:** `ebcf85f` `feat: route-respace fixed point loop`.

## What changed

- `src/respace.hpp:69-90` (public): `struct RespaceResult { Placement placement; RoutingResult
  routing; Score score; bool improved = false; };` and
  `RespaceResult respaceFixedPoint(const Problem&, const Placement&, const ExactCachedFn&);`.
  Included `search.hpp` from `respace.hpp` (no circular include -- verified `search.hpp` does not
  include `respace.hpp`, nor did anything else prior to this change).
- `src/respace.cpp:11-34`: `respaceCandidateBetter` (replicates `outlineSweepBetter`'s
  open_routes==0-first-then-lower-value rule locally, per the brief -- not moved/exposed across
  files) and `rectsUnchanged` (geometric no-op detector for the early-exit).
- `src/respace.cpp` (end): the loop itself -- scores `base` as the baseline/fallback, then up to 3
  iterations of `respaceToDemand` on the current routed placement + re-score via `exactCached`,
  keeping the best-so-far by `respaceCandidateBetter`; stops on `respaceToDemand` returning false,
  a geometric no-op, or the accepted routing already being `channel_overflow<=kEps &&
  open_routes==0`. Deterministic (no RNG).

## Key finding (course-corrected per coordinator direction)

The brief's original RED test (strict raw `channel_overflow` reduction on case1/bstar/seed1) does
not hold empirically -- not a bug in the loop. Root cause: case1's core blocks already saturate
`outline_w == problem.outline_w` (MAX, x-slack==0); only y has ~220 units of slack. `relaxToDemand`
correctly scales the infeasible x-axis demands to ~0 via the MAX secant clamp while the y-axis-only
widening goes through, but re-routing after that shift shuffles net traffic onto the still-
saturated x-locked channels, netting no strict overflow win within 3 iterations on this
axis-locked layout. Measured 5 cases x {sp, bstar}, seed 1 (bstar `channel_overflow` and
`respaceFixedPoint`'s outcome; sp likewise):

| case | engine | baseline overflow | best overflow after loop | improved |
|---|---|---|---|---|
| case0 | bstar | 3401.33 | 3401.33 | false |
| case0 | sp    | 3401.33 | 3401.33 | false |
| case1 | bstar | 180767  | 180767  | false |
| case1 | sp    | 171758  | 171836  | **true** (score.value marginally lower; overflow ticks up ~78, area shrinks more) |
| case2 | bstar | 144362  | 144362  | false |
| case2 | sp    | 145718  | 145718  | false |
| case3 | bstar | 304545  | 304545  | false |
| case3 | sp    | 318165  | 318165  | false |
| case4 | bstar | 326564  | 326564  | false |
| case4 | sp    | 760239  | 760239  | false |

Not one (case, engine) pair at seed 1 shows a strict raw-overflow win within 3 iterations -- all
these layouts are axis-locked (zero slack on the axis most of their demand needs). This matches
the design's own Cost-based (not overflow-based) preference rule: the loop is doing exactly what
`outlineSweepBetter`-style arbitration is defined to optimize (`score.value`), and correctly falls
back to the untouched baseline whenever nothing legal beats it.

Per coordinator decision, the case1 test (`testRespaceFixedPointCase1BStarSeed1NeverRegresses`,
`test/regression_p3_20260707.cpp`) now asserts non-regression against the design contract
(`open_routes==0`, `value <= baseline + kEps`, legality, determinism) with an inline comment citing
this measurement, instead of a strict overflow assertion.

## Synthetic two-axis-slack fixture (the real RED->GREEN proof)

`testRespaceFixedPointSyntheticTwoAxisSlackReducesOverflow` hand-builds a 4-block problem (Left/
Right side by side with a narrow 10-unit gap to T above/Bo below, generous MAX) where a heavy
bundle's horizontal crossing overflows the gap's height capacity, and Bo/T are NOT axis-sandwiched
(they bound the channel via a different axis than Left/Right, so the V-pass can move T directly).
Measured: **baseline `channel_overflow = 125` -> after `respaceFixedPoint`, `channel_overflow = 0`**,
`improved == true`, `open_routes == 0`, `score.value` drops from `1.50364e+08` to `2.5366e+07`,
legal (`validateSolution().ok`), and deterministic across two independent calls.

## Concerns

- The axis-locked-layout limitation is real and recurring (not just case1) -- worth the phase-level
  documentation the coordinator flagged (T7 should note it; the strategic fix is P2's cost-driven
  routing steering flow off the locked axis, not more spacing here). No design change made in this
  task per direction.
- No changes to `search.cpp`/hooks in this task -- `respaceFixedPoint` is not yet wired into the
  elite pass or finalize (that's P3 Task 5/6).

Report path: `.superpowers/sdd/p3-task-4-report.md`.
