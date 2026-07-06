# P1 — Declared Outline as a Solution Variable (Shrink-Wrap + Edge Re-Anchor)

Design deliverable for roadmap item P1 (`docs/research/2026-07-06-cost-reduction-strategy.md` §2.1, §4 items 5–6).
Doc-only; no source edited. All line numbers verified against `fable5` HEAD at authoring time.

---

## 0. Problem restated (grounded)

`Cost = OutlineArea + α·TotalWireLength`. `OutlineArea` = the **declared** output outline W×H
(QA A25: includes routing channels + FT-inflated blocks). MAX (`io.cpp:167-170`) is a ceiling only.

Today the declared outline is nailed to MAX everywhere and the SA has **no area gradient**:

- `problem.outline_w/h = MAX` (`io.cpp:169`). It flows into every engine geometry op.
- Edge blocks anchor to MAX borders: `placeEdge` (`placement_engine.cpp:31-62`) sets R blocks to
  `x = outline_w - w` (`:45,:53`) and T blocks to `y = outline_h - h` (`:46,:50`); `firstLegalEdgeSlot`
  (`:126-186`) and `legalizeEdgeBlocks` (`:188-208`) re-legalize against those same borders.
- `usedOutline` (`eval.cpp:7-17`, `:19-34`) is `min(MAX, bbox-of-rects[+channels])`. case0 BLK04 is `RB,BR`
  ⇒ pinned to `right()==MAX_w`, `y==0` ⇒ **bbox width ≡ MAX_w** ⇒ `outline_area` stuck at MAX (6.47M),
  while Σblock areas ≈ 3.60M (util 55.6%; hand-verified from `test/2026 ICCAD_case.case0.csv`).
- `cheapScore` (`eval.cpp:78-100`) uses the same `usedOutline` ⇒ **the area term is a constant during SA**.
  Only α·HPWL and penalties move the search. This is the single biggest untapped lever.

So P1 must (a) give `cheapScore` a real *geometry-only* area gradient, (b) let exact evals and output
route/score inside a genuinely smaller outline, (c) re-anchor edge blocks to the shrunk border while
preserving all legality (flush, thirds, corner-AND), and (d) let `finalizeFeedthrough` **grow** the
outline back toward MAX to absorb FT inflation (fixing the residual mismatch defect, §4 item 5).

---

## 1. Decisions on the architecture sketch

### 1.1 Where the declared outline lives — **derived-per-candidate, not SA genes** (agree with sketch)

**Decision:** add `double outline_w, outline_h` to `Placement` (`model.hpp:59-62`), default `0` meaning
"not yet shrink-wrapped / fall back to `problem` outline". They are **computed by a pure function of the
decoded rects**, never mutated by the engine. No new `PlacementState` genes.

**Why derived, not genes (reject 2 extra SA genes for v1):**
- The optimal declared W,H for a *fixed* set of block coordinates is deterministic: it is the minimal
  legal bounding envelope (block extents + edge/thirds constraints + channel-demand margin). Making it a
  gene means SA wastes moves rediscovering a closed-form quantity and mostly proposes illegal (too-small)
  or dominated (too-big) outlines. Derived-per-candidate gives the exact answer for free every decode.
- `decode` already packs core blocks toward the origin against MAX (`placeWithContour`,
  `placement_engine.cpp:305-326`); the *content* extent is what SA explores. Outline is a readout of it.
