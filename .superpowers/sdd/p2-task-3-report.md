# P2 Task 3 report: adaptive bottleneck chunking

## What was implemented

Two commits on `fable5`, HEAD before this task was `ffceae6`:

1. `c8bc980` `docs(routing): correct T2 review comment minors` (rider, comment-only)
2. `a41d988` `feat: adaptive bottleneck chunking` (the task)

### Rider (comment-only fixes from the T2 review)

- `src/routing.cpp` (was lines ~184-189, now ~184-195 after the edit added lines): the inline comment
  at the channel-hop pricing site claimed "this graph has no single-hop L case" as if no corner case
  existed. Corrected to state: pricing uses the entry-edge axis as an admissible one-sided proxy; a
  path that enters and exits a channel on orthogonal edges accrues BOTH axes at commit (the dual `if`s
  in the accrual loop, `dx>kEps`/`dy>kEps` at what are now lines 277-278), while only the entry axis
  was priced during Dijkstra search — deliberate, deferred per design §3.1/R2, to be measured at
  T6/T7.
- `src/routing.hpp` (line ~10): removed the stale `respace.cpp` mention from the list of
  `routeProblem` callers — verified via grep that only `eval.cpp:139` calls `routeProblem`;
  `respace.cpp` does not.

### Feature: adaptive bottleneck chunking (design p2-design.md §5.1-5.4)

Replaced the fixed `split_count = clamp(ceil(nets/1000), 1, 8)` bundle split with the design's greedy
split-at-bottleneck loop from §5.2, **for `cost_driven=true` only**. `cost_driven=false` keeps the
exact legacy fixed split, verified byte-identical by a new regression
(`testFixedSplitUnchangedForCostDrivenFalse`).

**Files changed:**
- `src/route_cost.hpp` (+22 lines): added `kMaxChunks=16`, `kChunkCeil=1000`,
  `kMinChunkFor(bundle_nets) = max(1.0, bundle_nets/64.0)`, placed beside the existing
  `kOverflowAreaWeight`/`kFtMismatchWeight`.
- `src/routing.cpp` (+143/-11 net): the substantive change.
- `test/regression_p2_20260707.cpp` (+193 lines): new fixtures and tests (below).

### Design judgment call resolved before implementing (flagged, not guessed)

The design's §5.2 pseudocode says: find the min-cost path for a probe of `remaining` nets, compute
slack along that path, then "route chunk of `n` along `path`" where `n` can be smaller than
`remaining`. I identified a real ambiguity: the existing `routeChunk` lambda prices its single
Dijkstra run AND commits/accrues in one call, both using the same `nets` argument. Naively calling it
twice (once at `remaining` to find `path`/`slack`, once at the smaller `n` to "commit") risks running
an independent second Dijkstra that is not guaranteed to reproduce the same node_path — I dispatched a
background research agent to check this rigorously (it is not obvious from inspection alone) and ran
my own numeric counterexample search in parallel. Both converged on the same finding:

