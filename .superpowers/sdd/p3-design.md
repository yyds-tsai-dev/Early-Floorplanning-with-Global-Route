# P3 — Demand-Driven Channel Re-Spacing (Constraint-Graph Repair)

Design deliverable for roadmap item P3 (`docs/research/2026-07-06-cost-reduction-strategy.md`
§2.4, §4 item 10, and the **P1 addendum 2026-07-07 post-T8.1**).
Doc-only; no source edited. All line numbers verified against `fable5` HEAD at authoring time
(commit `b269688`, tree with P1 T1-T8 already landed).

---

## 0. The problem this must solve (grounded)

`Cost = OutlineArea + α·TotalWireLength`, plus a graded `1e6 × channel_overflow` penalty inside
search. The **P1 addendum measured finding** is the binding constraint:

> `anchorAtOutline` re-anchors *edge blocks only* — the core packing SA produced under the new
> area gradient stays frozen, so larger declared outlines add *peripheral* whitespace, not channel
> capacity *between* congested cores. With overflow weighted 1e6, tight-core candidates therefore
> score worse than the loose-core solutions pre-P1 SA used to produce, on overflow-bound cases (1-4).

Concretely: P1 gave the SA an area gradient and shrank outline_area up to −33% (`case4/sp`), but the
P1 results table (design doc lines 236-252) shows `channel_overflow` in the **1e5** range on every
one of cases 1-4 — e.g. `case1/sp` overflow 134405, `case4/bstar` 232158. At `1e6×` that is a
`1.3e11`-`2.3e11` penalty term dwarfing both the ~1e7 outline area and the α·WL. The whole P1 area
win is *latent* until those overflow terms come down, and the addendum proves peripheral growth
**cannot** bring them down. Only widening the **deficient strips between blocks** can.

That is exactly what P3 does: take a routed placement, read the per-strip directional deficits the
router already computed, and push blocks apart *just enough* on *exactly* the strips that carry
routes, then re-anchor / re-route. Everything else stays put.

### 0.1 The demand signal already exists in `RoutingResult`

`routeProblem` (`routing.cpp:86-237`) hands back, per channel, everything P3 needs:

- `ch.horizontal_usage` — nets routed *horizontally* through the channel (`routing.cpp:190`), checked
  against `hcap = 25 × ch.rect.h` (`routing.cpp:222`).
- `ch.vertical_usage` — nets routed *vertically* through the channel (`routing.cpp:191`), checked
  against `vcap = 25 × ch.rect.w` (`routing.cpp:223`).
- overflow accrual is `horizontal_usage − hcap` and `vertical_usage − vcap` when positive
  (`routing.cpp:224-225`).

A channel is a **vertical slice** `[x0,x1] × [y0,y1]` (uncovered y-interval between block x-edges,
`extractChannels` `routing.cpp:48-84`). Its geometry: `ch.rect.w = x1−x0` (a *horizontal* gap between
whatever blocks bound it on the left/right), `ch.rect.h = y1−y0` (a *vertical* gap between whatever
blocks bound it below/above).

**The capacity-vs-geometry mapping is the whole ballgame** (this is the A3 per-direction subtlety the
brief flags):

| deficit | formula | to relieve it, grow… | which is a… |
|---|---|---|---|
| horizontal overflow `Δh = horizontal_usage − 25·h` | wires run **left↔right** across the slice | the channel **height** `h` (need `h ≥ horizontal_usage/25`) | **vertical** gap between the block *below* and the block *above* the channel |
| vertical overflow `Δv = vertical_usage − 25·w` | wires run **up↔down** across the slice | the channel **width** `w` (need `w ≥ vertical_usage/25`) | **horizontal** gap between the block *left* and the block *right* of the channel |

So — counter-intuitively — a **horizontal** routing deficit is repaired by a **vertical** (y-axis)
block re-spacing, and vice versa. Getting this axis-crossing right is the single highest-risk
correctness item in P3 (§1.3, §4-R1). Everything downstream depends on it.

---

## 1. Constraint-graph formulation

### 1.1 What we build — two independent longest-path graphs, one per axis

