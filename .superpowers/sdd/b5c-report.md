# B5c Report — Move-portfolio upgrade, landed as registry variants

Status: **DONE.** Implemented per `.superpowers/sdd/b5-design.md` §4 (the shared insert-next-to-
partner primitive, the SP dual-permutation quadrant-reinsertion table, the B*-tree detach/reattach
spec, the three moves — wire-median / abutment-targeted / greedy-reposition — and the fixed
proposal probabilities with geometric-moves mass 0.45), flag-gated behind `SearchOptions.
move_portfolio_v2` (default `false`, byte-identical), landed as **two new legs** appended to the
`SearchVariant` registry (now **8 legs**), extending B5a's own flag-gated-variant lesson through
B5b to B5c. All 5 default-flag cases pass their per-case pins under the aligned metric
(`official_expected = checker_cost + 3.0·mismatch = score.value`, per the b5a/b5b coordinator
ruling) — 3 hold their exact pre-B5c floor, 2 (case1, case2) improve substantially via the new
moves, **zero regressions**. Golden-`checker_cost` (ft0) view reported alongside, also improved on
every case. 17/17 suites green, warning-clean, runtime under budget.

---

## 1. What changed

- **`src/placement_engine.hpp:40-111`** — `makePlacementEngine`'s new optional second argument
  (`bool move_portfolio_v2 = false`, default preserves every existing call site unchanged), plus the
  §4.1 shared-primitive declarations: `wireMedianTarget`, `netWeightedBundle`, `worstBlock`, the
  `AdjacentSide` enum, `reinsertSPAdjacent`, `reinsertBStarChild` — all free functions (mirrors
  search.hpp's `rememberBounded`/`reservoirSample`/`buildEliteFullSet` precedent: hoisted out of the
  engine classes specifically so they are unit-testable in isolation).
- **`src/placement_engine.cpp`** — the bulk of the change:
  - `:493-591` — local (anonymous-namespace) B5c helpers: `congestionShare` (mirrors eval.cpp's
    `routeAwareProxyPenalty` congestion term only), `portAllowsLocal` (mirrors routing.cpp's
    `portAllows` verbatim), `adjacentEdgesFor` (the 4-side → edge-pair table), `pickAbutmentPair`
    (design §4.4 steps 1-2), `wireMedianPartnerAndSide` (shared by wire-median/greedy-reposition),
    `kAbutmentSideOrder` (the fixed `{Left,Right,Below,Above}` try-order).
  - `:592-806` (`SequencePairEngine`) — constructor taking `move_portfolio_v2` (`:594`), `mutate`
    (`:626-668`) gated `if (!move_portfolio_v2_) { <verbatim pre-B5c body>; return; }` else the new
    §4.5 proposal table, three new private move methods (`mutateWireMedian`, `mutateGreedyReposition`,
    `mutateAbutment`) plus `fallbackSwap`.
  - `:808-1087` (`BStarEngine`) — same shape: constructor (`:810`), `mutate` (`:852-894`) gated
    identically, three new private move methods using `reinsertBStarChild`, plus `fallbackSwap`.
  - `:1088-1094` — `makePlacementEngine` forwards the flag to both concrete constructors.
  - `:1109-1249` — the 5 header-declared primitives' definitions, in the OUTER `namespace efgr`
    (not the anonymous namespace) so they satisfy the header's external-linkage declarations, placed
    after the anonymous namespace closes — the exact placement convention this file already used for
    `shrinkWrapEstimate`/`shrinkWrapOutline`/`anchorAtOutline`, which likewise call earlier
    anonymous-namespace helpers.
- **`src/search.hpp`** — `SearchOptions.move_portfolio_v2` (`:66`, with a long doc comment recording
  the threading decision and its caveat), `SearchResult.move_portfolio_v2` (`:128`, telemetry, same
  precedent as `abutment_credit`), `SearchVariant.move_portfolio_v2` (`:309`), `defaultVariants()`/
  `runPricingPortfolio` doc comments updated for the 8-leg contract.
