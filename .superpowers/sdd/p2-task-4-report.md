# P2 Task 4 report — negotiated rip-up/reroute rounds

Commit: `8887117 feat: negotiated rip-up reroute` (HEAD, branch `fable5`, parent `a41d988`).

## Salvage audit verdict: KEPT (no rewrite needed)

Read `git diff src/routing.cpp src/route_cost.hpp` (a41d988 baseline) in full and checked it
point-by-point against the 8 controller-resolved requirements in the dispatch and design §4.1-4.4:

1. **Round structure** — matched exactly. `for (int round = 0; round < kNegotiationRounds; ++round)`
   builds a fresh `RoutingResult round_result` per round (`round_result.channels = result.channels`
   reuses the single `extractChannels` call's rect geometry with usage counters reset via the copy;
   `round_result.soft_feedthrough_nets.assign(...)` resets to 0), routes bundles in a
   `std::stable_sort`-by-`nets`-descending order computed once (index tie-break, since `order` starts
   as `0..n-1`), reuses the existing T3 adaptive-chunking while-loop verbatim (just threaded through
   `RoutingResult& rr` instead of closing over the single outer `result`), and tracks `best` by
   `roundCost`. `cost_driven=false` is a separate, unmodified-shape early-return branch
   (`src/routing.cpp:417-434`) — confirmed the mechanical extraction into `computeOverflowAndMismatch`
   is a pure lambda-ization of the pre-existing inline code, not a behavior change.
2. **Constants** — `src/route_cost.hpp:99,104,110`: `kNegotiationRounds=3`, `kHistoryDecay=0.5`,
   `kHistoryBump=1.0`, each with the exact comment content the brief specified (tuning knob, measured
   at T7).
3. **History state** — `history_channel` (`std::vector<std::array<double,2>>`, indexed by channel id)
   and `history_ft` (`std::vector<double>`, indexed by block id) declared call-local at
   `src/routing.cpp:136-137`, never static/global. Decay at the start of round≥1
   (`src/routing.cpp:466-472`); post-round bump matches the design formula exactly
   (`src/routing.cpp:566-589`), including the per-direction axis split.
4. **History in Dijkstra weights** — channel hop: `cost += kOverflowAreaWeight * hist * nets` where
   `hist` is selected via the same `horizontal` bool the marginal-cost pricing just computed from
   `edgeFromTo` (`src/routing.cpp:239-249`). Soft-block hop:
   `cost += kFtMismatchWeight * history_ft[v] * nets` (`src/routing.cpp:212-216`). Both only under
   `cost_driven` (the `else` legacy-weight branch is untouched).
5. **Best-round selection** — `roundCost` returns `std::pair<int,double>(open_routes, alpha*WL +
   kOverflowAreaWeight*overflow + kFtMismatchWeight*mismatch)`; `std::pair::operator<` gives exact
   lexicographic strict-less; `if (!have_best || roundCost(round_result) < roundCost(best))` ties keep
   the earlier round (`src/routing.cpp:456-460,552-555`).
6. **Zero-bump early exit** — `any_bump` tracked through both the channel and soft-block bump loops;
   `if (!any_bump) break;` with a comment explaining the provably-identical rationale
   (`src/routing.cpp:557-591`).
7. **Purity/determinism** — no `static`/global/thread-local, no RNG; history containers are
   `std::vector` indexed by node id (not map), matching `soft_feedthrough_nets`'/`channels`' own
   indexing scheme. Verified empirically via `testNegotiatedRoutingDeterministic` (below).
8. **Round 0 == T3-exact when history is 0** — the added history terms are `kOverflowAreaWeight * 0.0
   * nets` / `kFtMismatchWeight * 0.0 * nets`, i.e. exactly 0 at round 0 (no decay step runs before
   round 0 either). The only behavioral difference from a bare T3 call is bundle order
   (nets-descending, itself a requirement, not a regression) — documented inline at
   `src/routing.cpp:442-445`.

