# P3 Task 2 Report — constraint-graph relaxToDemand

**Status:** Done. GREEN across all suites.

**Commit:** `91c6d7c` — `feat: longest-path respacing over core blocks`

## Files
- `src/respace.hpp` — `bool relaxToDemand(const Problem&, Placement&, const std::vector<StripDemand>&);` declaration + doc; channelDemands doc comment updated (carry-over b).
- `src/respace.cpp` — `relaxToDemand`, `relaxOneAxis`, `relaxAxis`, `mergeDemandsMax`, `axisSpan` (internal helpers); `channelBounds` tie-break fixed (carry-over a).
- `test/regression_p3_20260707.cpp` — 6 new fixtures (e-j): 3-block row/column exact-shift, MAX-exceeding clamp, order-freeze fuzz (4 fixed demand sets), staggered-distractor `channelBounds` regression, mixed-direction (need_w+need_h) fixture.

## Algorithm
Two independent longest-path passes (H using need_w, V using need_h). Frozen adjacency per axis
mirrors `compactTowardOrigin`'s `axisOverlap` predicate. Each node's *old* coordinate is folded in
as a virtual edge during the same topological sweep (verified via `deep-reasoner` consult) rather
than clamped post-hoc — this is load-bearing for monotonicity on non-origin-compacted input, not
just defense-in-depth. MAX overflow uses the secant scale factor `s = clamp((MAX-S(0))/(S(1)-S(0)), 0, 1)`
applied only to demand weights (never base `a.w` terms), which is a provable upper bound for the
convex span-vs-scale curve (may leave a little slack, never exceeds MAX) plus a defensive hard
clamp at the boundary.

## Tests
`bash test/run_p3_regressions.sh` — all 10 fixtures pass (4 from T1 + 6 new). Verified empirically:
manually confirmed the MAX-clamp test exercises `scale<1` (span lands exactly at MAX=320, not by
coincidence), and reverted-code check confirms the staggered-distractor test would have failed
under the old tie-break (`50 > 50` false ⇒ wrong block wins). Also green: `run_p1_regressions.sh`,
`run_p0_regressions.sh`, `run_case00.sh`, `run_placement_engine_regressions.sh`,
`run_20260618_regressions.sh`, `run_20260608_regressions.sh`. `make` warning-clean.

## Concerns
- Secant scale is a conservative (not maximal) proportional widen when the binding longest-path
  changes with scale; per the deep-reasoner consult this is intentional (repo simplicity bias) —
  Task 3/4 callers should not assume `channel_overflow` always reaches exactly 0 when demand fits
  within slack in a multi-critical-path scenario, only that it's legal and improved.
- `relaxToDemand` operates on `StripDemand` entries directly (not pre-merged); it internally
  max-merges per axis via `mergeDemandsMax`, consistent with the respace.hpp doc-comment carry-over.
- Task 3 (respaceToDemand) still owns re-anchoring edge blocks and the legality gate; this task's
  output has NOT been run through `placementLegalAt`/`validateSolution` since edges aren't touched
  and outline isn't re-stamped here — that remains Task 3's job per the plan.

Report path: `.superpowers/sdd/p3-task-2-report.md`

## Addendum — review-fix follow-up: all-predecessors relax (2026-07-07)

**Status:** Done. GREEN across all suites.

Post-review, `relaxAxis`'s single-selected-predecessor scheme (nearest-neighbor via strict `>`
tie-break) was replaced with a full all-predecessors max, per two Critical findings:

- **Critical 1 (dropped keyed demand):** a demand keyed exactly `(a,b)` was only applied if `a`
  happened to win `b`'s nearest-geometric-neighbor slot; if some other block `M` won that slot
  instead, `merged.find({a,b})` was never even consulted for `b`'s own relax and the demand was
  silently dropped.
- **Critical 2 (tie-loser overlap):** a block that discarded a candidate predecessor at a tie
  (`far_a == far_b`, first-iterated wins) never re-checked that discarded candidate's *new*
  (possibly grown) position, so the tie-winner's stale computation could end up overlapping the
  discarded candidate after the discarded one was itself pushed forward by its own demand.