- **`src/search.cpp`** — `defaultVariants()` (`:970-986`, 8 legs: existing 6 gain an explicit
  `false` 4th field, then 2 new `SearchVariant{true,true,true,true}` /
  `SearchVariant{true,false,true,true}`), `runLeg` (`:1009-1022`, threads
  `opts.move_portfolio_v2 = variant.move_portfolio_v2` and passes it to `makePlacementEngine`),
  `runHybridPlacementSearch` (`:878-882`, constructs both sp/bstar engines with
  `options.move_portfolio_v2`), winner-stamping (`:1095`, `result.move_portfolio_v2 = winner.
  move_portfolio_v2`).
- **`src/main.cpp:135`** — stderr summary gains `moves=v1|v2` immediately after `credit=`.
- **`test/regression_b5_20260724.cpp`** — 10 new tests (§3 below) + the registry-shape test upgraded
  from 6 to 8 legs; wired into the existing `run_b5_regressions.sh` (no g++ line change — every
  source this needs was already linked). Gained `#include "../src/placement_engine.hpp"`.

No changes to `eval.cpp`, `eval.hpp`, `routing.cpp`, `routing.hpp`, `respace.cpp`, `cfg_score.cpp`,
`validate.cpp` — consistent with this task's own scope boundary (read-only for those files).

## 2. Implementation choices

### 2.1 Flag threading (task's own explicit decision point)