No architectural or correctness problems found. Comments throughout are unusually thorough and
correctly cross-reference the design doc sections and prior task numbers (T0-T3), which made the
audit fast and high-confidence. **Verdict: kept as-is, zero code changes to the salvaged WIP.**
`route_cost.hpp`'s diff (the +21 lines) was also verified as an exact match to requirement 2 and
kept unchanged.

## Stray files

- `test/fixture_pathfinder_derive.cpp` (untracked, 102 lines): a hand-built two-bundle/three-channel
  "ladder" fixture the dead agent was using to hunt for a round-0-overflow scenario, built for
  `EFGR_ROUTE_TRACE`. I compiled and ran it (`g++ -DEFGR_ROUTE_TRACE ... && ./fix_derive`) — its
  specific geometry actually produced **zero overflow at round 0** (T3's own adaptive chunking split
  the two bundles cleanly across 3 channels without contention), so its numbers were not directly
  reusable, but the harness pattern (`EFGR_ROUTE_TRACE`, `extractChannels` dump) was the right
  approach and I reused it in a scratch derivation script to build the actual working fixture (see
  Test section). Deleted after mining the approach (`rm -f test/fixture_pathfinder_derive.cpp`).
- `downloads/` (untracked dir): 2 PDF papers (`1709.02877.pdf`, `2504.17949.pdf`, ~1.6MB total) —
  agent-scratch, unrelated to this task. Deleted (`rm -rf downloads`).
- `scratch_c2/` (untracked dir): 2 compiled binaries + 2 `.cpp` sources (`real_harness`,
  `trace_harness`, each with matching `.cpp`) — leftover ad-hoc debugging harnesses from an earlier
  (P2 T2/T3?) dead-agent session, unrelated to T4. Deleted (`rm -rf scratch_c2`).

None of these three were git-tracked; deletion is a plain `rm`, no git operation needed.

## Test added

`test/regression_p2_20260707.cpp`: two new tests, `testNegotiationClearsRoundZeroOverflow` and
`testNegotiatedRoutingDeterministic`, plus a `NegotiationFixture` builder.

**Fixture construction** (documented in-file): a "ladder with a wall" topology — `SRC_A` (bottom
rail) touches both a narrow shared channel (vcap=1000) and a wide alternate channel (vcap=3475, only
reachable via SRC_A's 1-unit sliver overlap and much farther from SRC_A's centroid so it's strictly
more expensive when both are slack); `SRC_B` touches *only* the narrow shared channel (a `CAP_TOP`
block seals off any shortcut gap-channel to `TGT`, and `TGT` is shrunk so it never touches `SRC_B`
directly); a `WALL` block physically separates the two channels so they aren't graph-adjacent.
Bundle A (900 nets, heavier, routed first) and bundle B (400 nets) both target `TGT`.

I did **not** hand-derive the exact numbers — I wrote a scratch harness
(`derive_t4.cpp`, compiled with `-DEFGR_ROUTE_TRACE` against the real `src/routing.cpp`, run from
`/tmp` under WSL) and iterated the geometry twice (first pass let `SRC_B` touch `TGT` directly for a
free 0-cost bypass, fixed by shrinking `TGT` and adding `CAP_TOP`) until the trace showed the intended
contention. Confirmed trace output:
- **Round 0** (all-zero history): A takes the cheaper narrow channel wholesale (900 nets, no overflow
  for A alone); B has no alternative, is forced through the same channel, usage becomes 1300 > 1000
  cap → `channel_overflow = ((1300-1000)/25)*80 = 960.0` um², `open_routes=0`.
- **Round 1**: narrow channel's history is bumped (`0.3` after round 0); A is re-priced with
  `kOverflowAreaWeight * 0.3 * 900 = 2.7e8`, dwarfing the alternate channel's small extra distance
  cost (alpha=0.001) — A's whole 900 nets divert to the alternate channel, freeing the narrow channel
  for B alone (400 < 1000 cap) → `channel_overflow = 0.0`. Zero bumps this round → early exit fires,
  round 2 never runs.
