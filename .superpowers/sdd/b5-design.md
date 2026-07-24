# B5 — WL-Directed Search Upgrade + Elite-Pool Fix

Design deliverable for pre-beta ladder item **B5** (`docs/research/2026-07-23-research-refresh-and-strategy.md`
§5 WL-first agenda, §1 cost sizing, §2 surrogate literature). Attacks the 45–63% WL cost share and
closes the **formally-carried budget gate** (b1-report.md "Post-review amendment", Codex-mandated at
B1.1): after B5 the sweep matrix (budget {0,15,45} × {hybrid,bstar}, seed 1) must have **NO cell >
+1% vs its own budget-0 baseline**. Doc-only; **no `src/` or `test/` edited**. Line numbers verified
against `fable5` HEAD `408a297` (post-B6). Style mirrors `b4-design.md`.

**Current default-flag per-case checker cost (HEAD 408a297, the hard Pareto floor for every B5 step):**

| case | checker_cost (default: hybrid iters1000 seed1) |
|---|---:|
| 0 | 5,233,375.00 |
| 1 | 9,100,697.98 |
| 2 | 56,045,213.30 |
| 3 | 36,615,637.24 |
| 4 | 76,369,838.93 |
| **Σ** | **183,364,762.45** |

(Σ cross-checked against progress.md's B4.1 ledger line. case3 = the B3-era number verbatim — its
portfolio picks the legacy-pricing leg.)

---

## 0. Scope, in one paragraph

Three coupled changes, sequenced as three independently-Pareto-safe sub-tasks. **B5a (elite pool):**
the elite pass admits full-treatment candidates by *cheap* rank (`search.cpp:533` `kTopCheapCandidates
= 16`), a signal B6 measured to be a weak-to-inverted predictor of exact rank (`pool_tau` negative on
case1/case3; `exact_best_cheap_rank` = 16/16 on case3 default). B5a widens the cheap *prefilter* and
re-ranks admission by a **cheap-to-compute tight-outline exact score** before spending the expensive
sweep — a one-rung successive-halving race — plus an exact-best archive and Lau-Ho insurance picks.
This is the budget-gate closer. **B5b (proxy):** `cheapScore`'s WL/congestion proxy is blind to the
A31 abutment cliff (abutted pairs route port-to-port at zero WL, zero demand); B5b adds a
bundle-weighted abutment credit with a *ramp* so the SA can find the cliff by local moves. **B5c
(moves):** three WL-directed move types (wire-median, abutment-targeted, greedy-reposition) realized
in both engines' `mutate`. Scale-free acceptance (§6) is **deferred to B2**. Nothing touches
`eval.cpp`'s exact tier, `cfg_score.cpp`, `routing.cpp`'s accrual, or any scorer — self-consistency
(`run_selfconsistency_regressions.sh`) holds by construction.

---

## 1. The mechanism, made precise (grounded in code + B6 data)

The budget gate fails on exactly 4 cells (b1-report.md sweep):

| cell | Δ vs its own budget-0 | B6 rank evidence |
|---|---|---|
| bstar case0 @15 and @45 | **+7.69%** (frozen — identical @15/@45) | case0 default rank 1/16, but R=0: area/WL-only |
| hybrid case2 @15 | +1.68% (recovers to −4.3% @45) | case2 default pool_tau 0.300, rank 7/16 |
| hybrid case3 @15 | **+39.58%** (worst cell) | case3 default rank **16/16**, pool_tau −0.067 |
| hybrid case3 @45 | +21.13% | same |

**Why more budget makes results worse (code-verified).** Within a run every adoption path is
exact-gated and monotone (B1 proved this: best-update `search.cpp:612`, `outlineSweepBetter`
`:143`, `maybeAdoptRespace` Pareto-safe by contract). With the same seed, budget-N replays budget-0's
mutate stream exactly, so `result.best_*` at any shared iteration is provably no worse. The
degradation therefore cannot enter through the *running best*. It enters through the **elite pool's
composition**:

- The full elite treatment — `outlineSweep(raw)` + `outlineSweep(compacted)` + `maybeAdoptRespace`
  (`search.cpp:642-673`) — is applied **only to the top-16 cheap pool members** (`top_candidates`).
  Those transforms are *large*: B3.1 measured the post-finalize sweep alone moving case2 −9.46%,
  case1 −5.59%. So the winning final geometry is frequently `sweep(compact(X))` for some **pool
  member X**, not the raw running best.
- The pool is admitted by cheap value and churns as the SA runs: later candidates with better *cheap*
  value evict earlier ones. When cheap rank does not predict *post-sweep exact* value (B6: `pool_tau`
  ≤ 0 on case1/case3), the pool at a longer budget can **evict the very candidate X whose swept
  geometry won at budget-0**, replacing it with cheaper-but-worse-swept candidates. Budget-N then
  never produces `sweep(compact(X))`, and its final best is worse. This is the "elite-pass
  cheap-score mis-ranking under integer geometry" the register names — sharpened to: *the winning
  transform's input is admitted by a signal (cheap rank) that both mis-ranks it and churns with
  budget.*