Standard constraint-graph compaction, exactly the structure `SequencePairEngine::decode` already uses
internally (`placement_engine.cpp:422-451`: horizontal/vertical constraint lists + a
Bellman-Ford-style `x[to] = max(x[to], x[from]+weight)` relaxation). We reuse that *pattern* (not the
code — decode's graph comes from the sequence-pair; ours comes from current coordinates).

**Nodes.** One per block (`problem.blocks.size()` of them) plus four virtual outline walls
(`LEFT@x=0`, `RIGHT@x=W`, `BOTTOM@y=0`, `TOP@y=H`). Wall nodes anchor the graph and carry the
outline≤MAX constraint (§2.3).

**H-graph edges (x-coordinates).** For each ordered pair of blocks `(a,b)` that are **x-adjacent**
(a is left of b, they y-overlap, and no third block lies strictly between them in x within their
y-overlap band — i.e. b is a's immediate right neighbor), add edge `a → b` with

```
min_gap_x(a,b) = a.w  +  required_horizontal_gap(a,b)
```

The `a.w` term is the ordinary non-overlap constraint (b.x ≥ a.x + a.w). The extra
`required_horizontal_gap` is the **demand term**: the width the channel(s) sitting in the strip
between a's right edge and b's left edge must have to carry their *vertical* usage without overflow.

**V-graph edges (y-coordinates).** Symmetric: for each **y-adjacent** ordered pair `(a,b)` (a below b,
x-overlap, immediate neighbor), edge `a → b` with

```
min_gap_y(a,b) = a.h  +  required_vertical_gap(a,b)
```

where `required_vertical_gap` is the **height** the channel(s) between a's top and b's bottom need to
carry their *horizontal* usage.

Edge/wall interaction: `LEFT → a` with weight 0 (a.x ≥ 0), `a → RIGHT` with weight `a.w`
(a.right() ≤ W), symmetric for BOTTOM/TOP. Edge blocks are handled specially (§2.1).

### 1.2 Mapping per-CHANNEL overflow back to PAIRWISE gaps — the core algorithm

A channel spans `[x0,x1] × [y0,y1]`. It is bounded:
- on the **left** by the block(s) whose `right() == x0` and whose y-range overlaps `[y0,y1]`;
- on the **right** by the block(s) whose `x == x1` and y-overlaps `[y0,y1]`;
- **below** by the block whose `top() == y0` (or the BOTTOM wall if `y0==0`);
- **above** by the block whose `y == y1` (or the TOP wall if `y1==H`).

`extractChannels` builds these from the x-edge sort and the covered-interval scan
(`routing.cpp:59-79`), so the bounding blocks are recoverable by a geometric re-scan after routing (we
do **not** need to modify `extractChannels` to emit them — a post-hoc `channelBounds()` helper that
re-derives left/right/below/above block indices from the final rects + channel rect is enough, and
keeps `routing.cpp` untouched; §2.4).

**Vertical-usage → widen the width → charge the left/right pair.**
If a channel has `vertical_usage = V` and current width `w`, its required width is `w_req = V/25`.
The width deficit is `Δw = max(0, w_req − w)`. This deficit is a **lower bound on the horizontal gap**
between its left-bounding block(s) and its right-bounding block(s). For each (left block `L`, right
block `R`) pair bounding this channel, contribute a candidate H-edge demand:

```
required_horizontal_gap(L,R)  ⩾  w_req    (the FULL required width, not just Δw)
```

We charge the **full** `w_req`, not the incremental `Δw`, as the edge's demand term, because the
constraint graph sets the *absolute* gap `R.x − L.right() ≥ required_horizontal_gap`; the current
width is already `x1−x0` and the graph's longest-path will simply hold it if `w_req ≤ current`, or
widen it if `w_req > current`. (Charging `Δw` on top of the current gap would double-count — the graph
works in absolute coordinates, not deltas.)

**Horizontal-usage → widen the height → charge the below/above pair.** Symmetric: `h_req = H_usage/25`
becomes `required_vertical_gap(below, above) ⩾ h_req` in the V-graph.

**Multiple channels per strip.** A single vertical strip `[x0,x1]` is sliced into several channels
stacked in y (the covered-interval scan, `routing.cpp:70-78`). Different channels in the same strip
can bound *different* block pairs (as y changes, the left/right neighbors change). Each channel
contributes its own edge demand to the specific pair it geometrically separates. When two channels
happen to charge the *same* (L,R) pair (e.g. both bounded by the same two tall blocks), take the
**max** of their `w_req` — the graph edge weight is a single scalar and must satisfy the worst channel
on that pair (each channel is a physically distinct region; the pair's gap must simply be wide enough
for the widest demand among the channels it separates).

### 1.3 The axis-crossing invariant (highest-risk item — write the regression first)

Restating for emphasis because it inverts intuition and the A3 semantics make it easy to get backwards:

- `horizontal_usage` (wires crossing the slice **horizontally**, dx>0 at `routing.cpp:190`) is
  capacity-limited by channel **height** `h` → relieved by a **V-graph** (y) re-spacing of the
  **below/above** blocks.
- `vertical_usage` (wires crossing **vertically**, dy>0 at `routing.cpp:191`) is capacity-limited by
  channel **width** `w` → relieved by an **H-graph** (x) re-spacing of the **left/right** blocks.

`ch.usage = max(horizontal_usage, vertical_usage)` (`routing.cpp:192`) is a reporting aggregate — P3
must read the two directional fields **separately**, never `usage`. The T1 regression (§4) asserts a
hand-built single-channel case: a channel carrying only vertical_usage widens its width and leaves its
height alone, and vice versa. If this test is green, the rest is plumbing.

### 1.4 A3.1-vs-A3.2 caution (from strategy doc §5, Q2)

Current model sums all occupied segments per direction into one `horizontal_usage`/`vertical_usage`
scalar (the conservative reading). P3 inherits that reading unchanged: `w_req = vertical_usage/25`
uses the same summed quantity the overflow penalty uses, so P3's demanded gap and the scored overflow
are **consistent by construction** — we never size a channel to a capacity model the scorer disagrees
with. If an official evaluator later validates band-disjoint capacity (A3.2), both the router and P3
would change together; P3 introduces no new capacity assumption.

---

## 2. Longest-path re-coordinate — keeping legality

### 2.1 Edge blocks: re-anchored *after* re-spacing, NOT graph nodes

**Decision:** core (non-edge) blocks are the graph nodes; **edge blocks are excluded from the
constraint graph** and re-anchored afterward via the existing P1 machinery.

Rationale, grounded in the code:
- Edge blocks are pinned to outline borders by `placeEdge`/`legalizeEdgeBlocks`
  (`placement_engine.cpp:31-62,188-208`), and their legality (flush + thirds + corner-AND) is a
  function of the *final* outline `(W,H)` — which P3 changes. Putting them in the graph as movable
  nodes would fight the anchor logic. `shrinkWrapOutline`/`anchorAtOutline` already solve
  "re-anchor edge blocks to a given outline, legally" (`placement_engine.cpp:903-943`), so we reuse it.
- This mirrors P1's own split exactly: `shrinkWrapOutline` never moves core rects
  (`placement_engine.cpp` comment :55-56) and only re-legalizes edges. P3 re-coordinates the core,
  then calls the same commit primitive to re-anchor edges at the (possibly grown) outline.

So the P3 pipeline per iteration is:
1. Build H/V graphs over **core** blocks + walls, with demand-sized min-gaps.
2. Longest-path relax both graphs → new core `(x,y)`.
3. Compute the resulting core bbox → candidate `(W,H) = shrink-wrap of the re-spaced core`, clamped
   to MAX (§2.3).
4. `anchorAtOutline(scoped@(W,H), placement)` to re-anchor edges legally at that outline
   (`placement_engine.cpp:937`). If it returns `legal=false`, grow the deficient axis and retry
   (bounded, §2.3) — reusing `shrinkWrapOutline`'s monotone-grow-to-MAX fallback contract.
5. `placementLegalAt` / `validateSolution` gate before accepting (§2.5).

### 2.2 Core relative order — frozen from current coordinates (compaction-style)

**Decision:** the graph's edge *set* (who is left-of / below whom) is derived **from the current
routed placement's coordinates**, exactly like `compactTowardOrigin` derives its sweep order from
`x+y` (`search.cpp:27-32`) and its blockers from current geometry (`search.cpp:38-55`). We do **not**
re-derive order from the SA topology (sequence-pair / B*-tree) — P3 operates on a *decoded, routed*
`Placement`, downstream of the engine, and must be engine-agnostic (it runs on elites and in finalize,
which already hold `Placement`s, not `PlacementState`s).

Adjacency test (immediate-neighbor, per axis), reusing the exact predicate style already in
`compactTowardOrigin` (`search.cpp:41,51`) and `placeWithContour`'s `maxTopOverlap`
(`placement_engine.cpp:274-286`):
- `a` is an **x-predecessor** of `b` iff `a.right() ≤ b.x + kEps` and their y-ranges overlap
  (`axisOverlap`, `search.cpp:16-18`). For the *immediate* neighbor, keep only the `a` with the
  greatest `a.right()` among all x-predecessors sharing that y-band (transitive edges are redundant
  for longest-path but harmless; pruning them keeps the graph small and the demand attribution clean).

Because order is frozen, **relative order cannot flip within a single re-coordinate pass**: the
longest-path relaxation only ever *increases* coordinates along edges (it enforces `b.x ≥ a.x+gap`),
so an x-predecessor stays a predecessor. This is the key oscillation-safety property (§2.6) and the
same reason `compactTowardOrigin`'s directional sweep is stable.

### 2.3 Respect MAX; grow only as needed

The re-spaced core bbox may exceed the current declared outline (that is the *point* — we are widening
strips). Clamp handling:
- Target `(W,H)` = `max(re-spaced core extent, edge width/height needs, 1)` per axis, then
  `min(·, MAX)`. This is exactly `shrinkWrapEstimate`'s lower-bound arithmetic
  (`placement_engine.cpp:680-699,890-901`) applied to the *re-spaced* rects.
- If demand pushes the core extent **past MAX** on an axis, we cannot satisfy all gaps. This is the
  **infeasible-demand** failure mode (§3.1): clamp to MAX, accept **partial widening** (relieve as
  much overflow as fits), and let the residual overflow remain — it is a graded penalty, not a
  failure. See §3.1 for why we prefer proportional distribution of the available slack over a naive
  left-to-right per-block clamp.

### 2.4 Where the bounding-block recovery lives

Add a small pure helper (in a new `src/respace.cpp` TU — §2.7 — importing only `model.hpp`/`routing`):

```cpp
struct ChannelBounds { int left=-1, right=-1, below=-1, above=-1; };  // block indices or -1 for wall
std::vector<ChannelBounds> channelBounds(const Problem&, const Placement&, const std::vector<Channel>&);
```

It re-scans: for each channel rect, find blocks with `right()≈x0` & y-overlap (left), `x≈x1` &
y-overlap (right), `top()≈y0` & x-overlap (below), `y≈y1` & x-overlap (above), using the same
`kEps`/`positiveOverlap` conventions already in `routing.cpp:11-16` and
`placement_engine.cpp:80-82`. No change to `Channel`/`RoutingResult` structs, so no ripple through
the cache key or serialization.

### 2.5 Never emit illegal — reuse P1's gates verbatim

Every P3 candidate goes through the identical legality funnel P1 already established:
`placementLegalAt(scoped, placement, W, H)` (`placement_engine.cpp:753-769`, containment + no-overlap
+ edge-location) as the geometric pre-filter, then `validateSolution(problem, placement, routing)`
(`validate.cpp:68-122`) after re-routing, before commit. `finalizeFeedthrough` already does exactly
this (`search.cpp:198-204,227`) — P3's commit gate is the same `tryCommit` discipline. A P3 candidate
that fails either gate is discarded and the pre-P3 placement is kept. **Pareto-safe by construction:
P3 can only replace `best` with a strictly-better *legal* candidate**, mirroring
`outlineSweepBetter`/`acceptFeedthroughPaydown` (`search.cpp:104-119`).

### 2.6 Oscillation with routing — damping + fixed point, ≤3 iterations

The route↔respace loop can oscillate: re-spacing changes channel geometry → re-routing changes usage →
demand changes → re-spacing moves again. Convergence strategy:

1. **Monotone widening within a call.** Each re-coordinate pass only *increases* gaps (longest-path
   relax never shrinks a coordinate below its constraint). We **never shrink** a strip inside the P3
   loop even if re-routing reports lower usage on it — a strip that was widened stays widened for the
   duration of the repair. This removes the oscillation source entirely: the sequence of outlines is
   monotone non-decreasing per axis, bounded by MAX, so it converges.
2. **Damping on the demand delta.** When re-routing on the widened placement reports *new* (possibly
   different) usage, apply the new demand with a damping factor: `gap_target = current_gap + β·(w_req −
   current_gap)` for `β ∈ (0,1]`, `β = 1.0` for v1 (full step — the monotone-widen rule already
   prevents thrash; keep β as a tuning knob for R2). Re-routing can *reduce* usage on a strip we
   widened (wires reroute through the now-roomier channel elsewhere); the monotone rule ignores that
   reduction, trading a little extra area for guaranteed termination.
3. **Fixed-point / early exit.** Stop when either (a) `channel_overflow` reaches 0, (b) an iteration
   produces no new widening (all `w_req ≤ current gap`), or (c) the 3-iteration cap. Take the
   **best legal** candidate seen across iterations by `outlineSweepBetter` (lowest Cost with
   `open_routes==0`), not necessarily the last — a later iteration that grew the outline for little
   overflow relief may score worse.

### 2.7 Determinism

All graph construction is a deterministic function of the placement coordinates (sorted by index for
ties, like every existing tie-break in the repo — `search.cpp:32`,
`placement_engine.cpp:200,724`). No RNG anywhere in P3. The route↔respace loop is a pure function of
`(problem, placement, routing)`. Fixed `--seed` determinism is preserved (byte-identical `.cfg`), the
same gate P1 held (p1-design §R4).

---

## 3. Failure-mode analysis

### 3.1 Infeasible demand (required total width > MAX) → partial widening + residual overflow

If `Σ gaps + Σ block widths > MAX_w` on an axis, not all demand fits. Handling:
- The longest-path relax computes the *unclamped* ideal `x` coordinates. Let `x_span` = resulting core
  right extent. If `x_span > MAX_w`, **proportionally distribute the available slack**: the excess is
  `x_span − MAX_w`; shrink each *demand gap* (not block widths, which are fixed) by the same fraction
  so the total fits MAX. This keeps *all* strips partially widened rather than fully serving the
  first few and starving the rest (naive left-to-right clamp would do the latter). Residual overflow
  is then spread, and the graded penalty reflects a genuinely best-effort placement.
- **Never illegal:** after distribution, `x` coords are re-clamped to `[0, MAX_w − w]` and the
  frozen order guarantees no overlap (predecessors stay left). `placementLegalAt` confirms.
- This is the case-2/case-4 reality (huge net counts, `channel_overflow ~1e5-2e5`): P3 will *reduce*
  overflow, likely not to 0, and the P1-shrunk area is partly given back. The elite sweep
  (`outlineSweep`) still picks the Cost-minimal point on the resulting area↔overflow trade.

### 3.2 Relative-order flips

Cannot happen *within* a pass (§2.2: monotone relax preserves order). *Across* route↔respace
iterations the order is **re-derived** from the new coordinates — but since coordinates only grew
monotonically, x-predecessors stay x-predecessors, so the derived order is stable too. The only way
order changes is if re-routing somehow moved a block, which it does not (routing reads placement,
never writes it — `routeProblem` takes `const Placement&`). So order is invariant across the whole P3
call. No flip-handling code needed; assert it in the T-regression (order derived at iter k == order at
iter 0).

### 3.3 Soft-block resize as an alternative to moving — OUT OF SCOPE (defer to P4 LR-sizing)

A narrow channel could also be relieved by *shrinking* an adjacent soft block (freeing horizontal room
without moving anything), or feedthrough could absorb nets instead of widening the channel at all
(that is P2's cost-driven router). P3 deliberately does **only** re-spacing (move blocks apart). Soft
w/h re-optimization on the fixed topology is the Young-Chu LR sizing lever, explicitly **P4 item 13**
(strategy doc §4). P3 must not touch soft aspect — it reads `placement.rects` dims as given. Noted so
the reviewer does not expect P3 to reshape blocks.

### 3.4 Feedthrough interaction

Re-spacing widens channels; the router's pass-1 feedthrough fallback (`routing.cpp:129,146`) may then
become unnecessary (routes prefer the now-adequate channel), which *reduces* `soft_feedthrough_nets`
and thus FT-required area — synergistic with `finalizeFeedthrough`. Conversely, if P3 runs *after*
FT inflation (inside finalize), the inflated soft-block dims are already in `placement.rects` and P3
re-spaces around them correctly (it reads current dims). Order of operations in finalize: inflate →
re-decode → **P3 respace** → re-route → commit (§5). No conflict; P3 is dimension-agnostic.

### 3.5 Degenerate cases

- **No overflow anywhere** (`channel_overflow == 0`, all `w_req ≤ current`): P3 is a no-op — the first
  re-coordinate pass finds every gap already satisfied, produces identical coordinates, exits at
  §2.6(b). Guard: early-return if `routing.channel_overflow <= kEps && routing.open_routes == 0`.
- **No channels** (`extractChannels` fallback single full-outline channel, `routing.cpp:80-82`): one
  channel spanning everything; its bounds are all four walls; demand charges wall→wall gaps, i.e. the
  outline itself must be ≥ `usage/25`. Degenerates to "grow the outline," bounded by MAX. Safe.
- **All-edge / corner-locked cases** (no core nodes): graph has only walls; nothing to re-space; P3
  no-ops. `anchorAtOutline` at unchanged outline. Safe.
- **`≥1` floor:** mirror `usedOutline`/`shrinkWrap*`'s `max(1.0, ·)` clamp (`eval.cpp:18-19,35-36`)
  so a zero-area strip never produces a zero/negative gap.

### 3.6 Cache correctness

P3 produces a new `Placement` with different rects **and** a different stamped `outline_w/h`. Both are
already in `placementKey` (`search.cpp:84-95`, includes rects + `outline_w×outline_h` since P1 T1). So
the exact-cache correctly treats a re-spaced placement as a distinct entry — no stale-hit risk, the
P1 §3.2 fix already covers us. P3 adds nothing to the key.

---

## 4. Task breakdown (TDD-able, each lands green independently, Pareto-safe)

Each task builds warning-clean (`-Wall -Wextra -pedantic`) and passes existing `test/run_*.sh` before
counting done (per `CLAUDE.md`). New tests go in `test/regression_p3_20260707.cpp` +
`test/run_p3_regressions.sh`, compiled directly like the P1 harness (`test/run_p1_regressions.sh:7-11`
links `routing eval io validate placement_engine search floorplan` + the new `respace.cpp`). Ordering
mirrors P1: inert pure functions first, then wire-in, then the sweep/finalize hooks.

**T1 — `channelBounds` recovery + directional-demand mapping (pure, the crux).**
Files: new `src/respace.hpp/.cpp`. Implement `channelBounds()` (§2.4) and a pure `channelDemand()`
that, given a channel + its usage, returns `(w_req, h_req)` and which block-pair/axis to charge
(§1.2-1.3). **No placement mutation.** Tests: hand-built single-channel placements asserting the
**axis-crossing invariant** (§1.3) — vertical_usage charges the L/R horizontal gap, horizontal_usage
charges the below/above vertical gap; a wall-bounded channel charges the wall. This is the
highest-risk logic; test it in isolation first. ~90 LOC + test.

**T2 — H/V constraint-graph build + longest-path relax over core blocks (pure).**
Files: `src/respace.cpp`. Build adjacency from current coords (§2.2), inject demand min-gaps from T1,
relax (reuse decode's `x[to]=max(...)` pattern, `placement_engine.cpp:448-451`). Returns new core
`(x,y)` **without** touching edges/outline. Tests: a 3-block row with a demanded gap widens exactly
that gap, leaves others; frozen-order invariant holds (§3.2); no-overflow input → identical coords
(§3.5). ~110 LOC + test.

**T3 — full `respaceToDemand(problem, placement, routing) -> bool` single-pass, edge re-anchor + legality gate.**
Files: `src/respace.cpp`. Pipeline §2.1 steps 1-5 for **one** pass: relax → shrink-wrap target `(W,H)`
→ `anchorAtOutline` (grow-on-fail) → `placementLegalAt`. Mutates `placement` in place, returns whether
it changed anything legal. Tests: on case0-4 decoded+routed placements, output passes
`validateSolution` against its declared outline, declared ≤ MAX, and `channel_overflow` (after a fresh
`routeProblem`) is `≤` the input's on every case (never worsens overflow). ~120 LOC + test.

**T4 — route↔respace fixed-point loop (≤3 iters, monotone-widen, damping, best-legal pick).**
Files: `src/respace.cpp`. Wrap T3 in the §2.6 loop with the `outlineSweepBetter` best-pick and the
early-exits. Tests: an overflow-heavy synthetic converges to lower overflow within 3 iters and
terminates; a no-overflow case exits in 1 iter unchanged; the infeasible-demand synthetic (§3.1)
clamps to MAX, stays legal, reduces (not necessarily zeroes) overflow. ~70 LOC + test.

**T5 — hook into the elite pass (after `outlineSweep`, before finalize).**
Files: `src/search.cpp` (the `top_candidates` loop, `search.cpp:345-370`). For each elite (and its
compacted variant), after the outline sweep picks a candidate, run `respaceToDemand` on it and
exact-score the result; adopt via `outlineSweepBetter` only if strictly better + legal. Reuse the
`exactCached` lambda so cache/determinism hold. Tests: per-case Cost table (5 cases × sp/bstar/hybrid
× fixed seed) shows Cost **≤** pre-P3 baseline in every row, strictly `<` on the overflow-bound
rows (1-4) where the P1 addendum stalled. ~60 LOC + regression harness update. **First task that
moves Cost on cases 1-4.**

**T6 — hook into `finalizeFeedthrough` as a targeted repair operator.**
Files: `src/search.cpp` (`finalizeFeedthrough`, `search.cpp:190-255`). After the inflated re-decode,
call `respaceToDemand` before/around the compacted-variant `tryCommit` so FT inflation that pushed a
strip into overflow gets widened rather than penalized. Gate through the same `acceptFeedthroughPaydown`
+ `validateSolution`. Tests: a case where FT inflation causes channel overflow now pays it down via
re-spacing; `open_routes==0`, `validation=ok`, mismatch==0 preserved. ~40 LOC + assertion.

**T7 — full sweep + doc appendix (the P3 measurement, mirrors P1's appendix).**
Files: docs only + `test/run_p3_regressions.sh` final wiring. Run the 5×3 `--iters 2000 --seed 1`
sweep, record score/outline_area/wirelength/channel_overflow/mismatch/validation before (P1 HEAD) vs
after (P3), append to the strategy doc. Assert the acceptance criterion: **overflow → ~0 on the cases
where total demand fits MAX, and strictly reduced everywhere; net Cost ≤ P1 on every row, strictly <
on cases 1-4.** ~doc + harness. **Closes the P1 addendum's open dependency.**

Ordering rationale: T1-T2 are pure and green trivially (the axis-crossing invariant is nailed down in
isolation before any wiring). T3-T4 assemble the operator behind the same legality gates P1 uses, so
each is Pareto-safe standalone. T5 harvests the win on the elite pass; T6 extends it into finalize;
T7 measures. Every task falls back to "no change" if re-spacing finds nothing legal to improve — the
`respaceToDemand -> bool` return + `outlineSweepBetter` gate make regression impossible.

---

## 5. Where it hooks — the integration decision

**Decision: elite pass + finalize, NOT the SA hot loop.**

- **Elite pass (T5, primary hook), `search.cpp:345-370`:** run `respaceToDemand` on each of the ≤16
  cheap elites and their compacted variants, *after* `outlineSweep` (which picks a good outline)
  and *before* `finalizeFeedthrough`. Sequence per elite: `outlineSweep → respaceToDemand →
  exactCached → outlineSweepBetter gate`. This is where quality matters, the candidate set is tiny
  (≤32), and each candidate already pays for exact routing — the added cost is ≤3 extra route calls
  per elite (bounded, deterministic). This directly attacks the P1 addendum: the sweep can't relieve
  inter-core overflow (it only re-anchors edges); re-spacing can.
  - **Order vs `outlineSweep`:** respace *after* the sweep, because respace needs a *routed* placement
    to read demand from, and the sweep's chosen outline gives the tightest legal starting point. But
    respace may then grow the outline (widening strips) — so its output is re-scored fresh; the two
    are complementary (sweep = outline exploration, respace = internal strip sizing).

- **Finalize (T6, secondary hook), `search.cpp:190-255`:** as a repair operator when FT inflation
  overflows a channel. Same paydown gate.

- **SA hot loop — explicitly rejected.** `cheapScore` (`eval.cpp:103`) runs on every mutation and must
  stay O(n) with no routing. `respaceToDemand` requires a *routed* placement (it reads
  `RoutingResult`), i.e. an exact eval, which the hot loop only does periodically
  (`search.cpp:332`). Running it per-iteration would multiply routing cost by the iteration count —
  blowing the runtime budget (strategy §2.6: routing dominates). The cheap proxy already biases SA
  away from over-tight packings via `channelMarginProxy` (`eval.cpp:87-99`); P3 does the *exact*
  repair only on the handful of candidates that survive to exact evaluation.

- **`placementKey` cache & determinism:** P3 outputs are keyed correctly (§3.6); it uses the same
  `exactCached` lambda (`search.cpp:278-291`) so repeated evals dedupe; no RNG (§2.7). Determinism
  gate identical to P1.

---

## 6. Expected impact per case (from the P1 appendix numbers)

Reading the P1 results table (design doc lines 236-252, the pre-P3 baseline) and the addendum. The
`1e6 × channel_overflow` term dominates Cost on cases 1-4; P3's job is to collapse it.

| Case | best-engine (P1) | outline_area | channel_overflow | `1e6×overflow` term | P3 expectation |
|---|---|---|---|---|---|
| case0 | bstar | 6.46e6 | **0** | 0 | **no-op** (already overflow-free; §3.5 early-return). P1 already won here. |
| case1 | bstar | 7.55e6 | 38875 | ~3.9e10 | overflow is essentially the whole score (7.55e6 area is 0.02% of 3.9e10). If demand fits MAX (case1 has headroom), **overflow → ~0**, score collapses from 3.9e10 toward ~5e7. Largest single win. |
| case2 | hybrid | 3.76e7 | 104514 | ~1.0e11 | huge net counts; likely **partial** relief (§3.1 infeasible-demand) — overflow drops substantially but maybe not to 0; area given back some. Net Cost down meaningfully but gated by whether total demand ≤ MAX. |
| case3 | bstar | 2.32e7 | 97422 | ~9.7e10 | moderate headroom; expect **large overflow reduction**, possibly to ~0. sp variant (1.74e7 area, overflow 108465) is tighter-core — prime respace target. |
| case4 | sp | 2.53e7 | 215317 | ~2.15e11 | worst overflow, largest net counts; **partial relief** most likely (demand may exceed MAX). Still a large absolute Cost drop from any overflow reduction. |

**Which rows' `1e6×overflow` terms vanish:** case1 and case3 are the strong candidates for
overflow→~0 (moderate demand, headroom exists between shrunk area and MAX). case2 and case4 are
demand-bound — expect *proportional partial* relief (§3.1), a big absolute win but a nonzero residual.
case0 is untouched (already 0).

**Area cost of the win:** P3 gives back some of P1's area shrink (widening strips grows the outline
back toward MAX). But the trade is overwhelmingly favorable: on case1, trading (say) +1e6 area to
remove a 3.9e10 overflow term is a ~4-order-of-magnitude net win. The `outlineSweepBetter` /
best-legal pick (§2.6.3) ensures P3 only commits when the *net* Cost improves, so on any case where
the area regrowth would cost more than the overflow relief (won't happen at 1e6 weighting, but
guaranteed by the gate), P3 self-limits.

**Honest claim:** P3 is the lever that converts P1's latent area win into realized Cost on cases 1-4.
Expect case1/case3 to drop by ~3 orders of magnitude (overflow-term collapse), case2/case4 large but
partial, case0 unchanged. This is precisely the "missing piece" the P1 addendum names.

---

## 7. Risks & open questions

- **R1 — axis-crossing sign error (highest risk).** Getting horizontal↔vertical demand mapping
  backwards would *widen the wrong axis* and never relieve overflow. *Mitigation:* T1 tests the
  invariant in isolation before any integration (§1.3, §4-T1); the mapping is a two-row table, easy to
  assert.
- **R2 — route↔respace non-convergence / thrash.** *Mitigation:* monotone-widen rule (§2.6(1)) makes
  the outline sequence monotone non-decreasing, bounded by MAX ⇒ guaranteed termination in ≤3 iters;
  best-legal pick guards against a late iteration scoring worse. β damping knob reserved if needed.
- **R3 — infeasible demand on case2/case4.** Total required width may exceed MAX ⇒ can't zero
  overflow. *Mitigation:* proportional slack distribution (§3.1) gives best-effort partial relief;
  residual is a graded penalty, not a failure; final answer stays 0-*open-route* (the only hard fail).
  This is the same "conservative, keep 0-overflow where achievable" posture as P1 (strategy §5).
- **R4 — respace vs the real (unpublished) overflow formula.** We size gaps to `usage/25`, matching
  the *scorer's own* capacity model (`routing.cpp:222-223`) ⇒ consistent by construction (§1.4). If
  the official formula differs, both scorer and P3 change together.
- **R5 — determinism under the extra route calls.** *Mitigation:* no RNG, index tie-breaks, same
  `exactCached` cache; byte-identical `.cfg` gate per fixed seed (§2.7).
- **Q1 — should P3 also *shrink* over-wide strips?** Widening-only (§2.6(1)) is safe but leaves
  whitespace where P1 over-shrank then P3 over-grew. A shrink pass risks oscillation. *v1 = widen
  only*; the elite `outlineSweep`'s m=0 rung already recovers tight outlines where routable, so the
  two together approximate the two-sided optimum. Revisit if profiling shows leftover slack.
- **Q2 — interaction with P2 (cost-driven routing).** P2 chooses FT-vs-channel per net; P3 sizes the
  channels P2 decided to use. They compose (P2 first picks routes, P3 sizes the resulting demand). If
  P2 lands first, P3 reads its `RoutingResult` unchanged. No ordering conflict; P3 is downstream of
  whatever router produced the `RoutingResult`.
- **Q3 — transitive vs immediate-neighbor edges.** §2.2 keeps only immediate neighbors for a clean
  demand attribution; a pathological non-slicing arrangement could leave a gap unconstrained. *v1
  accepts this* (the demand still lands on *some* separating pair); add a targeted regression if a
  case exhibits it.