- Genes also bloat `isValidState`/`seedFromPlacement`/mutation-probability tuning across **both** engines
  for zero modeled benefit. Keep the representation minimal (repo convention: `CLAUDE.md` "small
  deterministic helpers").
- **Rejected alternative — outline in `PlacementState`:** couples outline to engine internals, forces
  every engine to carry/serialize it, and still needs the shrink-wrap function anyway to know a *legal*
  value. Strictly worse.
- **Future hook (P4, not now):** if profiling shows the shrink-wrap's aspect choice for the whole die
  matters (it trades width-vs-height of channel demand), a single scalar "outline aspect bias" gene could
  be added later. Out of scope for v1; the derived function picks the tight envelope which is the right
  default.

**Consequence for the two-tier cache:** the declared outline now varies per candidate, and routing
happens *inside* it, so it must participate in the exact-cache key (§3.3).

### 1.2 Decode unchanged; new post-decode `shrinkWrapOutline` (agree, with a correctness caveat)

**Decision:** leave `SequencePairEngine::decode` / `BStarEngine::decode` byte-for-byte unchanged — they
still pack core blocks toward origin against `problem.outline_w/h == MAX` (this is correct: packing
against the *ceiling* gives the most freedom, and shrink happens after). Add a free function

```cpp
// placement_engine.hpp (new decls) / placement_engine.cpp (impl)
struct OutlineFit { double w; double h; bool legal; };
OutlineFit shrinkWrapOutline(const Problem& problem, Placement& placement);  // mutates edge rects in place
```

that runs at the **end of each decode**, after `legalizeEdgeBlocks`. It computes the minimal legal
`(W,H) ≤ MAX`, re-anchors edge blocks to the shrunk border, and writes `placement.outline_w/h`. It never
enlarges beyond MAX and never moves a core block (see §1.2.3 for why core is left alone and how overlap is
prevented).

**Caveat vs the sketch's "grow W/H until legal":** growing the *outline* to fix an
edge-vs-core overlap is subtle because edge anchoring is a function of the outline (moving the R border
in moves the R block in, which can then hit core; moving it back out un-hugs the border). The clean
formulation is a **lower-bound + monotone feasibility** computation, not a blind grow loop:

#### 1.2.1 The shrink-wrap lower bounds (per axis, independent)

For width `W` (height `H` symmetric):

- `W_core = max over non-edge blocks of rect.right()` — core packing extent (unchanged by shrink).
- `W_edgeR = max over R-side edge blocks of block.w` — an R block flush to the right border occupies
  `[W - w, W]`; for it not to cross the left wall we need `W ≥ w`, but more importantly it must not
  overlap core. That is handled by the corridor test below, not by a width bound.
- `W_LBedge` = the largest right-extent forced by **L-side** and **B/T-with-x-anchored** edge blocks that
  are pinned by `x` (L blocks sit at `x=0`, width `w`; they only bound `W ≥ w`).
- `W_thirds` = the minimum width at which every edge block's LOCATION thirds constraint is still
  satisfiable on the *shrunk* thirds (see §1.2.2).

`W_min = max(W_core, W_edge_width_needs, W_thirds, 1)`, then `W = clamp(W_min, ·, MAX_w)`. Symmetric `H`.

Because R/T edge blocks are re-anchored to `W`/`H` *after* `W`/`H` is chosen (they hug the shrunk
border), they never bound `W` from above; they only participate through the overlap corridor and thirds.

#### 1.2.2 Thirds / corner-AND on the shrunk outline

`locSegment` (`placement_engine.cpp:68-78`) and `rectTouchesLocation` (`:84-92`) already compute thirds
from `problem.outline_w/h`. The shrink-wrap must evaluate the same predicates against the **candidate
`W,H`**, not MAX. So `shrinkWrapOutline` builds a *scoped* `Problem` copy with `outline_w=W, outline_h=H`
(same trick `exactScore` uses at `eval.cpp:104-106`) and calls the existing `edgeRectLegal`
(`:94-111`) after re-anchoring. Key sub-rules, all already implemented and reused unchanged:

- **90° pair (corner AND)** — `sides.size()>1` branch (`:107-110`): block must touch *all* its LOCATION
  segments. case0 BLK01 `TL,LT` ⇒ top-left corner; BLK04 `RB,BR` ⇒ bottom-right corner. On a shrunk
  outline the corner just moves; the block still sits in it. Corner blocks force
  `W ≥ (their width)` and `H ≥ (their height)` trivially, plus they pin the opposite-axis anchor.
- **Parallel multi (touch-one)** — `sides.size()==1` branch (`:101-106`): any one segment satisfied.
  1D overlap along the boundary (A19/A20) is exactly `positiveOverlap` (`:80-82`); a block longer than a
  third still legal. Shrinking narrows the thirds, so a mid block `TM` needs `W` large enough that
  `[third_w, 2·third_w]` still has positive overlap with the block — captured by `W_thirds`.

`W_thirds` is found by **feasibility test, not formula**: shrink is monotone (smaller W ⇒ thirds shrink ⇒
harder to satisfy), so binary-search / linear-probe `W` down from MAX to the smallest value where every
edge block re-anchors legally (see §1.2.4). In practice the corner/side blocks dominate and the loop is
2–4 evaluations.

#### 1.2.3 Why core blocks are never moved, and overlap can't appear

Core (non-edge) blocks keep the coordinates `decode` gave them. Shrink only:
1. picks `W,H ≥ W_core,H_core` (so no core block ever pokes past the shrunk border — outline violation
   impossible by construction), and
2. moves **R and T** edge blocks *inward* (L stays at `x=0`, B stays at `y=0`, unchanged).

The only new overlap risk is an inward-moved R/T edge block hitting a core block. `legalizeEdgeBlocks`
already places edge blocks with `collidesWithPlaced` (`:265-272`, invoked via `firstLegalEdgeSlot`
`:183`), but that runs against MAX. So the shrink must **re-run edge legalization against the scoped
(W,H) `Problem`** — i.e. call `legalizeEdgeBlocks(scoped, placement)` — which slides each edge block
along its border to the first non-colliding, thirds-legal slot at the shrunk coordinates. If no legal
slot exists at `(W,H)` for some edge block, that `(W,H)` is infeasible ⇒ grow (§1.2.4). This reuses
100% of the existing legality machinery; **no new geometry predicate is written**, eliminating the risk
of a shrink path that emits overlap the validator would reject.

#### 1.2.4 The feasibility loop (monotone grow-until-legal, bounded)

```
W = max(W_core, edge width needs, 1);  H = max(H_core, edge height needs, 1)
for guard in 0..K:                       # K small, e.g. 6
    scoped = problem; scoped.outline_w=min(W,MAX_w); scoped.outline_h=min(H,MAX_h)
    trial = placement (copy of core rects; edge rects reset to dimensionFor)
    legalizeEdgeBlocks(scoped, trial)                # existing fn, scoped outline
    if every edge block edgeRectLegal(scoped,·) AND no edge∩core overlap AND all rects ⊆ scoped:
        commit trial edge rects into placement; placement.outline_w/h = scoped; return {W,H,true}
    else: W = min(MAX_w, W * 1.05 + slack);  H = min(MAX_h, H * 1.05 + slack)   # or bump the deficient axis only
return {MAX_w, MAX_h, ...}                # worst case: fall back to MAX (never illegal, just no shrink)
```

Monotonic and bounded: each iteration grows toward MAX, MAX is always legal (it's what decode already
produced), so the loop terminates with a legal outline within `K` steps. Worst case = no shrink (today's
behavior) — **strictly a Pareto improvement, never a regression on legality.** Prefer bumping only the
axis whose edge legalization failed (cheaper, tighter) over scaling both.

### 1.3 `cheapScore` area gradient — geometry-only shrink-wrap + crude channel margin (agree)

**Decision:** in `cheapScore` (`eval.cpp:78-100`), replace `usedOutline(problem, placement)` with the
shrink-wrapped `(W,H)` — but a **cheap variant that does no edge re-legalization loop**, only the
lower-bound arithmetic of §1.2.1 plus the thirds feasibility check, so it stays O(n). Because `decode`
now stamps `placement.outline_w/h`, `cheapScore` can simply read those fields when present:

```cpp
double W = placement.outline_w > 0 ? placement.outline_w : problem.outline_w;   // set by shrinkWrapOutline in decode
double H = placement.outline_h > 0 ? placement.outline_h : problem.outline_h;
score.outline_area = W * H + channelMarginProxy(problem, placement, W, H);
```

`channelMarginProxy` = crude v1 whitespace demand: `Σ_bundle nets / 25` summed as area a strip must
reserve, e.g. for each bundle add `(nets/25) * min(dx,dy)`-ish reserved width × the span it crosses,
clamped so it never exceeds `(MAX_area − W·H)`. Keep it deliberately crude (roadmap §4 item 5: "cheap
eval uses proxy demand"); it exists to bias SA away from packings so tight they'd overflow, **not** to
predict routing. The out-of-outline penalty at `eval.cpp:85` must now compare against MAX (the ceiling),
**not** the shrunk `(W,H)` — otherwise a legal-but-tight core would be penalized as out-of-outline
during cheap eval. Concretely: keep `eval.cpp:85` using `problem.outline_w/h` (MAX); the shrink only
changes `score.outline_area`, not the containment penalty.

**Interaction with the parallel `score.value` fix:** the task says assume
`value = outline_area + α·wire_proxy + score.penalty` post-fix (`eval.cpp:98`). My change only alters how
`score.outline_area` is computed; it slots in cleanly under that composition. No conflict.

### 1.4 `exactScore` — route inside the shrunk outline; growth fallback on elites (refine the sketch)

**Decision:** `exactScore` (`eval.cpp:102-116`) already scopes a `Problem` copy's outline before routing
(`:103-106`). Change the scope source from `usedOutline(bbox)` to the placement's shrink-wrapped
`(W,H)` (the value `decode` stamped, or recompute via the *exact* `shrinkWrapOutline` if unset). Then
`extractChannels` (`routing.cpp:48-84`) slices the **smaller** outline ⇒ channels genuinely get scarcer
⇒ overflow/opens can appear. That is the correct cost signal; SA sees it through periodic exact evals.

**Growth fallback — reject naive per-eval binary search; use elite-only grow-on-failure.** Doing a
5–7-trial binary search on *every* exact eval multiplies routing cost 5–7× and blows the "periodic exact"
budget (routing dominates runtime, per §2.6 of the strategy doc). Instead:

- **Per exact eval (periodic + best-candidate, `search.cpp:180-189`):** single route at the tight
  shrink-wrapped `(W,H)`. If `open_routes>0` OR `channel_overflow` is severe (> a threshold τ), the
  outline is too tight to route ⇒ do **one** corrective grow step (re-run `shrinkWrapOutline` with an
  inflated channel-demand margin = actual `channel_overflow/25` widths from this route) and re-route
  once. Cap at 1 corrective step in the hot loop.
- **Elite pass only (`search.cpp:193-209`, the top-16 + compacted variants):** here spend the
  binary/geometric search — 5–7 route trials over `s ∈ [s_min, 1]` scaling the declared outline
  (or, better, an additive channel-demand margin) to find the min-Cost outline for that fixed placement.
  This is where quality matters and the candidate set is tiny (≤32), so cost is bounded. Pick the `s`
  minimizing `W·H + α·WL + graded_overflow_penalty`. This directly makes the search *see* the
  area↔wirelength↔overflow trade the sketch describes (smaller outline: −area, +WL, +overflow risk).

`s_min` = the tightest legal scale from §1.2.4 (below it, edge legalization fails). Deterministic:
fixed trial points, no RNG.

### 1.5 `finalizeFeedthrough` — grow the declared outline to absorb FT inflation (the P1 acceptance fix)

This is the explicit residual-mismatch fix (strategy doc §4 item 5 / Appendix; current defect at
`search.cpp:96-97`). Two coupled changes:

1. **The known guard bug (already root-caused in the strategy doc):** `search.cpp:96-97` commits only if
   `exact.value < best`. Change to commit whenever `feedthrough_mismatch` **strictly decreases** vs the
   current best (pay the mismatch penalty down first), independent of total value, then attempt
   re-legalize/compact to reclaim outline/overflow. *(This is P0 follow-up but P1 depends on it.)*

2. **Give the inflated re-decode headroom by growing the outline (the P1 part).** The root cause of the
   residual is: inflating a soft block's area and re-decoding via `seedFromPlacement`+`decode`
   (`search.cpp:91-92`) has **no room** on tight (hybrid case1/case3) packings — blocks spill out or
   channels overflow. With P1, `decode` shrink-wraps to a tight `(W,H)`; but during finalize we *want*
   slack. So: in `finalizeFeedthrough`, after inflating soft areas, **decode against MAX and let the
   shrink-wrap grow** (do **not** clamp to the pre-inflation `(W,H)`). Concretely the inflated Problem's
   ceiling stays MAX (`search.cpp:78` copies `problem`), and the re-decode's `shrinkWrapOutline` will
   naturally settle at whatever `(W,H) ≤ MAX` legally fits the inflated blocks. Because MAX is the real
   ceiling and case0-4 all have ≥20% headroom (util ≤ ~65%), the inflated blocks fit and mismatch → 0.
   The declared outline in the *output* grows from the shrunk value toward (but ≤) MAX exactly as much as
   the FT inflation demands — which is legal and correct (QA A25: outline includes FT-inflated blocks).

**Net effect:** on tight packings, finalize trades a little reclaimed area for mismatch=0 (mismatch
penalty `1e6×` dwarfs the area delta), satisfying the P1 acceptance criterion. On loose packings it's a
no-op (mismatch already 0).

### 1.6 Validation & output — use the **declared** outline, MAX as ceiling (refine)

Today's split-brain, verified:

- `validateSolution` (`validate.cpp:68-114`) checks containment and thirds against `problem.outline_w/h`
  = **MAX** (`:76`, and `locSegment` `:18-27`). With a shrunk declared outline this is **wrong**: an
  edge block flush to the shrunk right border sits at `x = W < MAX_w`, so `near(r.right(), MAX_w)` at
  `validate.cpp:36` **fails** ⇒ false "edge location violation". This *must* change or every shrunk
  solution is reported invalid.
- `writeCfg` (`io.cpp:197-223`) emits `usedOutline(problem, placement, routing)` (`:202`) = MAX-clamped
  bbox incl. channels. It already includes channels (good) but is clamped to MAX and derived from bbox,
  not the declared `(W,H)`.

**Decision:**
- Add a declared outline to the validation/output contract. Cleanest: `validateSolution` and `writeCfg`
  read `placement.outline_w/h` (the stamped declared outline) as the outline to validate/emit against,
  falling back to `problem.outline_w/h` (MAX) when unset (0). Thread it into `locSegment`/
  `satisfiesEdgeLocation` by building a scoped `Problem` (outline = declared) — same pattern as
  `exactScore`. Containment (`validate.cpp:76`) checks `≤ declared + kEps` (declared ≤ MAX so still
  within MAX). Fixed-dim and overlap checks unchanged.
- `writeCfg`: emit `OUTLINE declared_w declared_h`. The emitted outline must equal what `exactScore`
  scored (QA A25 / P0 item 3 unified them via `usedOutline`); after P1 both read the same declared
  `(W,H)` stamped on the placement, so they stay unified **by construction** (extend the P0
  `testOutlineConsistencyEvalVsCfg` regression to the shrunk case).
- **Invariant to assert:** declared `(W,H) ≤ MAX` and `≥` bbox-of(all rects ∪ channels). The second half
  means channels extracted at the declared outline never extend past it (they can't — `extractChannels`
  clamps to `problem.outline_*` `:51-52,:65`, which will be the declared outline in the scoped route).

---

## 2. Exact new / changed interfaces

### `model.hpp` (`struct Placement`, ~:59-62)
```cpp
struct Placement {
    std::vector<Rect> rects;
    std::vector<double> aspect;
    double outline_w = 0.0;   // NEW: declared outline; 0 => not shrink-wrapped, use problem/MAX
    double outline_h = 0.0;   // NEW
};
```
Adding trailing fields keeps all existing aggregate/`placementKey` code compiling; `placementKey`
(`search.cpp:60-69`) must be extended (§3.3).

### `placement_engine.hpp` (new declarations)
```cpp
struct OutlineFit { double w = 0.0; double h = 0.0; bool legal = true; };

// Exact: re-legalizes edge blocks against the shrunk outline (grow-until-legal loop). Mutates
// placement (edge rects + outline_w/h). Called at the end of decode() and by exactScore.
OutlineFit shrinkWrapOutline(const Problem& problem, Placement& placement, int max_grow_steps = 6);

// Cheap: O(n) lower-bound arithmetic + thirds feasibility only, no edge relegalization loop.
// Does NOT mutate placement; returns the estimated (W,H) for cheapScore.
OutlineFit shrinkWrapEstimate(const Problem& problem, const Placement& placement);
```

### `eval.cpp` (internal)
- `cheapScore`: use `shrinkWrapEstimate` (or read stamped `placement.outline_w/h`) for `outline_area`;
  add `channelMarginProxy` (static helper); keep out-of-outline penalty vs MAX.
- `exactScore`: scope routing to declared `(W,H)` (stamped or `shrinkWrapOutline`); optional 1-step
  corrective grow on open/overflow.
- New static helper `channelMarginProxy(problem, placement, W, H)` and (elite path) an
  `outlineSweep(problem, placement)` used by the search elite pass.

### `search.cpp`
- `placementKey` (`:60-69`): append declared outline (§3.3).
- `finalizeFeedthrough` (`:71-103`): guard change (commit on mismatch decrease) + let inflated re-decode
  grow outline toward MAX (§1.5).
- Elite pass (`:193-209`): call the outline sweep on each of the ≤16 candidates + compacted variants.

### `validate.cpp` / `validate.hpp`
- `validateSolution` builds a scoped `Problem` from `placement.outline_w/h` (fallback MAX) for
  thirds/containment. No signature change needed (it already takes `placement`).

### `io.cpp`
- `writeCfg` emits `placement.outline_w/h` (fallback to `usedOutline(...)`/MAX). `usedOutline` overloads
  can stay for the fallback path.

No public header signature changes except the two new `placement_engine.hpp` free functions and the two
`Placement` fields. Namespace `efgr`, 4-space, `lowerCamelCase`, `k`-constants — all per `CLAUDE.md`.

---

## 3. Failure-mode analysis

### 3.1 Shrink can never emit overlap / open / outline-violation
- **Outline violation:** `shrinkWrapOutline` picks `W ≥ W_core` and `H ≥ H_core` and re-anchors edge
  blocks *inside* `[0,W]×[0,H]`; every rect ⊆ declared outline by construction, and declared ≤ MAX, so
  containment vs MAX (`validate.cpp:76`) and vs declared both hold. The grow loop only *increases* room.
- **Overlap:** shrink reuses `legalizeEdgeBlocks`/`firstLegalEdgeSlot`/`collidesWithPlaced`
  (`placement_engine.cpp:188-208,126-186,265-272`) at the scoped outline; any `(W,H)` where an edge
  block cannot find a non-colliding legal slot is rejected and grown. At `W=MAX` the original decode's
  legalization is reproduced (already overlap-free), so the loop always terminates on a legal state. Core
  blocks are never moved ⇒ core∩core overlap is exactly as `decode` left it (already legal).
- **Open route:** shrink changes only the outline+edge anchors; routing is re-run by `exactScore`
  inside the declared outline. If tighter channels open a route, the corrective grow step (§1.4) or the
  elite sweep grows the outline until routable; final output only commits states with `open_routes==0`
  (enforced by `main.cpp:95` exit code and the finalize/elite acceptance). The cheap proxy's channel
  margin biases SA away from un-routable tightness in the first place.

### 3.2 `placementKey` cache — **must include the declared outline**
`exact_cache` (`search.cpp:125-139`) keys on `placementKey` (rects only, `:60-69`). Post-P1, **routing
happens inside the declared outline**, so two placements with identical rects but different declared
`(W,H)` route differently (different channels, overflow, WL) ⇒ **stale cache hits would corrupt scores.**
Fix: append `llround(outline_w*1000)`/`outline_h*1000` to the key. Since `decode` stamps a deterministic
outline per rects, in the common path the outline is a function of the rects and the extra key
components are redundant-but-harmless; they become load-bearing for the elite sweep and finalize, which
deliberately route the *same rects* at *different outlines* — those must be distinct cache entries.
This is a required change, cheap, and preserves determinism.

### 3.3 Hybrid seeding
`runHybridPlacementSearch` (`search.cpp:217-239`) seeds bstar from sp's `best_placement` via
`seedFromPlacement` (`placement_engine.cpp:376-393,497-516`), which reads only `rects[·].x/.y` to derive
orderings — it ignores `outline_w/h`. So the new `Placement` fields are simply dropped at the seam,
which is **correct**: the bstar leg re-decodes and re-shrink-wraps from scratch against MAX. No change
needed; the fields being ignored here is the desired behavior (each leg shrink-wraps independently, like
each leg finalizes independently per the existing comment at `:229-230`).

### 3.4 `compactTowardOrigin`
`compactTowardOrigin` (`search.cpp:19-56`) pushes non-edge blocks toward origin against
`problem.outline_w/h` = MAX (`:42,:52`), then the elite pass exact-scores the compacted variant
(`:202-207`). Compaction *reduces* `W_core/H_core`, so the subsequent `shrinkWrapOutline` inside
`exactScore` finds a *smaller* legal outline — compaction and shrink-wrap are synergistic. One subtlety:
`compactTowardOrigin` clamps to MAX (`:42,:52`), not to the declared outline; that's fine because it
runs *before* shrink-wrap and only makes blocks more compact. The compacted placement enters
`exactScore` with `outline_w/h==0` (compaction doesn't stamp them) ⇒ `exactScore` recomputes via
`shrinkWrapOutline`. No conflict, but the elite-pass code must ensure the compacted variant gets a fresh
shrink-wrap (it does, through `exactScore`).

### 3.5 Degenerate cases
- **No edge blocks** (e.g. a case with only MACRO/SOFT): shrink-wrap = pure `max(bbox, 1)`, trivially
  legal, big area win. `legalizeEdgeBlocks` iterates an empty edge list — safe.
- **All blocks edge / corner-locked to opposite corners** forcing `W≈H≈MAX`: shrink finds no room, loop
  hits MAX, output = today's behavior. No regression.
- **Empty channels / degenerate outline:** `extractChannels` fallback (`routing.cpp:80-82`) still fires
  at the declared outline. `usedOutline` clamps `≥1` (`eval.cpp:14-15,31-32`); mirror that `≥1` floor in
  `shrinkWrap*` to avoid a zero-area outline.

---

## 4. Task breakdown (TDD-able, ordered to land green independently)

Each task builds warning-clean (`-Wall -Wextra -pedantic`) and passes existing `test/run_*.sh` before
counting as done (per `CLAUDE.md` verification gates).

1. **T1 — `Placement.outline_w/h` fields + key + writeCfg passthrough (plumbing, tiny).**
   Files: `model.hpp`, `search.cpp` (`placementKey`), `io.cpp` (`writeCfg` reads stamped fallback MAX).
   Default 0 ⇒ behavior identical to today. Test: existing `run_case00.sh` still green; add a unit that
   two `Placement`s differing only in `outline_w` produce different `placementKey`. ~40 LOC.

2. **T2 — `shrinkWrapEstimate` (cheap, geometry-only) + unit tests.** Files: `placement_engine.hpp/.cpp`.
   Pure lower-bound arithmetic (§1.2.1) + thirds feasibility (§1.2.2), no mutation. Test
   (`test/`, ad-hoc `.cpp` like the others): case0 decoded placement ⇒ estimate `< MAX_area` and
   `≥ Σ core bbox`; a corner-locked synthetic ⇒ estimate `== MAX`. ~80 LOC + test.

3. **T3 — `shrinkWrapOutline` (exact, edge re-anchor + grow loop) + legality tests.** Files:
   `placement_engine.hpp/.cpp`. Reuses `legalizeEdgeBlocks` at scoped outline (§1.2.3-4). Test:
   for case0-4 decoded placements, assert output passes `validateSolution` **against the declared
   outline** (no overlap/thirds/containment failure) and declared ≤ MAX. Fuzz a few seeds. ~120 LOC + test.

4. **T4 — wire shrink into `decode` (stamp `outline_w/h`) + `cheapScore` area gradient.** Files:
   `placement_engine.cpp` (call `shrinkWrapOutline` at end of both decodes), `eval.cpp` (`cheapScore`
   reads stamped outline; add `channelMarginProxy`; keep containment vs MAX). Test: `cheapScore` on two
   placements with different core extents now yields different `outline_area` (gradient exists); Spearman
   spot-check that tighter packing ⇒ lower `outline_area`. Confirm out-of-outline penalty unchanged.
   ~90 LOC + test. **First task that moves Cost.**

5. **T5 — `exactScore` routes inside declared outline + 1-step corrective grow.** Files: `eval.cpp`.
   Scope routing to declared `(W,H)`; if `open_routes>0`/severe overflow, one grow+reroute. Test:
   case0-4 exact eval on a shrunk placement ⇒ `open_routes==0` after corrective grow; outline_area strictly
   `<` MAX where util<70%. ~70 LOC + test.

6. **T6 — validation & output use declared outline.** Files: `validate.cpp` (scoped Problem for
   thirds/containment), `io.cpp` (emit declared). Test: extend `testOutlineConsistencyEvalVsCfg` — scored
   outline == emitted outline on a shrunk case; a shrunk case0 solution validates OK (no false edge-loc
   violation). ~60 LOC. **Unblocks: shrunk solutions now validate & emit correctly end-to-end.**

7. **T7 — elite-pass outline sweep (5–7 trials on ≤16+compacted candidates).** Files: `search.cpp`
   (elite loop), `eval.cpp` (`outlineSweep` helper). Deterministic scale/margin trials; pick min-Cost.
   Test: per-case Cost table (all 5 public cases × sp/bstar/hybrid × fixed seed) shows Cost **≤** the
   pre-P1 baseline in every row, strictly `<` where util<70%. ~90 LOC + regression harness update.

8. **T8 — `finalizeFeedthrough` grow-to-absorb-FT + guard fix.** Files: `search.cpp`. Commit on
   mismatch-decrease; let inflated re-decode shrink-wrap grow toward MAX (§1.5). Test: case1/hybrid and
   case3/hybrid reach `feedthrough_mismatch==0` (the P1 acceptance criterion) with `open_routes==0`,
   `validation=ok`. ~50 LOC + regression assertion. **Closes the P1 residual defect.**

Ordering rationale: T1-T3 are inert plumbing/pure functions (green trivially). T4 turns on the cheap
gradient; T5-T6 make exact/validate/output consistent so shrunk states are legal end-to-end; T7 harvests
the area win via the sweep; T8 fixes the FT residual that only surfaces once outlines shrink. Each lands
without breaking the prior gate because shrink is Pareto-safe (falls back to MAX).

---

## 5. Expected cost impact (rough, from util numbers)

`Cost = OutlineArea + α·WL`, α=0.2 public. From the strategy doc's P0 sweep (Appendix) and util:

| Case | util | MAX outline_area | rough achievable area | area Δ | notes |
|------|------|------------------|-----------------------|--------|-------|
| case0 | 55.6% | 6.47M | ~4.5–5.0M (util→~75%) | **−1.5 to −2.0M** | dominated by area (α·WL≈2.3M for sp); ~15–25% Cost drop |
| case1 | — | 7.6–7.8M | tight (overflow-bound) | small | overflow-limited; sweep must not over-shrink; modest area win |
| case2 | — | 42.0M | area is huge vs WL·α | **large abs** | but WL·α (≈42M) comparable; net Cost dominated by overflow penalty today — real win needs P2/P3 |
| case3 | — | 23.4M | headroom exists | moderate | hybrid best; FT residual fixed by T8 removes 1e6×36988 penalty term |
| case4 | — | 37.8M | — | moderate | large; overflow-heavy, gated by routability |

**Key caveats:** (a) the *pure* area win is largest on case0 (loose, WL-competitive) — expect a
double-digit-% Cost reduction there. (b) On overflow-heavy cases (1-4 all show 5-figure
`channel_overflow` in the sweep), shrinking makes channels scarcer, so the *net* Cost win is gated by
P2/P3 (cost-driven routing + demand-driven spacing) landing — P1 alone may show only modest net change
there because the graded overflow penalty rises as area falls; the elite sweep's job is to stop at the
Cost-minimal outline, not the smallest. (c) T8 independently removes the `1e6×mismatch` term from
case1/case3 hybrid (mismatch 14479 / 36988 → 0) — that alone is a large reported-score improvement on
those two rows regardless of area.

Realistic honest claim: **case0 ~15–25% Cost drop; cases 1-4 area shrinks but net Cost gain is
partly latent until P2/P3.** The infrastructure (outline as a variable) is the prerequisite for all of it.

---

## 6. Risks & open questions

- **R1 — shrink vs overflow coupling.** Smaller outline ⇒ scarcer channels ⇒ overflow penalty up. If the
  elite sweep's overflow-vs-area trade uses the *guessed* graded penalty (real formula unpublished, QA A1),
  it may over- or under-shrink relative to the true judge. *Mitigation:* sweep minimizes a conservative
  objective and always keeps `open_routes==0`; final answers stay 0-overflow where achievable (strategy
  §5). Revisit weights when P3 (demand spacing) lands — it makes shrink *safe* by sizing channels to
  demand.
- **R2 — cheap↔exact rank divergence.** The crude `channelMarginProxy` may mis-rank tight packings vs
  exact routing, so SA chases outlines the exact router can't fill. *Mitigation:* measure Spearman(cheap,
  exact) on sampled candidates per case (strategy §3.2 recipe) before trusting the proxy; tune the margin
  coefficient. Keep the proxy crude in v1 and lean on the elite sweep for the real trade.
- **R3 — grow loop non-termination / thrash.** If `shrinkWrapOutline`'s per-axis grow oscillates
  (bumping W frees an R block but a thirds constraint then needs H, etc.). *Mitigation:* the loop is
  monotone-increasing toward MAX with a hard `max_grow_steps` cap and MAX as a guaranteed-legal
  fixpoint; never decreases within a call. Assert legality at exit; fall back to MAX.
- **R4 — determinism.** All new trials (sweep, grow) use fixed trial points / no RNG; `placementKey`
  extended so the cache stays correct. Must keep single-thread `--seed` determinism (gate: byte-identical
  `.cfg` for fixed seed across two runs).
- **Q1 — declared-outline aspect freedom.** Shrink-wrap picks the tight envelope; is a non-tight aspect
  (wider+shorter) ever Cost-better because it shortens dominant-bundle WL or eases channel demand? Left as
  a P4 single-gene experiment; v1 takes the tight envelope.
- **Q2 — A3.1 vs A3.2 channel-capacity reading (strategy §5).** If band-disjoint capacity is legal,
  effective channel capacity is higher ⇒ we can shrink further safely. Conservative sum-reading used;
  revisit only with an official evaluator.
- **Q3 — thirds on shrunk outline with a block longer than a third (A19/A20).** `positiveOverlap`
  already handles "1D overlap only" but confirm on cases where a re-anchored mid-side block spans two
  shrunk thirds — add a targeted regression in T3.