- **case3 default rank 16/16 is the smoking gun** (task D's verification): the exact-best member of
  the 16-pool is the *worst* cheap-ranked one. Cheap ordering is inverted for case3, so admission by
  cheap rank systematically excludes the good candidates; case3 is also the worst budget cell
  (+39.6%). Consistent.
- **Confirming task D's checklist:** the running best *is* tracked exactly (`search.cpp:612`) and
  monotone; the elite pass's sweep transforms *are* applied to it — but only **after** the elite loop
  (B3.1 post-finalize sweep, `search.cpp:699-714`) and via `maybeAdoptRespace` per elite. The one
  transform the running best does **not** get inside the elite loop is the raw/compacted
  `outlineSweep` that only pool members receive. So a swept **pool member** that beats the running
  best under a healthy pool is lost under a poisoned pool. That is the leak, and B5a plugs it by
  (a) admitting by a de-churned signal and (b) putting the running best + an exact archive into the
  swept set directly.

`R = 0.002–0.011` on all cases (well below θ=0.05), so overflow is 0–6.9% of cost — the leak is
mostly area/WL basin selection (case0 is pure area). This is why B5a alone is expected to close the
gate: it does not need new routing, only a better elite-admission signal.

---

## 2. (A) Elite-pool redesign — one-rung successive-halving admission

### 2.1 Design decision (primary; alternatives in §2.6)

Keep the expensive full treatment at **16 candidates** (runtime-neutral) but *choose which 16* by a
**tight-outline exact score** re-rank over a **wide cheap prefilter**, plus a small forced set:

```
K_cheap  = 48    // cheap-value prefilter pool (was 16)          -- widen
K_arch   =  8    // exact-value archive (SA-exact-scored bests)  -- new, ~free
K_full   = 16    // full-treatment slots (unchanged count)       -- runtime-neutral
R_ins    =  4    // Lau-Ho insurance reservoir picks             -- new
```

Admission (replaces the `top_candidates` iteration at `search.cpp:642`):
```
cand_set = dedup_by_placementKey( cheap_pool[≤48] ∪ exact_archive[≤8]
                                  ∪ {running best} ∪ insurance[≤4] )
for c in cand_set:  tight[c] = exactCached(c.placement).value      // rung-0: 1 eval, mostly cached
rank cand_set by (open_routes==0 first, then tight ascending)
full_set = top-K_full(cand_set)  ∪  {running best}  ∪  insurance[≤4]   // force-keep best + insurance
for c in full_set:  <existing elite treatment: outlineSweep raw + compacted + maybeAdoptRespace>
```

Why this shape:
- **Rung-0 tight-exact is a *good* predictor and *nearly free*.** It is one `exactScore` at each
  candidate's decoded shrink-wrap outline (real routing, real overflow, real WL) — vs cheap's
  proxy overflow/FT + no routing. B6 measured overall `tau` (cheap vs full-exact) at 0.067–0.807;
  tight-exact vs swept-exact differ only by the outline/capacity optimization the sweep adds (B3.1:
  ≤9%), so tight-exact ranks admission far better than cheap. The B6 elite snapshot
  (`search.cpp:635-640`) *already computes exactly this* for the top-16 — B5a extends it to K_cheap
  and reuses the values, so rung-0 costs only `+ (48−16)` new evals per leg, most of them
  `exact_cache` hits.
- **This is "exact-gate admission with rate limit" (task A option 2) realized as one successive-
  halving rung (option 3):** 48 cheap → tight-exact → 16 full. It simultaneously *widens* K (option
  1: the prefilter is 3× wider) and *corrects the mis-ranking* (the churn-prone cheap signal is
  replaced by tight-exact before the expensive sweep spends on it). The full multi-rung race (§2.6)
  is the fallback if 48→16 in one rung proves too coarse.

### 2.2 Diversity & insurance

- **Diversity criterion:** the tight-exact re-rank is itself the primary de-churn (a good signal does
  not need bucketing). For basin diversity across SA cooling, the **exact archive** (top-8 by exact
  value among SA-exact-scored candidates, placementKey-deduped) is retained regardless of cheap rank
  — these are candidates the SA *already* paid an exact eval for, so they are free to keep and they
  cover basins the cooled cheap pool has abandoned. `placementKey`-dedup (already in
  `rememberCandidate`) is the distance criterion; no new signature hash is needed (a coarse outline
  bucket was considered and rejected as redundant given tight-exact re-rank + the archive).
- **Insurance picks (Lau-Ho blind-pick floor):** `R_ins = 4` reservoir-sampled decoded placements
  across the whole SA run, drawn from a **dedicated `std::mt19937(options.seed ^ 0x5eed)`** so the
  main mutate/accept stream is byte-unperturbed (the reservoir only *adds* to admission, never
  changes which candidates the SA visits). Reservoir algorithm R: for the i-th decoded candidate,
  with prob `min(1, R_ins/i)` replace a uniformly-chosen slot. `R_ins = 4` is sized to the Lau-Ho
  blind-pick floor `1 − C(N−g,s)/C(N,s)` at our scale (≈50 movable-block placements/window): 4 picks
  give a ≥ ~30% floor of landing at least one member in the true top-decile even if the *entire*
  ranked machinery failed — cheap hidden-case insurance, its only job. **Honest caveat:** a reservoir
  is not budget-monotone (its sample differs at different budgets); this is acceptable because it only
  ever *enlarges* the exact-gated admission (can only improve each run), and the budget-gate guarantee
  rests on §2.1 + §5, not on the reservoir.

### 2.3 Capture probability vs today (quantified from the measured ranks)

Let `R*` = the cheap rank (among all SA-visited candidates) of the candidate whose swept geometry is
the true optimum. B6 evidence: the exact-best *member of the K=16 pool* sits at cheap rank up to
**16/16** (case3 default), **15/16** (case1 @4000), and > 8/16 in **5 of 10 runs** — i.e. the winner
repeatedly sits *at the pool boundary*, implying `R*` ≈ 16 or would spill past it under a slightly
longer run (exactly the budget-degradation regime).

| | admission | captures winner iff | on the measured cases |
|---|---|---|---|
| today | top-16 cheap | `R* ≤ 16` | marginal: case3 exactly 16, case1 15 → a longer run misses |
| B5a | top-48 cheap, tight-exact re-rank, + archive + best + insurance | `R* ≤ 48` **or** winner ever SA-exact-scored (archive) **or** = running best | `R*≈16–30` flips miss→capture; archive/best cover the rest |

So capture rises from *marginal at the K=16 boundary* (~50–60% on case1/case3, the failing cells) to
**~95%+**: the winner is retained by the 3× wider prefilter, promoted by the good tight-exact signal,
and independently guaranteed present if it was ever the running best or ever SA-exact-scored. The
residual < 5% (winner at `R* > 48` and never exact-scored) is what the §2.6 fallback / §5 structural
snapshot backstops if the re-sweep still shows a cell > +1%.

### 2.4 Cost model & runtime bound

Per-eval cost (B6 measured): case4 default ≈ 5,775 exact evals in **27 s** ⇒ ~4.7 ms/eval (a
`routeProblem`: 3 negotiation rounds + 1 legacy gate pass). Elite pass dominates the eval count.

```
elite_evals(B5a) ≈  K_cheap tight-exact evals (mostly cached: +32 new/leg over B6's 16)
                  + K_full × (2 sweeps × ~28 rungs, deduped/cached) + maybeAdoptRespace
                  ≈ today's elite cost  +  ~32 cheap evals/leg  +  (R_ins+1) extra full treatments
```
Full-treatment count goes 16 → ≤ 21 (16 by tight-exact + forced best + 4 insurance, deduped) ⇒
**≈ 1.3× elite cost**. 5-case default ≈ 65 s → **~85 s** (bound: ≤ 120 s ✓). 8-case × 2-pricing
portfolio: current worst case4 ≈ 27 s/portfolio → ~35 s after B5a; 8 × 35 s ≈ **5 min** (bound: ≤ 30
min ✓). **STOP** if measured 5-case default > 120 s — then drop `R_ins` to 2 and/or `K_cheap` to 32.

### 2.5 Exact insertion points (search.cpp)

1. `rememberCandidate` (`:524-535`): `kTopCheapCandidates` `16 → 48` (`:533`).
2. **Exact archive (new):** at the SA-loop exact-eval site (`:588-618`), after `exact` is computed,
   insert `(exact.value, effective)` into a `std::vector<CheapCandidate>`-shaped `exact_archive`
   kept top-8 by `exact.value`, placementKey-deduped (mirror `rememberCandidate`'s dedup+sort+resize).
3. **Insurance reservoir (new):** declare `std::mt19937 ins_rng(options.seed ^ 0x5eedu)` and a
   `std::vector<Placement> insurance` (cap 4); after each `cand_place = engine.decode(...)` (`:577`)
   run one reservoir step against `ins_rng`. Never read `rng`.
4. **B6 snapshot (`:635-640`):** keep recording `elite_pairs` in **cheap-rank order** (its documented
   contract) over the widened `top_candidates`; capture each member's `exact` value for reuse in (5).
5. **Admission (`:642`):** replace `for (const auto& candidate : top_candidates)` with the §2.1
   `cand_set` build → tight-exact rank (reuse (4)'s cache) → `full_set` (top-16 + forced best +
   insurance). Body of the loop (sweep raw + compacted + `maybeAdoptRespace`) is **unchanged**.
6. Post-finalize sweep (`:699-714`) and `finalizeFeedthrough` — **unchanged**.

### 2.6 Fallbacks (note, do not build unless a gate fails)

- **Multi-rung successive halving** (task A option 3, full form): rung-0 tight-exact 48→24; rung-1
  raw-sweep-only 24→10; rung-2 full raw+compacted+respace on 10. Use if one-rung 48→16 is too coarse
  (a compaction-favored winner cut at rung-0 because its *tight* score is mediocre but *compacted*
  score is great). Cost-neutral to §2.1; higher fidelity gradient.
- **Structural budget snapshot** (§5) — the guaranteed budget-gate backstop.

---

## 3. (B) Abutment credit in `cheapScore`

### 3.1 Ground truth (A31, cost-reduction §2.2)

When source/target **abut** — share an edge with positive perpendicular overlap — the router connects
port-to-port with **zero WL** (a <3-node guiding-point path sums to 0), **zero channel demand**, and
**no density limit**. "Abuts" must be defined consistently with `routing.cpp`'s adjacency
`positiveTouch` (`routing.cpp:17-23`): a shared vertical edge (`|a.right−b.x|<kEps` or
`|b.right−a.x|<kEps`) with y-overlap `> kEps`, **or** a shared horizontal edge with x-overlap `> kEps`.
The current `cheapScore` charges full `manhattan(centers)·nets` even for abutting pairs
(`eval.cpp:130-136`) and full overflow proxy (`eval.cpp:63-67`) — blind to the cliff.

### 3.2 The credit with a ramp (partial credit — justified)

A **pure cliff** (WL=0 iff exactly touching, full otherwise) is invisible to SA local moves: exact
abutment is measure-zero, and center-manhattan has a *floor* of `(w_a+w_b)/2` at horizontal abut, so
the proxy already bottoms out *positive* and gives no reward for the final snap-to-touch. B5b makes
the reward continuous into g=0. Define per bundle (source rect `a`, target rect `b`):
```
x_overlap = min(a.right, b.right) − max(a.x, b.x)
y_overlap = min(a.top,   b.top)   − max(a.y, b.y)
dx_gap    = max(0, −x_overlap)                       // horizontal separation (0 if x-overlap)
dy_gap    = max(0, −y_overlap)                       // vertical separation   (0 if y-overlap)

g   = (y_overlap > kEps) ? dx_gap                     // can abut on a vertical edge
    : (x_overlap > kEps) ? dy_gap                     // can abut on a horizontal edge
    :                      dx_gap + dy_gap            // diagonal: corner approach, capped credit
has_abut_axis = (y_overlap > kEps) || (x_overlap > kEps)

d_thr  = kAbutRampFrac · 0.5 · (min(a.w,b.w) + min(a.h,b.h))     // ramp width ≈ one block side
credit = clamp(1 − g / d_thr, 0, 1)
credit = has_abut_axis ? credit : min(credit, 0.5)   // diagonal pairs cannot truly abut w/o sliding
```
Apply the credit as a multiplier `(1 − credit)`:
- **WL** (`eval.cpp:134`): `wire_proxy += manhattan(a.cx,a.cy,c.cx,c.cy) · b.nets · (1 − credit)`.
- **Congestion** (`eval.cpp:63-67`, per-bundle overflow term): `penalty += overflow · kBetaLin · (1 − credit)`.

At true abutment (`positiveTouch` true): one axis overlaps `> kEps`, the other gap `= 0` ⇒ `g = 0` ⇒
`credit = 1` ⇒ **both terms → 0** — exactly the A31 cliff, consistent with routing's adjacency
(`kAbutRampFrac = 1.0` default, a named tunable). O(bundles), same loop cost as today.

### 3.3 Why the ramp gives a gradient toward the cliff (the load-bearing justification)

With y-overlap held, `M(g) = (w_a+w_b)/2 + g` and `(1 − credit(g)) = g/d_thr` on `[0, d_thr]`, so
```
credited_WL(g) = M(g) · (g/d_thr) · nets ,   d/dg = [ (w_a+w_b)/2 + 2g ] / d_thr · nets  > 0
```
`credited_WL` is *strictly increasing in g* across the whole ramp: **shrinking the gap always lowers
the proxy, all the way to exactly 0 at abut**, including a positive pull at the last epsilon
(`d/dg|₀ = (w_a+w_b)/(2 d_thr)·nets > 0`) that the un-credited floor never provides. This is precisely
the gradient the SA needs so that the §4 wire-median / abutment moves are *accepted* once they land a
pair near-abutting — the ramp and the moves are co-designed: the moves generate near-abutment
candidates, the ramp rewards keeping them. A pure cliff would leave those moves un-rewarded until an
exact-touch landing that random decode almost never produces. **Recommendation: partial credit
(ramp), width one block side.**

### 3.4 Self-consistency is untouched (no scorer change)

The exact tier does **not** see the credit. `routeAndScoreAt` (`eval.cpp:147-186`) calls
`cheapScore(scoped, placement)` then `score.penalty -= routeAwareProxyPenalty(scoped, placement)`
(`:162`) and overrides `score.wirelength = routing.total_wirelength`, `score.outline_area = w·h`. The
credited overflow term appears in *both* the `cheapScore` addend and the subtraction (same credited
`routeAwareProxyPenalty` function, deterministic in the rects) ⇒ **cancels exactly**; the credited
`wire_proxy` is **overridden** by real routed WL. So `exactScore.value` is bit-identical to today, and
`run_selfconsistency_regressions.sh` holds by construction. B5b changes *only* the cheap SA gradient.

---

## 4. (C) Move portfolio

All three moves share one primitive per representation: **reinsert block X adjacent to a partner P**
in a decode-consistent way, preserving `isValidState`. A move decodes internally
(`this->decode(problem, state)` — µs at n<50) to read geometry, then mutates only the topology fields.
Any move that cannot be realized (no bundles on X, X is an edge block, no partner) falls through to a
plain swap, so `mutate` always returns a valid state. `rng` is the only randomness source (determinism
preserved). `decode`/`isValidState` **signatures unchanged**.

### 4.1 Shared targets

- `wireMedian(problem, place, X)`: bundle-weighted median of X's partners' centers. Collect
  `(partner_center, nets)` over every bundle touching X; the **weighted median** of the x-coordinates
  and (independently) the y-coordinates is the per-module HPWL-optimal point (2023-D `optimPos`).
  O(deg(X)·log).
- `netWeightedBundle(problem, rng)`: roulette over bundles by `nets` (prefix-sum + one uniform).
- `worstBlock(problem, place)`: `argmax_X badness(X)`, `badness(X) = α·Σ_{bundles(X)} nets·manhattan(X.c,
  partner.c) + X's routeAwareProxyPenalty share`. O(bundles).

### 4.2 SP realization (`SequencePairEngine::mutate`, `placement_engine.cpp:519-534`)

SP quadrant convention (from the decode at `:558-566`): `pos_rank[a]<pos_rank[b] ∧ neg_rank[a]<neg_rank[b]`
⇒ a **left of** b; `pos<pos ∧ neg>neg` ⇒ a **below** b. To realize "place X adjacent to P on side s",
remove X from both permutations and reinsert at P's neighborhood:

| desired side of X vs P | pos insert | neg insert |
|---|---|---|
| left  (X.right ⟂ P.x) | just before P | just before P |
| right | just after P  | just after P  |
| below | just before P | just after P  |
| above | just after P  | just before P |

`reinsertSPAdjacent(state, X, P, side)`: erase X from `sequence_pos`/`sequence_neg`, then
`insert` per the table. Result is a permutation of `movableBlocks` ⇒ `isValidState` holds by
construction. **Wire-median move:** X = random movable (or the net-weighted pick); P = X's partner
nearest `wireMedian(X)`; side = the quadrant of `wireMedian(X)` relative to P's center. **Greedy-
reposition:** identical but `X = worstBlock(...)`.

### 4.3 B*-tree realization (`BStarEngine::mutate`, `placement_engine.cpp:643-660`)

B*-tree decode (`:680-691`): left child placed at `parent.right` (right-of parent), right child at
`parent.x` (stacked above in contour). To abut X to P: **detach X** (the `moveLeaf` detach logic,
`:745-750`) then **attach as P's child**: left child ⇒ X right-of P (shares P's right edge); right
child ⇒ X above P. `reinsertBStarChild(state, X, P, side)`:
```
detach X (clear old parent's link, parent[X] = -1)
if P's chosen-side slot free: attach there
elif P's other slot free: attach there
else: attach X as child of the nearest free-slot node walking from P toward wireMedian(X)   // moveLeaf-style target
parent[X] = chosen; (left|right)[chosen] = X
```
Choose side from the target: `wireMedian(X).x > P.cx` ⇒ left child (right-of P); else right child
(above P). The detach+reattach preserves the single-rooted tree invariant ⇒ `isValidState` holds.
Same X-selection split as SP for wire-median vs greedy.

### 4.4 Abutment-targeted move (both engines)

1. `b = netWeightedBundle(problem, rng)`; `X, P = b.source, b.target`.
2. If both are edge blocks → fall through (edges are outline-anchored, not topology-placed). If one is
   edge, X := the movable one, P := the edge one (move the free block to the anchored one).
3. **PORT EDGE legality:** the intended shared edge must be port-legal for both. To abut X left of P
   (shared vertical edge) X connects on edge 3, P on edge 1 — require `portAllows(X,3) ∧ portAllows(P,1)`
   (`routing.cpp:25-30` semantics). Try sides in order {left, right, below, above}; take the first
   port-legal pair; if none, fall through.
4. Realize via `reinsertSPAdjacent` / `reinsertBStarChild` for the chosen side.

### 4.5 Proposal probabilities (fixed; Fast-SA / FTAFP convention)

SP `mutate` (rewrite `move_dist`; today 5 uniform moves):

| move | prob | note |
|---|---|---|
| swap pos / neg / both (structural) | 0.40 | today's moves 0–2, kept |
| soft-aspect nudge | 0.10 | today's move 3 |
| edge-location choice | 0.05 | today's move 4 |
| wire-median | 0.20 | new |
| abutment-targeted | 0.15 | new |
| greedy-reposition | 0.10 | new |

B*-tree `mutate` (today 4 moves): node-swap 0.25, moveLeaf 0.15, soft-aspect 0.10, edge-choice 0.05,
wire-median 0.20, abutment 0.15, greedy 0.10. Geometric-targeted moves get **0.45** total in both.
**Fixed, not phase-dependent** — recommended for B5: the SA temperature already anneals
exploration→exploitation (a WL-lowering-but-area-raising move is rejected when cold), so phase-tied
probabilities add a schedule interaction (and a determinism-review surface) for no measured need. If a
later tuning wants it, it belongs with the §6 schedule work in B2, not B5.

---

## 5. (D) Budget-gate closure

**Empirical closer (B5a):** §1's leak is "a swept pool-member winner evicted under a churning cheap
pool." B5a removes both drivers: admission is re-ranked by **tight-exact** (a stable, well-correlated
signal — case3's inverted cheap order stops mattering because the sweep spends only on the tight-exact
top-16), the prefilter is **3× wider** (retains the winner past the K=16 boundary the ranks sit on),
and the **running best + exact archive are force-admitted to the swept set** (so the monotone best's
swept geometry is always produced — the transform the running best previously missed inside the elite
loop, §1). Cell-by-cell:
- **bstar case0 +7.7% (R=0):** pure area/WL basin loss on SA cooling. Tight-exact ≈ final-exact when
  R=0 (WL stable, no overflow), so the re-rank is near-perfect here and the widened prefilter + archive
  retain the diverse-basin winner the cooled cheap pool dropped. Expected to close fully.