Fix (`src/respace.cpp` `relaxAxis`, ~lines 181-216): for each core block `b` in frozen order,
`new_coord[b]` is now `max` over (1) `b`'s own old coordinate (monotone), (2) **every**
earlier-in-frozen-order block `a` with `crossOverlap(a,b)` — not just the nearest — contributing
`new_coord[a] + size[a] + scale*demand(a,b)`, and (3) **every** merged demand pair keyed exactly
`(a,b)` regardless of `crossOverlap`, contributing `new_coord[a] + size[a] + scale*need(a,b)`
(wall entries `a==-1` use `new_coord`/`size` of 0, matching prior wall semantics; `(b,-1)` "grow
the outline" pairs are still deliberately unread here, unchanged from before). Added two required
comments: at the H→V sequencing point in `relaxToDemand` ("V-pass adjacency uses post-H x geometry
deliberately...") and on `relaxToDemand`'s return ("true = relaxed within MAX; residual demand
possible via secant scale."). Both were already present from the prior pass and were left as-is
after verifying wording matches.

### Tests
`test/regression_p3_20260707.cpp` fixtures `testTieLosingCandidateCannotOverlapDiscardingBlock`
(TEST C2) and `testDemandKeyedPairAppliesEvenWhenAnotherBlockWinsPredecessorSlot` (TEST C1) were
rewritten to the exact fixture geometry specified for this follow-up (P/A2/A2P/B rects
`{0,0,10,4}`/`{10,0,1,4}`/`{27,4,2,4}`/`{29,0,5,8}` for C2; L/M/R rects
`{0,0,10,100}`/`{10,60,4,20}`/`{14,0,6,100}` with `need_w=25` for C1), replacing a prior
uncommitted draft that tested the same bug shapes with different (non-spec) numbers.

- **RED confirmed:** reverted `src/respace.cpp` to the committed single-predecessor version
  (`git show HEAD:src/respace.cpp`) and reran — `testTieLosingCandidateCannotOverlapDiscardingBlock`
  aborts on `assert(std::abs(pl.rects[3].x - 31.0) < kEps)` (B stays at 29, overlapping A2's grown
  `[30,31)`), confirming the bug reproduces exactly as designed.
- **GREEN confirmed:** restored the fixed `respace.cpp`; `bash test/run_p3_regressions.sh` passes
  all 12 fixtures (10 prior + these 2), including the C2 exact expected positions
  `{0, 30, 27, 31}` and the C1 gap invariant `R.x - (L.x+10) >= 25 - 1e-6`.
- Also reran (sequentially, to avoid a parallel-`make` object-file race observed when suites run
  concurrently) and confirmed green: `run_p1_regressions.sh`, `run_p0_regressions.sh`,
  `run_case00.sh`, `run_placement_engine_regressions.sh`, `run_20260618_regressions.sh`,
  `run_20260608_regressions.sh`. Clean `make` (after `make clean`) is warning-free.

Commit: `fix: relax over all constraint-graph predecessors, not a selected chain` (src/respace.cpp
+ test/regression_p3_20260707.cpp only; CLAUDE.md left uncommitted per instruction).

---

## Addendum: P3-T2 review fix (2 Critical + 1 Important + 1 Minor)

**Status:** Done. GREEN across all 7 suites.

**Commit:** `9127759` — `fix: relax over all constraint-graph predecessors, not a selected chain`

### Root cause
`relaxAxis` selected a SINGLE nearest predecessor per block per axis and only threaded gap+demand
through that one chain. This caused:
- **Critical 1:** a demand keyed exactly `(L,R)` was silently dropped whenever some other block
  `M` (not `L`) won `R`'s nearest-geometric-neighbor slot — the lookup `merged.find({pred,b})`
  only ever queried `(pred,b)`, never the demand's actual key.
- **Critical 2:** a tie-losing candidate for some block `X`'s predecessor slot was never
  re-checked against `X` after being discarded. If the tie-winner never moved (no demand of its
  own) but the discarded candidate legitimately grew forward via its OWN correct demand edge, `X`
  (still computed only from the stale tie-winner) could end up geometrically overlapped by the
  discarded candidate's new position.

### Fix
Rewrote `relaxAxis` (`src/respace.cpp`) to compute, per block `b` in frozen order (ascending old
coordinate, index tie-break):
```
new_coord[b] = max( coord[b],                                                    // monotone floor
                    max over EVERY earlier-in-frozen-order a with crossOverlap(a,b):
                        new_coord[a] + size[a] + scale*demandOrZero(a,b),
                    max over EVERY merged demand pair (a,b), a earlier in frozen order,
                        regardless of crossOverlap(a,b):
                        new_coord[a] + size[a] + scale*need(a,b) )
```
No single "pred" selection remains — every cross-overlapping earlier block contributes its own
non-overlap edge (fixes Critical 2), and every demand pair applies by its literal key regardless
of who wins any geometric-neighbor tie (fixes Critical 1). O(n²) per axis; acceptable at the
contest's n<50. The secant MAX-scale-back logic (`relaxOneAxis`) is untouched — `scale` still
multiplies only the demand terms, never the base non-overlap terms.

**Important (I3):** added a comment at the `relaxToDemand` H→V sequencing point stating explicitly
that running the V-pass after the H-pass is deliberate: the V-pass's `crossOverlap` predicate reads
`placement`'s live x-geometry, so it collides against the POST-H-PASS x-extents — the actual
post-move geometry that governs real vertical collision, not stale pre-relax x. This resolves the
design ambiguity in code rather than leaving it implicit.

**Minor (M4):** added a comment on `relaxToDemand`'s return-value semantics: with the all-
predecessor fix, demand pairs can no longer be silently dropped, but `true` still only means
"relaxed within MAX" — a demand can still be left partially unsatisfied by the secant scale-back
under MAX pressure, which remains a graded penalty evaluated elsewhere, not a failure this boolean
encodes. Updated the matching `respace.hpp` doc-comment to match (it previously described the now-
removed single-nearest-neighbor scheme).

### New regression tests (`test/regression_p3_20260707.cpp`)
Both confirmed RED against the pre-fix code, GREEN after, via direct `g++` compilation and
execution (not simulation):

1. **`testTieLosingCandidateCannotOverlapDiscardingBlock`** (Critical 2). Legal starting fixture:
   `P` idx0 `Rect{0,0,10,50}`, `a2` idx1 `Rect{10,0,1,50}` (shares P's y-band `[0,50]`), `a2'` idx2
   `Rect{9,50,2,50}` (disjoint y-band `[50,100]`, so legally overlaps `a2` in x without overlapping
   in y — and does NOT overlap P either), `b` idx3 `Rect{29,0,5,100}` (full height, cross-overlaps
   both `a2` and `a2'` bands). One demand `(P,a2)=20`. `a2`'s only true predecessor is `P`, so it
   legitimately leaps to `x=30` (`[30,31]`). Under the old code, `b`'s nearest-neighbor search ties
   between `a2` (far=11) and `a2'` (far=11) at `b`'s near edge (29); the strict `>` comparison means
   whichever is iterated first in frozen order (`a2'`, old coord 9 < `a2`'s old coord 10) wins the
   tie, discarding `a2`. `b` then computes its position from `a2'` alone (unmoved, `9+2=11`) and
   never moves off `x=29`, overlapping `a2`'s new `[30,31]` (`[29,34]` vs `[30,31]`).
   - **RED (confirmed via direct build+run before the fix):** `Assertion '!overlaps(pl.rects[i],
     pl.rects[j])' failed` at the post-relax pairwise-overlap check.
   - **GREEN (after fix):** `b` correctly considers both `a2` (new_coord 30) and `a2'` (new_coord
     9, unmoved) and any legitimate non-overlap edges, landing at `x=31`, clearing `a2` exactly; no
     pair overlaps.
2. **`testDemandKeyedPairAppliesEvenWhenAnotherBlockWinsPredecessorSlot`** (Critical 1). Legal
   fixture: full-height `L` idx0 `Rect{0,0,10,100}`, partial-height wedge `M` idx1
   `Rect{10,60,4,20}` sitting immediately between `L` and `R`, full-height `R` idx2
   `Rect{14,0,6,100}`. One demand keyed directly `(L,R)=25` (NOT `(M,R)`). Under the old code,
   `R`'s nearest x-predecessor is `M` (`M`'s far edge, 14, exactly meets `R`'s near edge), so
   `merged.find({M,R})` misses (the demand is keyed `{L,R}`) and the demand is silently dropped;
   `R` never moves past `x=14`, leaving only a 4-unit `L`-`R` gap, not the demanded 25.
   - **RED (confirmed via direct build+run before the fix):** `Assertion 'pl.rects[2].x -
     (pl.rects[0].x + 10.0) >= 25.0 - 1e-6' failed` (gap was 4, not ≥25).
   - **GREEN (after fix):** the `(L,R)` merged-demand-pair term applies directly regardless of `M`,
     landing `R` at `x=35`, giving exactly the demanded 25-unit gap.
3. All 10 pre-existing T1+T2 fixtures (fixtures a-j: channel-bounds axis-crossing, wall-bounded
   sides, zero-usage no-demand, 3-block row/column exact-shift, MAX-clamp secant, order-freeze
   fuzz, staggered-distractor, mixed-direction) remain GREEN, unmodified.

### Verification
- `make` (clean rebuild): warning-clean, `-Wall -Wextra -pedantic`.
- `bash test/run_p3_regressions.sh`: 12/12 GREEN (10 existing + 2 new).
- `bash test/run_p1_regressions.sh`: 22/22 GREEN.
- `bash test/run_p0_regressions.sh`: 8/8 GREEN.
- `bash test/run_case00.sh`: GREEN, `validation=ok`, `open_routes=0`.
- `bash test/run_placement_engine_regressions.sh`: GREEN.
- `bash test/run_20260618_regressions.sh`: GREEN.
- `bash test/run_20260608_regressions.sh`: GREEN, `validation=ok`, `open_routes=0`.

### Files changed
- `src/respace.cpp` — `relaxAxis` rewritten (all-predecessors max, no single `pred` selection);
  `relaxToDemand` gains the I3 (H→V sequencing) and M4 (return-value semantics) comments.
- `src/respace.hpp` — `relaxToDemand` doc-comment updated to describe the all-predecessors
  relaxation instead of the removed single-nearest-neighbor scheme.
- `test/regression_p3_20260707.cpp` — 2 new fixtures
  (`testTieLosingCandidateCannotOverlapDiscardingBlock`,
  `testDemandKeyedPairAppliesEvenWhenAnotherBlockWinsPredecessorSlot`) plus their `main()` wiring.