- Final `best` = round 1: `channel_overflow=0`, `feedthrough_mismatch=0`, `open_routes=0`.

These exact numbers were transcribed into the committed test's assertions and comments.

**Honesty check** (since the implementation pre-existed, strict RED-first was impossible): temporarily
edited `src/route_cost.hpp`'s `kNegotiationRounds` from `3` to `1`, rebuilt, and ran
`bash test/run_p2_regressions.sh`:

```
p2_regression_bin: test/regression_p2_20260707.cpp:670: void {anonymous}::testNegotiationClearsRoundZeroOverflow(): Assertion `r.channel_overflow == 0.0' failed.
test/run_p2_regressions.sh: line 13: 27377 Aborted                 (core dumped) ./test/p2_regression_bin
```

Confirmed RED — with only 1 round, `best` == round 0's 960.0 um² overflow, failing the assertion.
Reverted `kNegotiationRounds` back to `3` (verified via `git diff src/route_cost.hpp | grep
kNegotiationRounds` showing only the clean `+constexpr int kNegotiationRounds = 3;` addition line, no
stray edits), rebuilt, reran — GREEN (see below). This proves the new test genuinely exercises
multi-round negotiation, not just T3's adaptive chunking.

## Build + full suite results (all green, after honesty-check revert)

```
make clean && make        -> clean, zero warnings (-Wall -Wextra -pedantic)
run_case00.sh              -> case00 done, validation=ok x2
run_placement_engine_regressions.sh -> placement_engine_regression: ok
run_20260618_regressions.sh -> regression_20260618: ok
run_20260608_regressions.sh -> regression_20260608: ok, validation=ok
run_p0_regressions.sh      -> p0 regressions passed (8/8 tests)
run_p1_regressions.sh      -> p1 regressions passed (22/22 tests)
run_p2_regressions.sh      -> P2 T0+T1+T2+T3+T4 regressions passed (19/19 tests, incl. new T4 tests)
run_p3_regressions.sh      -> P3 T1+T2+T3+T4+T5+T6 regressions passed (18/18 tests)
```

## Files changed (commit `8887117`)

- `\\wsl.localhost\ubuntu-24.04\home\yyds\Early-Floorplanning-with-Global-Route\src\routing.cpp` — kept
  as salvaged (no edits); 339-line diff implementing the negotiation loop.
- `\\wsl.localhost\ubuntu-24.04\home\yyds\Early-Floorplanning-with-Global-Route\src\route_cost.hpp` —
  kept as salvaged (no edits); +21 lines of T4 constants.
- `\\wsl.localhost\ubuntu-24.04\home\yyds\Early-Floorplanning-with-Global-Route\test\regression_p2_20260707.cpp`
  — +133 lines: `NegotiationFixture`, `testNegotiationClearsRoundZeroOverflow`,
  `testNegotiatedRoutingDeterministic`, registered in `main()`.

`CLAUDE.md` was left untouched throughout (pre-existing dirty state from before this task, per
instructions — never staged, never committed).

## Self-review / concerns

- The salvaged implementation is unusually well-commented and internally cross-referenced; I found no
  correctness gaps against the 8 binding requirements and made zero source edits to the WIP.
- One minor, non-blocking observation: the outer `result` object's `soft_feedthrough_nets` is assigned
  once at the top of `routeProblem` but never read again once the `cost_driven=true` branch is
  reached (only `round_result.soft_feedthrough_nets` matters there) — harmless (a few dozen bytes of
  unused allocation), not a correctness issue, not touched since it's outside this task's scope.
- The `EFGR_ROUTE_TRACE`-gated debug printing left in `routing.cpp` (from the salvaged WIP) is
  compiled out by default (`#ifdef`) and was extremely useful for both auditing the WIP and deriving
  the test fixture; left in place as-is since it's part of the kept diff and doesn't affect the
  default build.
- No regressions, no open items. This closes P2 Task 4 per the brief and design §4.