- **hybrid case2/case3 @15/@45:** overflow-basin loss. Tight-exact *routes for real* (sees the integer-
  geometry overflow the cheap proxy mis-estimates), so admission promotes genuinely low-cost basins;
  wide prefilter + archive retain the winner. Directly attacks "mis-ranking under integer geometry."

**Structural backstop (guaranteed; build only if the re-sweep still shows a cell > +1%).** Because
budget-N replays budget-0's mutate stream for its first `options.iters` iterations, snapshot the
cheap+exact pools when `iter` first reaches `options.iters` (= REF_ITERS = 1000 for the budget runs,
whose `iter_bound` is separately raised to 2M at `search.cpp:562` while `options.iters` stays 1000).
Always union that `checkpoint_pool` into the elite admission. Then budget-N's admission ⊇ budget-0's
*final* admission, and since every elite adoption is exact-gated, **budget-N ≤ budget-0 structurally**
— the same "reproduce the shorter run's basis, then only improve" floor B4.1's pricing portfolio and
B1's legacy-seed leg use. Cost: hold ≤ 48 extra placements. This converts the gate from empirical to
proven; it is deliberately *not* primary because it adds a fragile "snapshot at iter==options.iters"
coupling and the §5 empirical closer is expected to suffice on its own (verify first).