- `channelMarginalCost` has a real kink at the channel cap (`channelOverflowArea` is 0 below cap,
  linear above), and `softFtMarginalCost` has a genuine non-monotone-slope kink at each FT tier
  boundary (3000/6000/9000 nets, `ft.hpp`'s `ftTierRate`). Because different candidate paths can have
  different kink locations, the argmin-over-paths is not provably stable as the priced `nets` shrinks
  — a path that's cheapest at `remaining`-pricing is not provably still cheapest at a smaller `n`
  (worked counterexample in the agent's analysis, centered on two competing FT-through-different-
  tier-rate paths). Re-running Dijkstra at the smaller size is therefore an unverified assumption, not
  a safe optimization.
- The cheap, correct fix (which the agent independently identified and I had already spotted as the
  natural seam while re-reading the file) is to split `routeChunk` into a pure pathfinding phase and a
  pure commit phase, so the SAME `node_path` found by the probe is what gets committed at the smaller
  `n` — no second Dijkstra, no risk of path divergence.

This is why `routeChunk` (routing.cpp) is now composed of `findRoutePath` (Dijkstra only, no accrual)
+ `commitRoutePath` (guides/wirelength/RouteStep/accrual, parameterized on an already-found
`node_path` and a caller-chosen `nets` that may be smaller than what found the path) + a thin
`routeChunk` wrapper that composes both at the SAME size, reproducing the exact legacy behavior.

### Implementation summary (`src/routing.cpp`)

- `findRoutePath(bundle, nets)`: the old Dijkstra-only portion of `routeChunk` (lines ~139-238 in the
  new file), now returning a `Route` with only `node_path`/`open` set (no `route.nets`, since a probe
  may not become the committed size).
- `commitRoutePath(bundle, node_path, nets)`: the old post-Dijkstra portion (guides, wirelength,
  channel/soft-block usage accrual, `RouteStep`s, `result.total_wirelength +=`), now taking an
  already-found `node_path` and the actual chunk size to commit.
- `routeChunk(bundle, nets)`: thin wrapper, `commitRoutePath(bundle, findRoutePath(bundle,
  nets).node_path, nets)` — used verbatim by the legacy `cost_driven=false` loop, unchanged.
- `pathSlack(node_path)`: new helper implementing design §5.2 step 2 — minimum, over
  channel/soft-FT-block intermediates on the path, of remaining slack (`cap_dir - usage_dir` for
  channels, clamped to ≥0; `next_tier_threshold - acc` for soft blocks, unbounded/`+inf` once
  `acc >= 9000` since the top tier has no further boundary). Direction classification for channel
  slack reuses the exact entry-edge axis the Dijkstra pricing loop uses
  (`edgeFromTo(rects[u],rects[v])` in `{1,3}`=horizontal, `{2,4}`=vertical) — the same axis rule as
  pricing, not the post-hoc guiding-point dx/dy the commit accrual uses (per the task's explicit
  instruction not to invent a different axis rule).
- Bundle loop: `if (!cost_driven) { <exact old fixed-split code>; continue; }` then the new §5.2
  greedy `while (remaining > 0 && chunks_emitted < kMaxChunks)` loop for `cost_driven=true`: probe at
  `remaining`, compute slack, clamp `n = clamp(slack, min_chunk, remaining)` when `slack > 0`, else
  `n = min(remaining, kChunkCeil)` when the path is already saturated (accepting graded overflow, no
  cheaper split exists — matches design §5.2 step 3 exactly), commit, accumulate `remaining -= n`.
- **Correctness fix beyond the literal pseudocode**: the design's `kMinChunk` floor
  (`bundle.nets/64`) can force up to 64 chunks in the worst case if the bottleneck keeps clamping to
  the floor, which would exceed `kMaxChunks=16` and leave nets unrouted — violating the brief's "Σ
  chunk nets == bundle nets" hard invariant. I added: the last permitted iteration
  (`chunks_emitted == kMaxChunks - 1`) always takes the entire `remaining`, ignoring
  slack/`kMinChunk` clamping on that one slot. This makes the loop provably terminate with
  `remaining == 0` within `kMaxChunks` iterations in all cases, while never emitting more than
  `kMaxChunks` chunks. Verified by `testAdaptiveChunkingRespectsMaxChunksWithNoAlternative`
  (50,000 nets, no FT alternative, saturates a tiny channel immediately) → exactly 16 chunks, all
  50,000 nets conserved.
- Open-route handling inside the greedy loop: if `findRoutePath` finds no path at all (topology-only
  unreachability, independent of `nets` since no `nets`-based check ever removes an edge from
  consideration — verified by inspection), the WHOLE `remaining` is emitted as one open route and the
  loop breaks, matching the legacy "never re-probe an unreachable bundle" behavior (path existence
  cannot change by re-probing at a smaller `nets`).

## TDD evidence

### RED

Command: `bash test/run_p2_regressions.sh`

First attempt at the fixture (before tightening the chunk-count assertion) surprisingly PASSED under
the still-fixed-split code, because a per-chunk cost-driven Dijkstra could coincidentally reproduce
the aggregate (1000, 2000)-shaped totals even while still literally emitting 3 fixed chunks of 1000
each (two of which happened to route through the FT alternative independently). I caught this by
inspecting the passing output (`chunks=3` printed, not `2`) and tightened the test to assert
`r.routes.size() == 2` exactly — the only assertion that structurally distinguishes "greedy
bottleneck-split implemented" from "fixed split whose independent per-chunk routing happens to total
the same way." After that fix, the RED run failed as expected:

```
p2_regression_bin: test/regression_p2_20260707.cpp:497: void {anonymous}::testAdaptiveChunkingSplitsAtBottleneck(): Assertion `r.routes.size() == 2' failed.
test/run_p2_regressions.sh: line 13: 135733 Aborted                 (core dumped) ./test/p2_regression_bin
```

This is the correct RED: legacy fixed split emits exactly 3 routes for a 3000-net bundle regardless
of `cost_driven`; only the not-yet-implemented adaptive loop can emit exactly 2 (1000 filling the
channel's slack to capacity, 2000 diverting through FT once saturated).

### GREEN

Command: `bash test/run_p2_regressions.sh` (after implementing `findRoutePath`/`commitRoutePath`/
`pathSlack`/the greedy loop):

```
testVerticalOverflowIsAreaDenominatedByChannelHeight ok
testHorizontalOverflowIsAreaDenominatedByChannelWidth ok
testOverflowAreaComparableToFtMismatchMagnitude ok
testChannelMarginalCostIsZeroWhenSlack ok
testChannelMarginalCostSaturatedVertical ok
testChannelMarginalCostSaturatedHorizontal ok
testChannelMarginalCostStraddlesCap ok
testSoftFtMarginalCostPricesTierJump ok
testSoftFtMarginalCostFreeWhenPlacedAreaGenerous ok
testWireCostTrivial ok
testCostDrivenPrefersFtOverSaturatedChannel ok (legacy overflow=39040, cost_driven overflow=0)
testCostDrivenZeroWlDirectAbutUnchanged ok
testCostDrivenDeterministic ok
testFixedSplitUnchangedForCostDrivenFalse ok
testAdaptiveChunkingSplitsAtBottleneck ok (chunks=2, diverted=2000)
testAdaptiveChunkingRespectsMaxChunksWithNoAlternative ok (chunks=16)
testAdaptiveChunkingDeterministic ok
P2 T0+T1+T2+T3 regressions passed
P2 regressions passed
```

### Full gate (after GREEN, against committed HEAD `a41d988`)

`make clean && make` — warning-clean (`-Wall -Wextra -pedantic`), no warnings emitted.

All 8 required suites, each `exit=0`:

```
=== run_case00 ===            exit=0   (open_routes=0 validation=ok, both engine runs)
=== run_placement_engine_regressions === exit=0   placement_engine_regression: ok
=== run_20260618_regressions ===         exit=0   regression_20260618: ok
=== run_20260608_regressions ===         exit=0   regression_20260608: ok  (open_routes=0 validation=ok)
=== run_p0_regressions ===               exit=0   P0 regressions passed
=== run_p1_regressions ===               exit=0   P1 regressions passed
=== run_p2_regressions ===               exit=0   P2 T0+T1+T2+T3 regressions passed
=== run_p3_regressions ===               exit=0   P3 T1+T2+T3+T4+T5+T6 regressions passed
```

## New test fixtures added

`test/regression_p2_20260707.cpp`, `BottleneckFixture` + 4 tests:

- `testFixedSplitUnchangedForCostDrivenFalse` — legacy byte-identity guard: 3000-net bundle,
  `cost_driven=false` must produce exactly `(1000,1000,1000)`.
- `testAdaptiveChunkingSplitsAtBottleneck` — the core RED→GREEN test: same bundle,
  `cost_driven=true` must produce exactly `(1000, 2000)` — channel slack filled exactly, remainder
  diverted through FT, channel usage == 1000 (no overflow), `soft_feedthrough_nets[S] == 2000`.
- `testAdaptiveChunkingRespectsMaxChunksWithNoAlternative` — 50,000-net bundle, no FT alternative
  (only 2 hard blocks + a tiny channel): must terminate in exactly 16 chunks (the forced-last-slot
  fix), total nets conserved.
- `testAdaptiveChunkingDeterministic` — routing the same fixture twice via `cost_driven=true` yields
  identical chunk counts, per-chunk nets, node_paths, `channel_overflow`, `feedthrough_mismatch`
  (GC6 purity/determinism, same invariant class as T2's `testCostDrivenDeterministic`).

Fixture geometry was derived numerically (not guessed): a vertical channel with `w=40` (`vcap=1000`
nets exactly) and span `h=120`, plus a soft block `S` with `ft_base_area=245000` placed at exactly
that area (zero starting slack). I verified by direct computation of `channelMarginalCost`/
`softFtMarginalCost` (matching the already-tested T1 formulas) that: (a) for a probe of the full
3000-net remainder from an empty state, the channel's marginal cost (9.6e9) is decisively less than
S's FT marginal cost (12.02e9), so the first probe picks the channel and correctly reads its slack as
the bottleneck (1000); (b) after committing 1000 nets to the channel (now saturated), a second probe
of the remaining 2000 nets flips — channel marginal cost for 2000 more nets on a saturated channel
(9.6e9) now exceeds FT's marginal cost for the same 2000 nets (7.98e9) — so the second chunk diverts
entirely through S. Both margins are comfortable (~20% and ~17% respectively), chosen deliberately
over a narrower-margin alternative to keep the test robust to floating-point noise.

## Self-review findings

1. **Completeness vs brief**: RED written first and confirmed failing for the correct structural
   reason (chunk count, not a coincidental total) before implementing. Greedy loop implements §5.2
   faithfully: probe at `remaining`, slack from the SAME path (channel cap headroom or FT
   next-tier-threshold), clamp `(kMinChunk, remaining)` when `slack>0`, else
   `min(remaining, kChunkCeil)` when saturated. `kMaxChunks=16`, `kChunkCeil=1000` (documented as my
   resolution of the design's unpinned ambiguity, matching the legacy granule per the task's explicit
   instruction), `kMinChunkFor(nets)=max(1,nets/64)` all landed as named constants beside the existing
   T1 weights in `route_cost.hpp`. Total nets conserved (enforced structurally by the forced-last-slot
   fix, not just tested). One `Route`/PATH-line per chunk — `io.cpp` untouched, matches design §5.4.
2. **Legacy-path untouched**: verified three ways — (a) the `!cost_driven` branch is a byte-for-byte
   copy of the pre-T3 loop body (diffed and confirmed only whitespace/wrapping changed, no logic
   touched); (b) `testFixedSplitUnchangedForCostDrivenFalse` passes; (c) every pre-existing T0/T1/T2
   test in the file still passes unmodified, including `testCostDrivenPrefersFtOverSaturatedChannel`
   and `testCostDrivenZeroWlDirectAbutUnchanged`, which exercise `cost_driven=true`'s single-chunk
   path (bundles small enough that the greedy loop's first chunk consumes the whole bundle, so their
   pre-T3 expected numbers are unaffected).
3. **Determinism**: grepped `src/routing.cpp` and `src/route_cost.hpp` for `static`/`rand`/`mt19937`/
   `chrono`/`thread` — no matches (only unrelated `static_cast`). All new state
   (`min_chunk`/`remaining`/`chunks_emitted` in the bundle loop, `findRoutePath`/`commitRoutePath`/
   `pathSlack` lambdas) is call-local, captured by reference into the single `routeProblem` stack
   frame, matching the pre-existing pattern. New explicit `testAdaptiveChunkingDeterministic` added
   beyond what the brief strictly required, since T4 (negotiation, next task) will build directly on
   this loop and determinism is a hard invariant worth pinning explicitly rather than only inheriting
   from T2's fixture.
4. **Tie-breaks stable**: the Dijkstra priority-queue comparator is unchanged (pre-existing,
   `(dist,node)` with `std::greater`, ties broken by ascending node index). No new tie-break surface
   was introduced by `pathSlack` (pure deterministic scan of an already-deterministic `node_path`) or
   by the clamp/`llround` arithmetic (no ties possible, pure numeric computation).

## Concerns

- **Judgment call flagged for visibility, not a blocker**: the find/commit split was not explicitly
  specified in the brief or design doc at the code-structure level (the design's pseudocode is
  abstract, `path = dijkstra(...)`, `route chunk of n along path`). I resolved the "is a second
  independent Dijkstra at the smaller size safe?" question via a background research agent plus my
  own independent numeric counterexample search before implementing, rather than guessing; both
  converged on "unsafe in general, restructure into find+commit." I'm confident in this call but
  flagging it since it's the one place I deviated from a literal reading of "call routeChunk with a
  probe size, then call it again with the clamped size" in favor of a structural refactor.
- **`kMinChunkFor` interpretation**: the design's `kMinChunk (e.g. max(1, nets/64))` is ambiguous about
  whether `nets` means the bundle's total or the shrinking `remaining` counter. I chose "bundle
  total, computed once per bundle" (matching the task prompt's explicit resolution: "use
  `max(1.0, nets/64.0)` per design §5.3"), reasoning that a floor proportional to a *shrinking*
  `remaining` would ironically shrink toward the pathological 1-net case the floor is meant to
  prevent. This is stated as a design decision in the code comment, not silently assumed.
- **Last-slot-forces-full-remainder is beyond the literal §5.2 pseudocode**: the pseudocode as written
  does not describe this rule; I added it because the literal pseudocode does not itself guarantee
  the "chunk count ≤ 16" and "Σ chunk nets == bundle nets" invariants can both hold simultaneously
  when `kMinChunk` repeatedly wins the clamp (up to 64 chunks needed in the worst case, exceeding
  `kMaxChunks=16`). I verified this is a real, reachable case (not a hypothetical) via
  `testAdaptiveChunkingRespectsMaxChunksWithNoAlternative`, which would otherwise have under-routed
  nets or violated the chunk cap. Flagging this because it's a correctness fix to the design's own
  pseudocode, not a literal transcription of it — worth a second look if reviewed against the design
  doc line-by-line.
- No other concerns. Build is warning-clean; all 8 required suites plus the new P2 fixtures pass
  against the committed HEAD.