`mutate`'s virtual signature is fixed at `(problem, state, rng)` across the whole codebase — every
override, every call site (`search.cpp`'s SA loop and `finalizeFeedthrough`), and every
test-defined `PlacementEngine` subclass (e.g. `test/regression_20260618.cpp`'s `GappedEngine`).
Adding a 4th parameter would touch the abstract interface and all of those sites for the least
value. Instead the flag is baked into the **constructed engine instance**:
`makePlacementEngine(name, move_portfolio_v2 = false)` forwards to `SequencePairEngine`/
`BStarEngine`'s own constructor argument, stored as a private member `mutate()` reads directly. This
means `runPlacementSearch`/`finalizeFeedthrough` — which only ever call `engine.mutate(...)` on an
already-built `engine` reference — need **zero changes**; only the two concrete engine classes, the
factory function, and `runLeg`/`runHybridPlacementSearch` (the sites that *construct* engines) are
touched. `SearchOptions.move_portfolio_v2` is the registry's source of truth; `runLeg` is the one
production call site that keeps it and the engine's own flag in sync.

**Documented caveat** (in `SearchOptions.move_portfolio_v2`'s own comment): setting the
`SearchOptions` field alone has no effect on an engine a caller constructed without also passing the
boolean to `makePlacementEngine` — a footgun for any *future* direct caller bypassing `runLeg`, not
exercised by any call site today (`runLeg`/`runHybridPlacementSearch` both do it correctly).

### 2.2 Both engines' reinsertion semantics

**SP (`reinsertSPAdjacent`, design §4.2):** verified the quadrant convention against the actual
decode constraint-graph builder (`placement_engine.cpp`'s `pos_rank`/`neg_rank` comparison) before
implementing: `pos_rank[a]<pos_rank[b] ∧ neg_rank[a]<neg_rank[b] ⇒ a left of b`; `pos<pos ∧ neg>neg ⇒
a below b`. Erasing `x` from `sequence_pos`/`sequence_neg` and reinserting immediately
before/after `p` per the 4-row table realizes exactly this relation — always a permutation of the
same elements, so `isValidState` holds by construction, no leaf/tree-shape restriction (unlike
B*-tree below).

**B*-tree (`reinsertBStarChild`, design §4.3) — the one real generalization beyond the design's
literal text.** §4.3's own algorithm box only describes **2** outcomes (attach `x` as `p`'s left
child ⇒ `x` right-of-`p`; as `p`'s right child ⇒ `x` above-`p`) because a B*-tree child slot only
ever expresses "right-of"/"above-of" its parent — there is no native tree-shape for "left-of" or
"below" a node from the CHILD's own perspective. To realize the abutment move's full
`{Left,Right,Below,Above}` try-order (§4.4 explicitly says "both engines"), **Left/Below swap which
of `{x,p}` is the "mover"**: `Right`/`Above` attach `x` as `p`'s child (literal §4.3); `Left`/`Below`
attach `p` as `x`'s child instead (`p` becomes right-of-`x`/above-`x`, which IS `x` left-of-`p`/
below-`p`) — same detach+reattach primitive, arguments swapped. This is a generalization I judged
necessary to give both engines equal access to all 4 named sides, not a design-doc requirement
verbatim.

**Leaf-only detach restriction (own judgment call, documented honestly as a real limitation).** The
design cites "the moveLeaf detach logic" for `reinsertBStarChild`'s detach step, and `moveLeaf` only
ever moves **leaves** (both children empty) — it has no logic for reparenting an internal node's own
children. Rather than invent an unspecified subtree-reparenting scheme, `reinsertBStarChild`
requires the resolved mover to currently be a leaf (and not the root), returning `false` (no state
mutation) otherwise. **Real consequence:** wire-median/greedy-reposition on `BStarEngine` fail (fall
through to swap) whenever the chosen block is currently an internal tree node — a strictly SP-only
freedom, since `reinsertSPAdjacent` has no analogous restriction. The 8-leg acceptance run (§5) shows
this is not crippling (`BStarEngine` still contributes to the hybrid leg's winning basin on cases 1/2
via its own SA trajectory), but it is a real, measured asymmetry between the two engines' move
portfolios, not a cosmetic one.

**`strict` vs best-effort fallback.** The abutment move already verified port-edge legality for one
*specific* side before calling `reinsertBStarChild`; if the exact child slot needed to realize that
side is occupied, silently falling back to "the other slot" (a different, unverified adjacency) would
defeat the whole point of the move. So `strict=true` there means "exact slot or fail, try the next
side" — never silently swaps in an unverified relation. Wire-median/greedy-reposition have no
specific-side legality contract, so they pass `strict=false`: exact slot, else the anchor's other
slot, else (mirroring `moveLeaf`'s own uniformly-random target-selection, reused rather than
reinvented) a uniformly-random node with a free slot — `old_parent`'s just-vacated slot is always in
that set, so this path never fails given a valid leaf mover.

### 2.3 Two honest scope-boundary duplications (eval.cpp / routing.cpp untouched)

- **`worstBlock`'s badness metric** (design: "`α·Σnets·manhattan` + X's `routeAwareProxyPenalty`
  share") — `routeAwareProxyPenalty` has **internal linkage** in `eval.cpp`, which is out of this
  task's scope to touch. `worstBlock` replicates only the **congestion/overflow** term locally
  (`congestionShare`, ~6 lines, using the public `kBetaLin`) and **omits** the smaller FT/soft-block-
  bbox share of `routeAwareProxyPenalty`. Documented at both the header declaration and the helper
  itself.
- **Port-edge legality** (`portAllowsLocal`) mirrors `routing.cpp`'s internally-linked `portAllows`
  verbatim (5 lines) for the same reason — `routing.cpp` is out of scope.

Both are small, self-contained, unlikely to drift silently (each is a handful of lines whose contract
is pinned by this task's own unit tests), and are the direct consequence of the task's own "do NOT
touch respace/routing/eval beyond reading" boundary — not an oversight.

### 2.4 Smaller judgment calls (all documented in code, restated here for visibility)

- **Even-count weighted-median tie rule** (`wireMedianTarget`): the design's §4.1 formula does not
  pin an exact even-count convention. Implemented the standard "lower weighted median" (smallest
  coordinate at which cumulative weight first reaches half the total) — unit-tested on a 4-point
  equal-weight fixture (§3(b)).
- **Quadrant → single side collapse**: a `wireMedian` target implies an independent (x,y) direction,
  but `AdjacentSide` only expresses one of 4. Collapsed via the **dominant axis** (compare `|dx|` vs
  `|dy|`) — the same convention `eval.cpp`'s own `routeAwareProxyPenalty` already uses for its
  congestion term, so this is consistent with existing code, not a new convention.
- **X-selection for wire-median**: design lists "random movable (or the net-weighted pick)" — took
  the primary (uniform random) option, not the parenthetical alternative.
- **Abutment X/P resolution when neither endpoint is an edge block**: `x=bundle.source`,
  `p=bundle.target` (arbitrary but deterministic) — the design only pins the edge-block case
  explicitly.

## 3. Unit tests (design §7 B5c + this task's item 4)

10 new tests, all passing, appended to `test/regression_b5_20260724.cpp`'s `main()`:

- **(a) flag-off byte-identity** — `testSPFlagOffByteIdenticalToReimplementedReference` /
  `testBStarFlagOffByteIdenticalToReimplementedReference`: an **independently reimplemented copy**
  (not a call into the engine) of each engine's pre-B5c `mutate` dispatch — including a verbatim copy
  of `BStarEngine::moveLeaf`, since that private function is exercised by the flag-off branch too —
  run 1000 times against the SAME seed as the real engine on a 5-block all-macro fixture (soft-
  aspect/edge-choice moves are guaranteed no-ops in both, given no soft/edge blocks, isolating the
  structural-swap/moveLeaf dispatch); asserts every intermediate state field matches every iteration.
- **(b) `wireMedianTarget`** — `testWireMedianTargetEvenCountTieUsesLowerMedian` (4 equal-weight
  partners at x=0/10/20/30 → target x=10, the lower-median convention),
  `testWireMedianTargetWeightsByNetCount` (heavy vs light partner → median sits at the heavy one, not
  the midpoint), `testWireMedianTargetNoBundlesReturnsOwnCenter`.
- **(c) abutment PORT EDGE legality** — `testAbutmentSideRealizesPortLegalTouch("sp"|"bstar")`: a
  2-block fixture where `X.port_edge=3`, `P.port_edge=1` makes `Left` the *only* legal side of the 4;
  independently reimplements the port-legality try-order (since `portAllowsLocal` is private),
  confirms exactly one side is legal and it is `Left`, then calls `reinsertSPAdjacent`/
  `reinsertBStarChild` for that side directly and asserts the decoded geometry actually touches on
  the intended edge pair.
- **(d) greedy-reposition's `worstBlock` metric** — `testWorstBlockPicksHighestBadnessBlock`: a
  4-block fixture (one heavy-and-far, one light-and-near, one bundle-less, one edge-type shared
  anchor) with a **hand-verified ~100,000x badness margin** picks the heavy-and-far block. (A first
  draft of this fixture made the shared anchor block type `Macro` too, so it competed as its own
  candidate and legitimately won by accumulating badness from *both* bundles — caught before it could
  mask a real result; fixed by making the anchor an `Edge` block, excluding it from `worstBlock`'s
  movable-only candidate set. Not a `worstBlock` bug — a test-fixture design error, logged honestly.)
- **(e) infeasible-fallback contract** — `testFlagOnMutateAlwaysValidFuzz("sp"|"bstar")`: 1000 random
  flag-on mutations on the real case0 problem; asserts `isValidState` holds and `decode` never
  crashes/produces a rect-count mismatch at every single step.
- Registry shape: `testDefaultVariantsRegistryHasEightLegsInOrder` (renamed from the 6-leg version,
  extended to check all 8 entries' 4 fields each).

## 4. Correctness verification (flag-off byte-identity)

**Structural argument:** the flag-off branch in both engines' `mutate` is the pre-existing body,
verbatim, inside `if (!move_portfolio_v2_) { ...; return; }` — not a rewrite. `move_portfolio_v2_`
defaults `false` on both constructors, and `makePlacementEngine`'s new second argument defaults
`false`, so every pre-existing call site (including all 6 pre-B5c `SearchVariant` legs, which now
carry an explicit `false` 4th field) is byte-unaffected.

**Unit-level (§3a):** an independently reimplemented reference (not calling the engine) matches the
real engine's state sequence exactly across 1000 mutations, for both engines.

**Empirical, end-to-end (the acceptance run, §5):** 3 of 5 default cases (case0, case3, case4) show
`0.0000%` delta against their b5b-era pins to the reported precision — i.e. the SAME pre-existing
legs still win those cases and reproduce the SAME `score.value`/`checker_cost`, exactly as they did
before this task touched anything. This is the strongest possible confirmation available short of a
literal git-stash/rebuild md5 diff (which B5a/B5a.1/B5a.2/B6 used when the *entire* registry was
byte-identical pre/post; here 2 of 6 pre-existing legs are expected to still WIN different cases than
before is not the claim — the claim is that THEIR OWN computed values are unperturbed, which the
exact-match deltas confirm).

## 5. Acceptance — per-case, both metrics (item 5)

| case | aligned pin | aligned measured | Δ% (aligned, governing) | ft0 pin | ft0 measured | Δ% (ft0) | winning leg (pricing, admission, credit, moves) |
|---|---:|---:|---:|---:|---:|---:|---|
| 0 | 4,653,040.63 | 4,653,040.63 | **0.0000%** | 4,624,846.00 | 4,624,846.00 | 0.0000% | official, v2, on, **v1** (unchanged floor) |
| 1 | 8,881,672.86 | **8,652,032.63** | **−2.5856%** | 8,564,588.73 | **8,437,960.88** | −1.4785% | official, v2, on, **v2** (NEW leg wins) |
| 2 | 57,334,883.75 | **51,351,665.57** | **−10.4356%** | 56,007,181.85 | **50,069,327.60** | −10.6020% | official, v2, on, **v2** (NEW leg wins) |
| 3 | 38,049,010.33 | 38,049,010.33 | **0.0000%** | 36,615,637.24 | 36,615,637.24 | −0.0000% | legacy, v1, off, **v1** (unchanged floor) |
| 4 | 68,830,618.87 | 68,830,618.87 | **0.0000%** | 68,469,987.82 | 68,469,987.82 | 0.0000% | official, v1, on, **v1** (unchanged floor) |
| **Σ** | **177,749,226.44** | **171,536,368.03** | **−3.4953%** | **174,282,241.64** | **168,217,759.54** | **−3.4797%** | |

**Gate verdict: PASSES, 5/5, on BOTH metrics** — every case is `≤` its prior pin under the governing
(aligned) metric, and (unlike B5b) the ft0 view shows **no disclosed regression at all** this time:
every case improves or holds exactly on ft0 too. `open_routes=0`, `validation=ok` on all 5 cases,
both metrics. The two new legs (leg 6 = official/v2-admission/credit-on/moves-on, leg 7 = official/
v1-admission/credit-on/moves-on) win outright on case1 and case2 — real, substantial improvement,
not a floor-holding no-op — while case0/case3/case4 keep their exact pre-B5c winning leg (all reading
`moves=v1`), confirming the new legs simply don't help there and the fold correctly declines them.

## 6. τ-diagnostics for the new legs (item 5 — measurement, not a gate)

Isolated the 2 new legs directly (bypassing the 8-leg fold) via a throwaway diagnostic harness
(not committed — same pattern as `tools/rescore.cpp`, calling `runHybridPlacementSearch` on a
`Problem`/`SearchOptions` pair configured per-leg and reading `fidelity_pairs`/`elite_pairs`
directly), on all 5 cases:

| case | leg | tau | pool_tau | exact_best_cheap_rank/K | isolated score.value |
|---|---|---:|---:|---|---:|
| 0 | leg6 (v2-admission) | −0.156 | −0.310 | 32/48 | 4,835,581.00 |
| 0 | leg7 (v1-admission) | −0.156 | 0.167 | 9/16 | 5,248,719.62 |
| 1 | leg6 (v2-admission, **wins the fold**) | 0.556 | 0.390 | 21/48 | 8,652,032.63 |
| 1 | leg7 (v1-admission) | 0.556 | 0.400 | 2/16 | 8,652,032.63 |
| 2 | leg6 (v2-admission, **wins the fold**) | 0.378 | −0.121 | 21/48 | 51,351,665.57 |
| 2 | leg7 (v1-admission) | 0.378 | 0.417 | 2/16 | 51,696,277.02 |
| 3 | leg6 (v2-admission) | 0.535 | 0.199 | 3/48 | 44,817,974.90 |
| 3 | leg7 (v1-admission) | 0.156 | −0.167 | 12/16 | 47,391,692.29 |
| 4 | leg6 (v2-admission) | 0.333 | −0.046 | 46/48 | 75,889,795.73 |
| 4 | leg7 (v1-admission) | 0.156 | 0.133 | 6/16 | 70,161,163.40 |

**Reading:** every isolated-leg `score.value` above is cross-checked against the acceptance table
(§5) — case1/case2's leg6 rows match the fold winner's reported value to 2 decimal places, confirming
the isolation harness is measuring the same thing the real fold picks. **Interesting secondary
finding, same phenomenon B5b's own §7 noted for case2/case3:** case1's leg6 and leg7 converge to the
**identical** `score.value` (8,652,032.630828 both) — admission changes which candidates receive the
expensive elite-pass treatment, not the SA loop itself (which the move portfolio *does* affect
equally under both admission schemes), so both admission variants land on the same final basin there.
Case3/case4's isolated legs both score far worse than the actual fold winner (an unaffected
pre-existing leg) — consistent with the fold correctly declining the new legs on those cases. No
uniform pattern in tau/pool_tau across cases (case0/case4 show negative pool_tau for leg6, case2/
case3 show a sign flip between leg6 and leg7) — reported as measured, not oversold.

## 7. Runtime (item 6)

5-case default (8-leg) total: **272.02s**, vs 206.24s for the pre-B5c 6-leg registry (b5b-report.md
§8) — **+31.9%**, closely matching this task's own expectation ("current 6-leg 206s; expect ~+33%").
**Under the 300s bound**, with 27.98s (9.3%) of margin.

**Watch item carried forward from b5b-report.md §8 (now more pressing):** that report already
flagged the registry's margin as "thin enough that ordinary run-to-run variance could plausibly tip a
future measurement over budget." The registry has now grown 6→8 legs and the margin, while still
comfortable in absolute seconds, is proportionally similar (was 1.8% at 210s bound / 6 legs, now
9.3% at 300s bound / 8 legs — the ABSOLUTE margin grew because this task's own bound grew
proportionally with leg count, but B2's own portfolio dimension will add at least one more axis to
this SAME registry). Restating the prior report's own recommendation: B2's design should budget for
leg-pruning (or a smarter per-leg early-exit) rather than assuming unlimited headroom, since this is
now the THIRD task in a row to grow the registry without shrinking it anywhere.

## 8. Suite battery (item 7 — all 17 + new tests)

Ran in small foreground chunks (3-4 suites per command) rather than one long background job, per
this task's own operational constraint:

```
PASS test/run_case00.sh
PASS test/run_placement_engine_regressions.sh
PASS test/run_20260618_regressions.sh
PASS test/run_20260608_regressions.sh
PASS test/run_cfg_score_regressions.sh
PASS test/run_integer_output_regressions.sh
PASS test/run_testdata_regressions.sh
PASS test/run_p0_regressions.sh
PASS test/run_p1_regressions.sh
PASS test/run_p2_regressions.sh
PASS test/run_p3_regressions.sh
PASS test/run_b6_regressions.sh
PASS test/run_b1_regressions.sh
PASS test/run_b3_regressions.sh
PASS test/run_b4_regressions.sh
PASS test/run_b5_regressions.sh (22 tests: 12 pre-existing B5a/B5b + 10 new B5c)
PASS test/run_selfconsistency_regressions.sh (292s; solver == golden re-scorer to <=1e-6 rel on all
                                               5 cases, 0 open routes)

=== 17/17 passed ===
```

Build warning-clean (`-Wall -Wextra -pedantic`, `make clean && make`, zero warnings/errors) —
including two `-Wmaybe-uninitialized` warnings caught and fixed mid-task (both `mutateAbutment`
methods' `edge_x`/`edge_p` locals, zero-initialized defensively; the compiler could not statically
prove `adjacentEdgesFor`'s switch is exhaustive across the loop's inlined call sites).

## 9. Files changed

- `src/placement_engine.hpp` — `makePlacementEngine`'s new optional arg (`:50`), 5 new primitive
  declarations + `AdjacentSide` enum (`:60-111`).
- `src/placement_engine.cpp` — local B5c helpers (`:493-591`), both engines' constructors/`mutate`/
  new private move methods (`:592-1087`), `makePlacementEngine` (`:1088-1094`), the 5 outer-namespace
  primitive definitions (`:1109-1249`).
- `src/search.hpp` — `SearchOptions.move_portfolio_v2` (`:66`), `SearchResult.move_portfolio_v2`
  (`:128`), `SearchVariant.move_portfolio_v2` (`:309`), doc comments for the 8-leg registry.
- `src/search.cpp` — `defaultVariants()` (`:970-986`, 8 legs), `runLeg`/`runHybridPlacementSearch`
  (`:878-882`, `:1009-1022`), winner-stamping (`:1095`).
- `src/main.cpp` — stderr gains `moves=v1|v2` (`:135`).
- `test/regression_b5_20260724.cpp` — 10 new tests + registry-shape test upgraded to 8 legs; gained
  `#include "../src/placement_engine.hpp"`.
- `test/run_b5_regressions.sh` — unchanged (no new source dependency).
- `.superpowers/sdd/b5c-report.md` — this file.
- `.superpowers/sdd/progress.md` — ledger line appended.

No changes to `src/eval.cpp`, `src/eval.hpp`, `src/routing.cpp`, `src/routing.hpp`,
`src/respace.cpp`, `src/cfg_score.cpp`, `src/validate.cpp` — read-only per this task's own scope.

## 10. Deviations / honest findings

- **B*-tree's 4-way side realization required a generalization beyond §4.3's literal 2-outcome
  algorithm box** (mover/anchor swap for Left/Below) — necessary to give the abutment move equal
  access to all 4 named sides on both engines, as §4.4 explicitly requires ("both engines"). Judged
  the lowest-risk generalization available (same primitive, swapped arguments) rather than
  inventing a new mechanism.
- **B*-tree's detach is leaf-only** (mirrors `moveLeaf`'s own scope, per the design's own citation of
  it) — wire-median/greedy-reposition silently degrade to swap whenever the chosen block is
  currently an internal node. A real, measured asymmetry vs SP (which has no such restriction), not
  hidden: see §2.2.
- **`worstBlock` and the abutment move's port-legality check each locally replicate a small piece of
  `eval.cpp`/`routing.cpp` logic** (`congestionShare`, `portAllowsLocal`) because the real functions
  have internal linkage and those files are out of this task's scope to touch. Each is a handful of
  lines, pinned by this task's own unit tests, and `worstBlock` explicitly omits the smaller FT/
  soft-block-bbox share of `routeAwareProxyPenalty` — documented, not silently approximated.
- **Even-count weighted-median tie rule, quadrant→side collapse, and wire-median's X-selection rule**
  are judgment calls filling small gaps the design doc leaves unpinned (§2.4) — each documented in
  code and cross-checked against existing codebase conventions (e.g. the dominant-axis rule already
  used by `eval.cpp`'s own congestion term) rather than invented from scratch.
- **A test-fixture bug** (the `worstBlock` fixture's shared anchor block initially typed `Macro`
  instead of `Edge`, letting it compete as its own candidate and legitimately win) was caught and
  fixed before it could mask a real result — a test-authoring mistake, not a `worstBlock` defect
  (§3d).
- **Runtime margin, while comfortable in absolute terms (9.3%), is proportionally similar to B5b's
  own flagged concern** and the registry has now grown for a third consecutive task without
  shrinking — restated as a B2 watch item, not acted on here (§7).
- **Verification had to be re-run from scratch twice** due to unrelated session/infrastructure
  stalls interrupting background suite-battery processes; final numbers in this report come from
  synchronous, small-chunk foreground runs against the exact working tree being committed (verified
  via `git status`/`git rev-parse HEAD` immediately before the final battery), not stitched together
  from partial earlier attempts.
- No architectural changes beyond this task's own spec: `PlacementEngine`'s abstract interface,
  `runPlacementSearch`, `finalizeFeedthrough`, `paydownBetter`, `buildEliteFullSet`, `outlineSweep`,
  and the exact tier are all untouched. Only the two concrete engine classes' `mutate` bodies (each
  behind its own flag-gated branch), the primitive/factory declarations, and the registry's leg
  list/count changed.

## 11. Commit

One commit, staging exactly: `src/placement_engine.hpp`, `src/placement_engine.cpp`,
`src/search.hpp`, `src/search.cpp`, `src/main.cpp`, `test/regression_b5_20260724.cpp`,
`.superpowers/sdd/b5c-report.md`, `.superpowers/sdd/progress.md`; message:
`feat(engine): wire-median/abutment/greedy-reposition moves as registry variant (B5c)` +
`Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>`.