---

## 6. (E) Scale-free acceptance — defer to B2

**Recommendation: DEFER.** Relative-delta normalization (`Δ/current`, 2023-D) retunes acceptance
uniformly across the 1e7–1e8 cost span — genuinely attractive — but it is a **pure trajectory change
with no structural floor**:
- All Pareto gates are post-hoc (elite pass, sweeps, portfolio fold) — they gate the *output*, so a
  schedule change only shifts *which* trajectory feeds them, uncovered by any per-placement floor.
- The pricing-portfolio floor (B4.1) does **not** cover it: both pricing legs share the one schedule,
  so a scale-free schedule changes both legs together — there is no old-schedule leg to fold against.
- It would compound B5a/B5b/B5c's own trajectory perturbations, making a budget-gate regression
  impossible to attribute. Adopting an *unguarded* trajectory change in the exact task whose acceptance
  *is* the budget gate is the wrong risk.

If adopted later it needs its own per-case ≤ mechanism — an old-schedule portfolio leg, or (better)
**B2's multi-seed × {sp,bstar,hybrid} restart portfolio**, which folds trajectory variance by value
exactly as B4.1 folds pricing variance. That is where a schedule retune belongs. FastFP rejection-
ratio-adaptive cooling (research §5.5) is the same story — B2.

---

## 7. (F) Verification plan

**Per-item unit / regression tests** (new `test/regression_b5_20260724.cpp` + `run_b5_regressions.sh`,
17th suite; link `routing eval io validate placement_engine search respace route_cost cfg_score`):

- **B5a:** (i) admission unit test — a hand-built pool where the cheap-best ≠ tight-exact-best asserts
  the full-treated set is the tight-exact top-16 (not cheap top-16), running best always present,
  insurance count = 4; (ii) determinism smoke — two `runPricingPortfolio` invocations, same case/seed,
  byte-identical `.cfg`; (iii) archive/reservoir determinism (fixed seed ⇒ fixed picks).
- **B5b:** (i) `abutmentCredit` unit table — abut ⇒ credit 1 / WL term 0; `g = d_thr` ⇒ credit 0;
  `g = d_thr/2` ⇒ credit 0.5; diagonal ⇒ capped ≤ 0.5; (ii) a routed fixture where an abutting heavy
  bundle contributes 0 to `wire_proxy`; (iii) **self-consistency stays 5/5** (the cancellation §3.4).
- **B5c:** (i) each move preserves `isValidState` (fuzz 10⁴ mutations per engine from `initialState`);
  (ii) `reinsertSPAdjacent`/`reinsertBStarChild` produce the intended adjacency on a hand-built state;
  (iii) abutment move yields a `portAllows`-legal shared edge; (iv) determinism.

**τ-harness as the proxy-improvement metric** (re-run B6's measurement, `--adaptive-interval` off,
default + `--iters 4000`, per b6-report.md §"Findings for B5" #5). Numeric targets:

| metric | B6 baseline | B5 target |
|---|---|---|
| overall `tau` (mean) | ~0.52 | ≥ 0.60 (B5b: cheap now sees the cliff) |
| `pool_tau` (mean) | 0.246, **negative** on case1/case3 | ≥ 0.35 mean, **≥ 0 on every case** |
| `exact_best_cheap_rank` | > 8/K in 5/10 runs; case3 16/16 | **no case worse than 8/K** |

(The rank/pool_tau are measured on the *cheap* pool; B5a's tight-exact re-rank is what makes the
admitted set robust even when these stay imperfect — the harness confirms the proxy improved, the
budget sweep confirms the *outcome*.)

**Per-case ≤ current at default flags** — hard gate, pinned as `%.17g` named constants (B1's
`kPreB1DefaultUpperBounds` lesson: 12-sig-fig pins spuriously fail a 1e-6 tolerance): case0
5,233,375.00 / case1 9,100,697.98 / case2 56,045,213.30 / case3 36,615,637.24 / case4 76,369,838.93.

**Budget sweep gate (B5 acceptance):** `ENGINES="bstar hybrid" BUDGETS="0 15 45" SEEDS="1"
CASES="0 1 2 3 4" REF_ITERS=1000 bash scripts/sweep.sh` — **every cell ≤ +1% vs its own budget-0
baseline**, golden-rescored, all `open_routes=0 validation=ok`. This is the STOP condition for B5.

**Runtime bound:** 5-case default ≤ 120 s (projected ~85 s, §2.4); 8-case × 2-pricing ≤ 30 min
(projected ~5 min). Re-measure after B5c (adds decode-in-mutate, ~µs, negligible).

---

## 8. (G) Sequencing within B5

**Order: B5a → B5b → B5c** (increasing trajectory-perturbation risk; each Pareto-gated independently).

| step | change | go/no-go evidence before proceeding |
|---|---|---|
| **B5a** | elite pool (widen prefilter + tight-exact re-rank + archive + insurance) | **budget sweep gate passes** (the acceptance — B5a is the closer) **and** per-case ≤ current **and** `pool_tau ≥ 0` / rank ≤ 8/K improve. Runtime ≤ 120 s. |
| **B5b** | abutment credit ramp in `cheapScore` | overall `tau` **rises** (proxy genuinely better, not just relocated) **and** per-case ≤ current **and** budget gate still holds **and** self-consistency 5/5. |
| **B5c** | wire-median / abutment / greedy moves | per-case ≤ current **and** budget gate holds **and** `tau`/rank improve. Re-measure runtime. |

Rationale: **B5a first** — it is the smallest change *and* the budget-gate closer (task G's hypothesis
confirmed by §1: the gate is an elite-admission bug, not a proxy or move deficiency), so if it closes
the gate alone the rest is pure upside landing on a de-risked base. **B5b before B5c** — the moves
(§4) are *guided by* the abutment-aware cheap score; landing the proxy first means B5c's wire-median /
abutment moves are rewarded by the ramp (§3.3 co-design) rather than fighting a cliff-blind proxy.
Each step is exact-gated to per-case ≤ current, so each lands safe on its own; if any step's gate
fails, STOP and report (B1/B4 precedent — do not improvise past a STOP).

---

## 9. Risk register

- **R1 (B5a re-rank still misses the winner at extreme budget).** *Mitigation:* wider prefilter +
  archive + running-best force-admit (§2.1); §5 structural snapshot is the guaranteed backstop.
- **R2 (widened elite pass blows runtime).** *Mitigation:* full-treatment count held at 16; measured
  ~1.3×; STOP + drop `R_ins`/`K_cheap` if > 120 s (§2.4).
- **R3 (abutment ramp mis-guides — over-credits distant pairs, WL regresses).** *Mitigation:* narrow
  ramp (`d_thr` ≈ one block side); τ-harness must show `tau` *up*; per-case ≤ current gates it; the
  exact tier is uncredited (§3.4) so the *scored* WL is never optimistic.
- **R4 (self-consistency broken by the proxy change).** *Mitigation:* the cancellation is algebraic
  (§3.4) — same credited `routeAwareProxyPenalty` on both sides, WL overridden; pinned by the 5/5
  suite as a hard gate.
- **R5 (a new move corrupts state → `isValidState` false / decode UB).** *Mitigation:* every
  reinsertion is permutation-/tree-preserving by construction; infeasible moves fall through to a
  swap; 10⁴-mutation fuzz per engine.
- **R6 (moves decode inside `mutate` → cost / nondeterminism).** *Mitigation:* n < 50 (µs); only the
  0.45 geometric fraction decodes; single shared `rng`; no wall-clock/static.
- **R7 (B5a cascades into hybrid's bstar-leg seed → cross-case trajectory shift).** *Mitigation:* same
  benign cascade B1/B3 documented; caught by the per-case ≤ current gate (empirical, not assumed).
- **R8 (insurance reservoir not budget-monotone).** *Mitigation:* it only *enlarges* the exact-gated
  admission (can only improve each run); the gate guarantee rests on §2.1 + §5, not the reservoir.

---

## 10. Task breakdown (Pareto-safe, TDD-able, STOP conditions)

Every task builds warning-clean (`-Wall -Wextra -pedantic`), keeps determinism (single seeded `rng`,
no wall-clock/static in the hot path), and lands green independently. Each is gated by "per-case ≤
current" so regression is caught, not assumed.

- **B5a-T1 — widen cheap pool + exact archive (`search.cpp`).** `kTopCheapCandidates 16→48`; add
  `exact_archive` (top-8 by exact.value) at the SA exact site (`:588-618`). No admission change yet.
  Test: archive holds the 8 best-by-exact, deduped. ~25 LOC + test.
- **B5a-T2 — insurance reservoir (`search.cpp`).** Dedicated `ins_rng`, reservoir of 4 over decoded
  candidates. Assert `rng` stream untouched (byte-identical `.cfg` with `R_ins` forced 0). ~20 LOC.
- **B5a-T3 — tight-exact admission (`search.cpp:642`).** Build `cand_set`, re-rank by tight-exact
  (reuse the B6 snapshot cache), full-treat top-16 + forced best + insurance. Body unchanged. Tests:
  §7 B5a (i); determinism (ii). **STOP + report if the budget sweep still shows any cell > +1%** —
  then escalate to §2.6 multi-rung and/or §5 snapshot. ~50 LOC + test.
- **B5a-T4 — budget sweep + per-case gate.** Run the acceptance sweep + default matrix; append
  before/after table + honest per-cell notes. **This is B5a's acceptance.** Doc + harness.
- **B5b-T1 — `abutmentCredit` helper + credit application (`eval.cpp`).** Add the §3.2 helper; apply
  `(1−credit)` in `cheapScore` WL (`:134`) and `routeAwareProxyPenalty` overflow (`:63-67`). Tests:
  §7 B5b (i)/(ii); **self-consistency 5/5 (iii) is a hard STOP**. ~30 LOC + test.
- **B5b-T2 — τ + per-case gate.** Re-run the τ-harness (targets §7); per-case ≤ current. **STOP if
  `tau` does not rise** (the proxy change failed its purpose — reconsider `kAbutRampFrac`).
- **B5c-T1 — shared move primitives (`placement_engine.cpp`).** `wireMedian`, `netWeightedBundle`,
  `worstBlock`, `reinsertSPAdjacent`, `reinsertBStarChild`. Pure/unit-tested in isolation (§7 B5c
  (ii)). No `mutate` change yet. ~90 LOC + test.
- **B5c-T2 — wire in the three moves + probabilities (`placement_engine.cpp`).** Rewrite both
  `mutate` move tables (§4.5); moves decode internally; infeasible → swap fallback. Tests: §7 B5c
  (i)/(iii)/(iv). ~60 LOC + test.
- **B5c-T3 — full suite + budget sweep + runtime.** All `run_*.sh` green incl. self-consistency 5/5;
  per-case ≤ current; **budget gate holds**; runtime bounds (§7). **This is B5's final acceptance.**

**Ordering rationale.** B5a-T1/T2 assemble the pool machinery behind tested pieces before T3 flips the
admission (the one behavior change) — so the budget-gate STOP in T3 is isolated. B5b is a
single-function proxy edit gated by self-consistency. B5c builds primitives (T1) before wiring (T2).
Each sub-task falls back cleanly (infeasible move → swap; failed gate → STOP), so a regression is
caught at its own gate, never silently shipped.
