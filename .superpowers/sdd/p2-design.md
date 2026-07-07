# P2 — Cost-Driven Routing (FT-vs-Channel Marginal-Area Router)

Design deliverable for roadmap item **P2** (`docs/research/2026-07-06-cost-reduction-strategy.md`
§2.3, §4 items 7-9, and the **P3 appendix "Honest findings" 2026-07-07** — the axis-locked rows).
Doc-only; **no source edited**. Line numbers verified against `fable5` HEAD at authoring time
(commit `b269688`, tree with P0/P1/P3 already landed).

---

## 0. The problem this must solve (grounded)

`Cost = OutlineArea + α·TotalWireLength`, plus graded penalties inside search:
`1e6 × channel_overflow`, `1e6 × feedthrough_mismatch`, `1e15 × open_routes` (`eval.cpp:142-144`).

**The binding constraint is the P3 "Honest findings" measurement** (strategy doc line 319):

> those layouts are **axis-locked** — the core packing's x-extent is already pinned at
> `problem.outline_w` (MAX), so `relaxToDemand`'s H-pass demand is scaled to ~0 by the MAX secant
> clamp before it can do anything… Raw `channel_overflow` is not reducible by spacing alone on those
> rows — there is no free axis left to widen into. … The strategic next phase for the remaining
> axis-locked rows is **P2 (cost-driven routing)**: steering flow off the locked axis and into
> feedthrough via Dijkstra edge weights that price channel widening against FT tier cost.

Concretely, from the P3 appendix table (strategy doc lines 285-299), **10 of 15 rows did not move**
under P3 — case0 (3 rows, already overflow-free, a legitimate no-op), and the 7 axis-locked rows
**case2/sp, case3/{sp,bstar,hybrid}, case4/{sp,bstar,hybrid}**. Their `channel_overflow` sits at
**1e5-2e5** (e.g. case4/sp 215317, case3/sp 87181, case2/sp 112436), so the `1e6×` term alone is
**1.1e11-2.15e11** — utterly dominating the ~1e7 area and the α·WL. P3 cannot help because spacing
can only widen an axis that has slack, and these have none.

**P2's mission:** the *current* router is feasibility-driven — it finds *a* path (channels first,
then soft-block FT as a fallback), never the *cheapest* one. When a channel is x-locked and
saturated, the router keeps piling nets onto it (paying graded overflow it cannot avoid) instead of
diverting them through a soft block's feedthrough, which QA §2.3 proves is **2.4× cheaper in area**
on the official example (3600 nets: channel = 87,940 µm² vs FT through BLK07 @40% tier = 36,063 µm²).
P2 rebuilds routing as **cost-driven**: every Dijkstra edge weight is denominated in Cost units
(µm² of area-equivalent + α·µm·nets of wirelength), so the shortest path *is* the min-ΔCost route,
and an overflowing x-locked channel edge becomes so expensive that flow reroutes through FT on its
own.

---

## 1. Current router — exactly what P2 replaces (grounded, `routing.cpp`)

`routeProblem` (`routing.cpp:86-237`) does, per bundle chunk (`routeChunk`, `routing.cpp:124-206`):

- **Two feasibility passes** (`routing.cpp:129-130`): `pass 0` allows only channels + the two
  endpoint blocks (`allowed`, `routing.cpp:108-112`); `pass 1` additionally allows traversal through
  *soft* blocks (feedthrough). The loop stops at the first pass that finds a path
  (`&& route.node_path.empty()`, `routing.cpp:129`).
- **Edge weights** (`routing.cpp:145-152`):
  - base `manhattan(center_u, center_v)` (`routing.cpp:145`) — center-to-center distance, **not** a
    Cost-unit quantity (it double-counts distance already inside blocks and is not multiplied by α or
    nets in the *cost*, only in the eventual guiding-point WL, `routing.cpp:178`).
  - soft-block penalty `nets * 0.05` (`routing.cpp:146`) — a flat heuristic, **not** FT tier area.
  - channel congestion `10000 * nets * (h_usage/hcap + v_usage/vcap)` (`routing.cpp:147-151`) — a
    static snapshot of usage *at the moment this chunk routes*, in arbitrary units (10000), unrelated
    to the `1e6×overflow` the scorer actually charges.
- **Usage accrual** after committing the path (`routing.cpp:181-193`): per intermediate channel node,
  `horizontal_usage += nets` if the guiding-point segment has dx>0, `vertical_usage += nets` if dy>0;
  per intermediate soft node, `soft_feedthrough_nets[node] += nets`.
- **Fixed chunking** (`routing.cpp:209-210`): `split_count = clamp(ceil(nets/1000), 1, 8)`, then nets
  split as evenly as possible across chunks (`routing.cpp:211-215`).
- **Overflow accounting** (`routing.cpp:221-226`): `channel_overflow += max(0, h_usage − 25·h) +
  max(0, v_usage − 25·w)` per channel.
- **FT mismatch** (`routing.cpp:227-235`): per soft block, `required = ftRequiredArea(base,
  ftExtend(nets, ftTierRate(...)))`; if the placed area is smaller, `feedthrough_mismatch += required
  − actual`.

**The four structural defects P2 fixes:**

1. **Weights are not Cost.** The `10000`, `0.05`, and center-manhattan terms have no relation to
   `1e6×overflow`, FT tier area, or `α×WL`. The router minimizes a fiction.
2. **Congestion is a snapshot, not marginal.** The channel term reads usage *before* this chunk is
   added, so it never charges the *marginal* overflow this chunk creates (nets 1..N of the same
   bundle each see the same pre-chunk usage). Ordering-dependent, path-order-fragile.
3. **FT is a last resort, not a priced alternative.** Pass structure forbids FT in pass 0; a channel
   route always wins even when FT would be 2.4× cheaper. The `0.05` penalty is far below any real
   tier delta, so once pass 1 is reached FT is essentially free — the opposite failure.
4. **No negotiation.** Bundles route once, in `problem.bundles` order (`routing.cpp:208`), first-come
   grabs the cheap channels, later bundles overflow. No rip-up/reroute.

---

## 2. Design goal — routing as min-ΔCost pathfinding

Replace the two-pass feasibility router with a **single cost-driven Dijkstra** whose edge weights are
all in **Cost units** (µm² area-equivalent + α·µm·nets WL), wrapped in a **bounded PathFinder-lite
negotiation loop** (2-3 rip-up/reroute rounds with history cost), with **adaptive chunking** that
splits at capacity bottlenecks. The old two-pass router is retained verbatim as a **feasibility
fallback** — if the cost-driven router leaves any route open, we fall back so we **never emit an open
route** (the only hard fail besides overlap/outline/format, strategy doc line 43).

**The central identity that makes this work:** the scorer already charges `α·WL + 1e6·overflow +
1e6·FT_mismatch`. If each edge's weight equals the *marginal* contribution to those exact terms that
traversing it adds, then Dijkstra's shortest path is the path that adds the least Cost, and the sum
over all routed bundles is (approximately, modulo path interaction) the Cost-minimal routing. The
router and the scorer stop disagreeing — which is the same "consistent by construction" discipline P3
established (`p3-design.md` §1.4).

---

## 3. Cost-unit edge weights (Q1 — the crux)

All weights below are in **Cost units**. Denote the chunk's net count `n`, the per-testcase weight
`α = problem.alpha`, and two named area-equivalent weights (§7 sensitivity):

```
constexpr double kOverflowWeight       = 1e6;   // MUST equal eval.cpp:143's overflow multiplier
constexpr double kFeedthroughWeight    = 1e6;   // MUST equal eval.cpp:144's FT-mismatch multiplier
```

Both are **named constants shared with `eval.cpp`** (move them to `model.hpp` so router and scorer
reference the same symbol — §7, R5). The router pricing an edge at a different weight than the scorer
charges is the single most dangerous bug class here.

### 3.1 Block-adjacency edges (block ↔ block, port-to-port, zero channel)

QA A31 (strategy doc line 30): adjacent blocks connect **port-to-port with no channel and zero
wirelength** — the guiding-point HPWL over a 2-node path is empty (`routing.cpp:177` loops
`i+1<guides.size()`, and a 2-node path has 1 guide, so the WL sum is empty). This is **already
exploited** and must be preserved. Weight:

```
w_adj(u → v) = 0        // when both u,v are blocks and one is source, the other target (direct abut)
```

For a block that is *not* an endpoint being crossed as feedthrough, see §3.3 (only soft blocks are
crossable; hard/edge blocks are never interior nodes, enforced by `allowed`, `routing.cpp:108-112`).
The **port-edge legality** filter (`endpointEdgeAllowed`, `routing.cpp:114-122`) is retained
unchanged — it is a hard feasibility gate on the endpoint edges, orthogonal to cost.

**Design note (WL for multi-hop channel routes).** For paths that *do* traverse channels, the WL is
the guiding-point HPWL × nets (`routing.cpp:172-179`). To make the Dijkstra path length actually
approximate `α·WL`, the base geometric term of each edge must be `α · n · Δ(guiding points)`, not the
raw center-manhattan. **Decision:** keep the base term as `α · n · manhattan(center_u, center_v)` as a
*monotone proxy* for the guiding-point WL during the search (the exact guiding-point WL is only known
after the full path is fixed, since a guide depends on the *overlap segment* of consecutive rects,
`routing.cpp:26-37`), but scale it by `α · n` so it is denominated in the same Cost units as the
area terms and trades off correctly against them. The final `route.wirelength` is still computed
exactly from guiding points (`routing.cpp:172-179`) — the α·n·center-manhattan is *only* the search
cost, and it is an admissible-ish lower-bound-flavored proxy (center distance ≥ ... no, not a true
lower bound, but monotone and cheap; §8-R2 flags the residual gap). This is a deliberate, documented
approximation: exactness of the *scored* WL is preserved; only the *routing decision* uses the proxy.

### 3.2 Channel edges — slack vs saturated (marginal widening burden)

A channel edge `u → v` where `v` is channel `c`. The chunk routes `n` nets through `c` in some
direction. The **marginal area burden** is the extra channel area the scorer will charge *because
this chunk pushed usage past capacity*. Per the A3 per-direction capacity model (`routing.cpp:222-223`,
`model.hpp:67-68`):

- A **horizontal** crossing (dx>0 at the guide, `routing.cpp:190`) consumes `h_usage` capacity, capped
  at `hcap = 25 · c.rect.h`. Adding `n` horizontal nets moves `h_usage → h_usage + n`.
- A **vertical** crossing (dy>0, `routing.cpp:191`) consumes `v_usage`, capped at `vcap = 25 · c.rect.w`.

The marginal overflow this chunk creates in the consumed direction is:

```
marginal_overflow(c, dir, n) = max(0, usage_dir + n − cap_dir) − max(0, usage_dir − cap_dir)
```

i.e. the *increase* in the `overflow` quantity the scorer sums (`routing.cpp:224-225`) attributable to
these `n` nets. Its Cost is `kOverflowWeight × marginal_overflow`. **This is the key correction over
the current snapshot term**: the current router charges `10000·n·(usage/cap)` regardless of whether
the channel is already over capacity (`routing.cpp:151`), so it *deters* using an empty channel and
*under-charges* an already-saturated one. The marginal formulation gives **exactly the right slope**:

- **Slack channel** (`usage_dir + n ≤ cap_dir`): `marginal_overflow = 0` ⇒ **channel edge is free**
  (only the α·WL base term applies). This is correct — using spare capacity costs nothing.
- **Saturated channel** (`usage_dir ≥ cap_dir`): every one of the `n` nets is pure overflow ⇒
  `marginal_overflow = n` ⇒ Cost `= kOverflowWeight · n = 1e6·n`. **Enormous** — steers flow away.
- **Partially saturating** (`usage_dir < cap_dir < usage_dir + n`): only the nets past cap are
  charged: `marginal_overflow = usage_dir + n − cap_dir`.

**The axis-locked reality (P2's mission), made precise.** The brief says "widening an x-locked
channel is IMPOSSIBLE — cost should approach the graded-overflow penalty." In this model a
saturated channel's edge already costs `1e6·n` *unconditionally*, whether or not the axis can be
widened. That is exactly right: P2 does **not** try to distinguish "widenable" from "locked" channels
at routing time (that is P3's job, run *after* routing). At routing time, an over-capacity crossing
is simply expensive, and the FT alternative (§3.3) — whose area *can always be paid* by inflating the
soft block up to MAX — becomes the cheaper path whenever the FT tier delta `< 1e6·n`. So on the
axis-locked rows, saturated x-channels price themselves out and flow migrates to FT automatically,
with no special-casing of the lock. (P3 then sizes whatever channels P2 *did* use; the two compose,
`p3-design.md` §7-Q2.)

**Why marginal and not average.** Averaging (`overflow_after / n`) would let the first net of a chunk
subsidize the last; marginal charges each net the true incremental penalty, which is what a per-net
routing decision should see. Within a single Dijkstra we cannot re-price mid-path (the whole chunk of
`n` takes one edge), so `n` is the chunk size — **this is the direct motivation for adaptive chunking**
(§5): smaller chunks let the marginal price update between chunks, approximating true per-net
negotiation.

### 3.3 Soft-block feedthrough edges — incremental tier delta (Q1, the subtle part)

A soft-block FT edge `u → v` where `v` is soft block `b`, crossed by `n` nets. The scorer charges
`kFeedthroughWeight × feedthrough_mismatch`, and `feedthrough_mismatch` for block `b` is `max(0,
required_area(total_nets_b) − actual_placed_area_b)` (`routing.cpp:227-234`). The **marginal FT cost**
of routing `n` more nets through `b` is the *increase in required area* those nets cause, but **only
the part that is not already covered by the block's slack area** (placed area minus current required).

Let `acc` = nets already accumulated through `b` in this routing pass (`soft_feedthrough_nets[b]` so
far), `base = b.ft_base_area` (`routing.cpp:231`), and `A(m) = ftRequiredArea(base, ftExtend(m,
ftTierRate(b, m)))` (`ft.hpp:26`, the exact scored required-area function). The marginal *required-area*
delta is:

```
Δreq(b, acc, n) = A(acc + n) − A(acc)
```

But the scorer only charges the part of `required` that exceeds the **placed** area
(`routing.cpp:234`). So the marginal *mismatch* (what actually costs Cost) is:

```
placed_b = round2(rects[b].w · rects[b].h)                       // routing.cpp:233
mismatch_before = max(0, A(acc)     − placed_b)
mismatch_after  = max(0, A(acc + n) − placed_b)
marginal_ft_mismatch(b, acc, n) = mismatch_after − mismatch_before
w_ft(u → v) = α·n·manhattan(...) + kFeedthroughWeight · marginal_ft_mismatch(b, acc, n)
```

**This directly answers "how to price crossing a block whose accumulated nets sit just below a tier
boundary."** `A(m)` is a step-ish function: `ftTierRate` jumps at 3000/6000/9000 nets (`ft.hpp:10-16`)
and `ftExtend` is `ceil(...)` (`ft.hpp:19-21`), so `A(acc+n) − A(acc)` naturally captures a tier
jump — if `acc = 2900` and `n = 200`, the crossing pushes `b` from the ≤3000 tier into the ≤6000
tier, and `Δreq` reflects the full tier-2-rate area increase, not a linear approximation. **This is
strictly more accurate than the flat `nets·0.05`** (`routing.cpp:146`) and is the same math the scorer
uses, so router and scorer agree.

**Two crucial subtleties:**

1. **Slack absorption.** If the soft block was placed with area *larger* than its base (P1 shrink-wrap
   / soft-aspect can size it up, or a prior FT inflation), `placed_b > A(acc)` means the first nets
   through it are **free** (they fit in existing slack) — `mismatch_after − mismatch_before = 0` until
   `A(acc+n)` exceeds `placed_b`. The `max(0, ·)` on both terms handles this exactly. This is a
   *feature*: it means P2 correctly treats a block with spare area as a cheap FT conduit, which is
   precisely the official example's logic (BLK07 already has area; the 36,063 µm² is the *marginal*
   inflation).
2. **`acc` must be per-pass mutable state read during Dijkstra.** The edge weight depends on how many
   nets *already* went through `b` this routing pass — a stateful, order-dependent quantity, exactly
   like the channel usage. Within one Dijkstra, `acc` is the pre-chunk `soft_feedthrough_nets[b]`
   (constant for the whole chunk, same treatment as channel usage in §3.2); across chunks it grows.
   This is why the chunk is the pricing granule and why negotiation (§4) re-prices between rounds.

**Determinism note:** `A(·)` reads only `problem.blocks[b]` (immutable) and `acc` (deterministic
accrual), so the weight is a pure function of `(placement, routing-state-so-far)`. No hidden state
leaks across `routeProblem` calls (the whole `RoutingResult` is constructed fresh each call —
`routing.cpp:87`), which is the cache-key invariant the brief demands (verified §6).

### 3.4 Units audit (everything in Cost units)

| Edge type | weight (Cost units) | grounds |
|---|---|---|
| block↔block direct abut (both endpoints) | `0` | QA A31, `routing.cpp:177` empty-WL |
| channel edge, slack | `α·n·manhattan(cu,cv)` | α·WL proxy only; overflow term 0 |
| channel edge, saturating | `α·n·manhattan(cu,cv) + 1e6·marginal_overflow` | §3.2; `eval.cpp:143` |
| soft-block FT edge | `α·n·manhattan(cu,cv) + 1e6·marginal_ft_mismatch` | §3.3; `eval.cpp:144`, `ft.hpp:26` |

The α·n·manhattan base is the *same* on all edges (a monotone WL proxy, §3.1); the area-equivalent
delta is what distinguishes channel-widening from FT. The router now compares "pay `1e6·marginal_overflow`
of channel area" vs "pay `1e6·marginal_ft_mismatch` of FT area" **in the same units the scorer uses**
— the 2.4× FT win on the official example falls out automatically.

---

## 4. Negotiated congestion — PathFinder-lite (Q2)

### 4.1 Where it lives, and the round structure

Negotiation lives **inside `routeProblem`**, wrapping the per-bundle chunk-routing loop
(`routing.cpp:208-219`) in an outer round loop:

```
for round in 0 .. R-1:                       // R = kNegotiationRounds (2 or 3)
    reset per-channel usage + soft_feedthrough_nets to 0     // rip up ALL routes
    order bundles by nets descending (stable, index tie-break)   // Q2, heaviest first
    for each bundle (adaptive chunks, §5):
        route chunk with cost-driven Dijkstra (§3), weights include history cost (§4.2)
        accrue usage / soft_feedthrough_nets from the committed path
    record total Cost of this round's routing
keep the best-Cost round's routing   // deterministic: strict-less, ties keep earlier round
```

**Rip-up = full reset each round** (not incremental single-net rip-up): at ≤50 blocks and a few
hundred chunks this is cheap, fully deterministic, and avoids the bookkeeping of tracking which nets
occupy which resource. The *history cost* (below) is what makes later rounds differ from earlier ones
despite the identical starting state — this is the essence of PathFinder.

### 4.2 History cost + decay

Maintain a per-resource **history multiplier** that persists *across rounds* (the only cross-round
state), initialised to 0 and bumped after each round for every resource that ended the round over
capacity:

```
// after each round, for each channel c and direction dir:
if usage_dir(c) > cap_dir(c):
    history[c][dir] += kHistoryBump · (usage_dir(c) − cap_dir(c)) / cap_dir(c)   // proportional to congestion
// symmetric per soft block b that ended over its placed-area slack:
if A(soft_feedthrough_nets[b]) > placed_b:
    history_ft[b] += kHistoryBump · (A(...) − placed_b) / placed_b
```

The history term is **added to the edge weight** during the next round's Dijkstra:

```
w_channel += kOverflowWeight · history[c][dir] · n      // congested-history surcharge
w_ft      += kFeedthroughWeight · history_ft[b] · n
```

so a channel that was overloaded last round is more expensive this round even before it fills up,
pushing the heavy bundles that grabbed it first to yield to alternatives. **Decay** is not strictly
needed at R≤3 (history only accumulates 2-3 times), but for robustness apply a light decay
`history *= kHistoryDecay` (`kHistoryDecay = 0.5`) at the *start* of each round so a resource that is
no longer congested relaxes back — standard PathFinder hygiene, prevents a one-round spike from
permanently poisoning a resource.

Named constants (all in `model.hpp`, §7): `kNegotiationRounds`, `kHistoryBump`, `kHistoryDecay`.

### 4.3 Interaction with per-direction capacity semantics

History is keyed **per channel per direction** (`history[c][H]` vs `history[c][V]`), matching the
A3 per-direction ledger (`routing.cpp:190-191`, `model.hpp:67-68`). A channel saturated horizontally
but slack vertically only surcharges *horizontal* crossings — correct, because a vertical crossing of
the same channel consumes the *other* (still-available) capacity. This is the same axis-crossing care
P3's §1.3 demanded; getting it backwards would surcharge the wrong direction (§8-R1).

### 4.4 Determinism (Q2)

- Bundle order is `nets` descending with **index tie-break** (stable `std::sort` on `(−nets, index)`),
  the same tie discipline as everywhere in the repo (`search.cpp:32`, `p3-design.md` §2.7).
- Dijkstra tie-breaks: the priority queue already compares `(dist, node)` (`routing.cpp:133-134`), so
  equal-distance nodes pop in node-index order — deterministic. **Keep this**; do not introduce any
  RNG.
- Best-round selection is strict-less on total Cost, keeping the earlier round on ties.
- No wall-clock, no threads inside routing. Byte-identical `.cfg` under fixed `--seed` (the same gate
  P1/P3 held).

---

## 5. Adaptive chunking (Q3)

### 5.1 Why the fixed `ceil(nets/1000)`, cap 8 is wrong

`routing.cpp:209-210` splits every bundle into at most 8 chunks of ~1000 nets each. Two problems:

1. **Granularity vs marginal pricing.** §3.2/§3.3 price a whole chunk of `n` nets at one edge weight
   using pre-chunk usage. A 1000-net chunk sees a *single* marginal price for all 1000 nets, so it
   cannot split "put 200 nets in the slack channel, 800 through FT." Finer chunks approximate true
   per-net negotiation (each chunk re-reads the updated usage/acc).
2. **Bottleneck-blindness.** A bundle whose cheapest path has a channel with only 300 nets of slack
   should split so ~300 nets take that channel and the rest divert — the fixed 1000-net split cannot
   express this.

### 5.2 Bottleneck-driven splitting algorithm

Route the bundle **greedily, chunk by chunk, re-pricing after each**, splitting at the tightest
capacity along the current cheapest path:

```
remaining = bundle.nets
chunks_emitted = 0
while remaining > 0 and chunks_emitted < kMaxChunks:
    // 1. find the current min-cost path for a probe of `remaining` nets (§3 weights, current usage)
    path = dijkstra(bundle, remaining, current_usage)          // cost-driven, §3
    // 2. find the tightest slack along that path (how many nets fit before the FIRST edge saturates)
    slack = min over channel edges e in path of remaining_slack(e, dir)   // = cap_dir − usage_dir, ≥0
            (soft FT edges: nets until the next tier boundary of that block, = next_tier_threshold − acc)
    // 3. chunk size = clamp(slack, kMinChunk, remaining); if slack<=0 the whole path is already
    //    saturated so take a full min-cost chunk of size min(remaining, kChunkCeil) and accept the
    //    graded overflow (no cheaper split exists)
    n = (slack > 0) ? clamp(slack, kMinChunk, remaining) : min(remaining, kChunkCeil)
    route chunk of n along `path`, accrue usage, emit PATH, remaining -= n, chunks_emitted++
```

This splits **exactly at the bottleneck**: fill a channel to capacity, then the next chunk's Dijkstra
sees it saturated (§3.2 makes its edge cost `1e6·n`) and naturally diverts to the next-cheapest
resource (another channel or FT). No fixed 1000-net granule.

### 5.3 Upper bound on chunks (runtime, Q3)

`kMaxChunks` per bundle bounds the Dijkstra count. Set `kMaxChunks = 16` (double the old cap of 8):
adaptive splitting needs a few more chunks than the old scheme to express bottleneck-fills, but must
stay bounded. `kMinChunk` (e.g. `max(1, nets/64)`) prevents pathological 1-net chunks. Total Dijkstra
runs per `routeProblem` call ≈ `R rounds × Σ_bundles chunks_b ≤ R × (#bundles × kMaxChunks)`. At
≤50 blocks, ≤~50 bundles (n(n−1)/2 max but sparse), R=3, kMaxChunks=16 ⇒ ≤ ~2400 Dijkstras, each over
≤ (50 blocks + few-hundred channels) nodes — **sub-millisecond each**, so a full `routeProblem` is
still well under the "exact routing dominates but is µs-ms-scale at n<50" budget (strategy §2.6). The
old router did ≤ `#bundles × 8` Dijkstras with no rounds; P2 is ~`R × 2×` that = ~6× — still trivial
against the 2-hour, 12-case budget.

### 5.4 PATH output format constraint (Q3)

`writeCfg` emits **one `PATH` line per non-open route** (`io.cpp:224-228`: `PATH <nets> <obj> <edge>
…`). Each chunk is one `Route` (`routing.cpp:216-217`) ⇒ one PATH line. There is **no spec limit on
path count** (nets ≤ 500k is the only stated bound, strategy doc line 41; QA A4/A6 explicitly permit
arbitrary bundle splitting with different edges per net). So emitting up to `kMaxChunks` PATH lines
per bundle is spec-legal. The only cost is output size, negligible at these counts. **No constraint
blocks adaptive chunking.**

---

## 6. Cache-key correctness (brief constraint — verify no hidden state leaks)

The brief demands: "exact cache keying unaffected (routing is a pure function of placement+outline —
verify no new hidden state leaks across calls)." Verification:

- `routeProblem` constructs a **fresh** `RoutingResult` per call (`routing.cpp:87`) and reads only
  `problem` (immutable) + `placement` (the argument). History cost (§4.2), usage, and `acc` are all
  **round-local / call-local** — declared inside `routeProblem`, never `static`, never global. So
  `routeProblem(problem, placement)` remains a **pure function** of its arguments.
- `placementKey` (`search.cpp:85-96`) keys on rects + stamped outline. P2 changes *how* a placement
  is routed but not *what a placement is*, so the key is unchanged and correct — the same placement
  always routes identically (determinism, §4.4), so a cache hit is still valid.
- **The one thing to check in review:** the negotiation loop's best-round selection must be a pure
  function (no RNG, deterministic tie-break) — verified §4.4. If any `static` or thread-local sneaks
  into history storage, the cache would return stale routings for identical placements; a regression
  must assert `routeProblem(p) == routeProblem(p)` bit-for-bit across two calls (§9-T7).

**No cache change is needed.** P2 is transparent to the cache by construction, exactly as the brief
requires.

---

## 7. Named constants & sensitivity to the unknown overflow formula (Q5, R5)

The judge's overflow/FT deduction formula is **unpublished** (strategy §5, QA A1 says only
"proportional"). Our `1e6` weights are guesses. P2 must be **robust to reweighting**:

- **All weights are named constants in `model.hpp`** (shared by router and scorer):
  `kOverflowWeight`, `kFeedthroughWeight` (these two **must stay equal to** `eval.cpp:143-144`'s
  multipliers — that is the router-scorer consistency invariant), plus router-only tuning knobs
  `kNegotiationRounds`, `kHistoryBump`, `kHistoryDecay`, `kMaxChunks`, `kMinChunk`, `kChunkCeil`.
- **The router's decisions are driven by the *ratio*** `marginal_overflow_cost / marginal_ft_cost`,
  both scaled by the same `1e6`. So if the judge's true weight is `W ≠ 1e6`, the router's channel-vs-FT
  *ranking* is unchanged as long as `kOverflowWeight == kFeedthroughWeight` (both cancel in the
  comparison). The router is thus **insensitive to the absolute overflow weight** for the FT-vs-channel
  decision — it only needs the two weights *equal to each other* and equal to the scorer's, which we
  control. **This is a strong robustness property**: getting the absolute magnitude wrong only mis-scales
  the α·WL-vs-area trade-off (which we *can* tune per-α), not the core FT/channel choice.
- **α is already per-testcase** (`problem.alpha`, `io.cpp`); the α·WL base term (§3.1) scales with it
  automatically. A quick per-case α-sensitivity check is cheap at n<50 (strategy §5) but out of P2 scope.
- **Sensitivity note for the writeup:** if the judge weights FT *more* leniently than channel overflow
  (e.g. FT is only a proportional area add, channel overflow is a harder deduction), we would want
  `kOverflowWeight > kFeedthroughWeight` to bias further toward FT. Keeping them as separate named
  constants lets a one-line change re-tune this the moment an official evaluator appears, without
  touching routing logic. **Recommend: ship with them equal (matching the scorer), document the knob.**

---

## 8. Expected impact on the 10 axis-locked rows (Q4)

Using the P3 appendix numbers (strategy doc lines 285-299) and the FT tier math (`ft.hpp`). The
axis-locked rows carry `channel_overflow` the router cannot avoid *by widening* — but P2 can convert
some of it to FT area if the FT tier cost is cheaper than the overflow penalty. **The decisive test
per channel-crossing is `marginal_ft_mismatch < marginal_overflow`** (both ×`1e6`), i.e. whether
diverting `n` nets to an adjacent soft block's feedthrough adds less required-area than leaving them
to overflow the locked channel.

**The FT-vs-overflow break-even (per net).** Overflow charges `1e6 · 1` per net over cap (§3.2). FT
charges `1e6 · Δreq/... ` — the *area* per net. From the official example (strategy §2.3): 3600 nets
through FT @40% tier adds 36,063 µm² ⇒ **~10 µm² of FT area per net**, vs channel overflow which is
charged as **1 "µm-of-width-deficit" per net** (`overflow` is in `usage − 25·dim` units, i.e.
nets-over-capacity, and the scorer multiplies by `1e6` directly — units of nets, not µm²). **Here is
the critical unit subtlety the review must confirm:** `channel_overflow` is in *nets-over-capacity*
(`routing.cpp:224`: `h_usage − hcap`, both in nets), while `feedthrough_mismatch` is in *µm²*
(`routing.cpp:234`: area difference). The `1e6` multiplier is applied to *both* (`eval.cpp:143-144`),
so **1 net of overflow costs the same as 1 µm² of FT mismatch** in the current scorer. That means:

```
divert n nets to FT is cheaper  ⟺  Δreq_area(FT, n) < n        [in the scorer's own units]
```

For the official BLK07 case, `Δreq ≈ 36063 µm²` for `n = 3600` ⇒ `36063 > 3600`, so *by the current
scorer's units* FT would look **more** expensive (10 µm² per net > 1 net-unit per net)! **This is a
scorer-unit inconsistency the P2 design surfaces** (§8.1) — the official *area* comparison (87,940 vs
36,063 µm²) says FT wins, but the scorer's mixed units (nets for overflow, µm² for FT) say the
opposite. **Resolving this is a P2 prerequisite finding, flagged below.**

### 8.1 BLOCKING FINDING — scorer unit mismatch must be resolved first

The channel-overflow term (`eval.cpp:143`, `routing.cpp:224`) charges **nets-over-capacity**, but the
FT-mismatch term (`eval.cpp:144`, `routing.cpp:234`) charges **µm² of area**. Multiplying both by the
same `1e6` compares apples (nets) to oranges (µm²). The official cost model (QA §2.3, PDF Fig 14/15)
compares them **both as area**: a channel carrying `V` nets needs width `V/25` µm and height = channel
span, so its *area* is `(V/25) · span`; FT's area is the tier inflation. **P2's whole premise — "FT is
2.4× cheaper area" — is only expressible if channel overflow is also priced as area**, i.e. the
overflow penalty should be `1e6 · (overflow_nets/25) · channel_span_along_flow` (µm²), not `1e6 ·
overflow_nets`. Until the channel-overflow term is area-denominated, the router cannot see the 2.4×
FT win the strategy doc promises, because the scorer itself doesn't.

**Recommendation (feeds Task 0 below):** as the first P2 task, **make `channel_overflow` area-denominated
in the scorer** so both penalty terms are in µm² and the `1e6` weight means "µm² of extra outline area,"
consistent with `Cost = OutlineArea + …`. Then the router's marginal-overflow edge weight (§3.2) uses
the same area formula, and FT-vs-channel is a true area comparison — the 2.4× win becomes visible to
both scorer and router simultaneously (the P3 "consistent by construction" discipline). This is a
scoring-fidelity change (P0-flavored) that P2 *depends on*; it must land first or the router optimizes
a unit-inconsistent objective. **This is the highest-value single finding in this design.**

### 8.2 Per-row expectation (after §8.1 lands)

| Case/engine (P1/P3 baseline) | overflow (nets) | axis-locked? | FT-convertible? | P2 expectation |
|---|---|---|---|---|
| case0/* | 0 | — | — | **no-op** (no overflow; router finds the same zero-cost channel routes). |
| case1/* | 30k-134k | partly (P3 moved it) | yes where soft blocks abut congested channels | further overflow→FT where a soft block sits on the flow; P2 + P3 compose. |
| case2/sp | 112436 | **locked** | depends on soft-block adjacency to the locked x-channels | **partial**: divert the nets whose flow passes an abuttable soft block into FT; residual stays. |
| case2/bstar,hybrid | 77-78k | partly | yes | P3 already helped; P2 diverts the residual through FT. |
| case3/{sp,bstar,hybrid} | 87k-110k | **locked** | **yes if soft blocks line the locked axis** | **prime target**: this is where "steer flow off the saturated x-channels into FT" pays. Expect meaningful overflow→FT conversion, bounded by how many soft blocks are adjacent to the flow. |
| case4/{sp,bstar,hybrid} | 215k-232k | **locked**, demand-bound | **partial** | worst overflow, largest nets; FT conversion helps but soft-block FT capacity (tier area up to MAX) is finite; expect a large absolute drop but a nonzero residual. |

**The honest bound:** P2 can only divert nets through soft blocks that *geometrically lie on a
cheaper-than-overflow path* between the bundle endpoints (the Dijkstra graph, `routing.cpp:98-106`,
only offers a soft block as an FT node if it *touches* the path's rects). If a case's congested
channels have no adjacent soft block (all hard/edge macros around the locked axis), P2 has no FT
alternative to offer and the overflow is genuinely irreducible by *routing* — only re-placement (P4
abutment moves) could help. So P2's ceiling is **case-topology-dependent**; it will move the rows
where soft blocks abut the locked flow (likely case3, case2/bstar-hybrid) and partially move case4,
and it cannot manufacture FT paths that the placement doesn't physically provide. This is measured in
Task 7, not assumed.

**Estimated FT-tier headroom (from `ft.hpp`).** A soft block with base area `S` can absorb nets up to
the tier where `ftRequiredArea` grows to fill available room-to-MAX. Since `ftExtend` grows ~linearly
in nets (`(nets/25)·rate/2` per side) and area grows quadratically in the side, a single soft block's
FT area cost accelerates — so P2 spreads FT across *multiple* soft blocks (the Dijkstra picks the
cheapest available each chunk, and history cost §4.2 pushes later chunks to *other* soft blocks once
one starts climbing tiers). This is exactly the negotiation loop's job.

---

## 9. Task breakdown (Q6 — TDD-able, Pareto-safe rollout)

Each task builds warning-clean (`-Wall -Wextra -pedantic`), passes existing `test/run_*.sh`, and lands
green independently. New tests → `test/regression_p2_20260707.cpp` + `test/run_p2_regressions.sh`,
compiled directly like the P1/P3 harnesses (link `routing eval io validate placement_engine search
floorplan respace`). **Rollout is behind a comparison gate**: the cost-driven router is added
*alongside* the existing two-pass router, and `routeProblem` picks the better (or falls back on open
routes), so every task is Pareto-safe — worst case, P2 reproduces the old router's result.

**T0 — area-denominate `channel_overflow` in the scorer (the §8.1 prerequisite).**
Files: `src/routing.cpp` (`:221-226`), `src/eval.cpp` (`:143`), `src/model.hpp` (add `kOverflowWeight`,
`kFeedthroughWeight`). Convert the overflow accrual from nets-over-capacity to µm² of extra channel
area (`(usage − cap)/25 · channel_span`), so both penalty terms are in µm² and comparable. Move the
`1e6` multipliers to named constants shared with the router. Tests: the official Fig-14/15 example
(3600 nets: channel area 87,940 µm² vs FT 36,063 µm²) now scores FT as cheaper; existing overflow
regressions updated to the area unit; **per-case Cost table must not regress legality** (open_routes,
validation, mismatch unchanged). **This is a scoring-fidelity change P2 depends on — land first,
measure its own before/after sweep (it changes reported scores on every overflow row).** ~40 LOC + test.

**T1 — cost-unit edge-weight functions (pure, the crux).**
Files: new `src/route_cost.hpp/.cpp` (or a namespace-local section of `routing.cpp`). Implement pure
`channelEdgeCost(channel, dir, n, usage)` (§3.2 marginal overflow) and `softFtEdgeCost(block, acc, n,
placed)` (§3.3 marginal tier delta, reusing `ft.hpp`'s `A(·)`). **No routing, no mutation.** Tests
assert: slack channel → 0 area cost; saturated → `1e6·n`; partially saturating → exact boundary; FT
crossing a tier boundary (acc=2900, n=200) charges the tier jump; slack-absorption (placed > A(acc))
→ 0 until exceeded. This is the highest-risk logic (unit correctness, §8.1); test in isolation first.
~90 LOC + test.

**T2 — cost-driven single-chunk Dijkstra (one pass, no FT-vs-channel special-casing).**
Files: `src/routing.cpp`. A new `routeChunkCostDriven` that runs *one* Dijkstra with T1 weights over
the full graph (channels + soft blocks both allowed from the start — no two-pass structure), using
current usage/acc as the marginal-price snapshot. Tests: on a hand-built 2-block direct-abut case →
zero-cost 2-node path (A31 preserved); a case where FT is cheaper than a saturated channel → path goes
through the soft block; a case where the channel has slack → path uses the channel. ~80 LOC + test.

**T3 — adaptive bottleneck chunking (§5).**
Files: `src/routing.cpp`. Replace the fixed `ceil(nets/1000)` split with the greedy re-price-per-chunk
loop (§5.2), bounded by `kMaxChunks`/`kMinChunk`. Tests: a bundle whose cheapest path saturates a
channel at 300 nets splits ~300 to that channel, remainder diverts; chunk count ≤ kMaxChunks; total
nets conserved (Σ chunk nets == bundle.nets); each chunk emits one route. ~70 LOC + test.

**T4 — negotiation loop with history cost (§4).**
Files: `src/routing.cpp`. Wrap T2+T3 in the R-round rip-up/reroute loop with per-resource per-direction
history (§4.2), heaviest-bundle-first ordering, best-round selection. Tests: a 2-bundle contention
case where round 0 overflows but round 1 (history surcharge) reroutes the lighter bundle → lower total
Cost; determinism (`routeProblem(p) == routeProblem(p)` bit-for-bit, §6); R=1 reproduces T2+T3 (no
history yet). ~90 LOC + test.

**T5 — feasibility fallback + comparison gate (never emit open routes).**
Files: `src/routing.cpp`. If the cost-driven router leaves any route open, fall back to the existing
two-pass `routeChunk` for *those bundles only* (or wholesale, whichever is simpler and still routes
everything). Structure so `routeProblem` returns the cost-driven result when it is fully routed and
no worse, else the fallback. Tests: a case the cost-driven router can route → cost-driven result used;
a pathological case (constructed to open a cost-driven route) → fallback engages, `open_routes==0`;
**every existing routing/validation regression still passes** (Pareto-safety proof). ~60 LOC + test.

**T6 — wire cost-driven router as the default in `routeProblem`; keep old path reachable.**
Files: `src/routing.cpp`. Make the cost-driven+negotiated+adaptive router the `routeProblem` body,
with the old two-pass retained as the named fallback. No interface change (`routing.hpp:8` unchanged),
so `eval.cpp`/`search.cpp`/`respace.cpp` callers are untouched — cache key and determinism preserved
(§6). Tests: `run_case00.sh`, `run_placement_engine_regressions.sh`, `run_20260618/08_regressions.sh`,
`run_p1/p3_regressions.sh` all green. ~30 LOC + full suite.

**T7 — full sweep + doc appendix (the P2 measurement, mirrors P1/P3 appendices).**
Files: docs + `test/run_p2_regressions.sh` final wiring. Run the 5×3 `--iters 2000 --seed 1` sweep,
record score/outline_area/wirelength/channel_overflow/mismatch/validation before (P1+P3 HEAD) vs after
(P2), append to the strategy doc. Assert acceptance: **on the axis-locked rows (case2/sp, case3/*,
case4/*), channel_overflow strictly drops via FT conversion where soft blocks abut the flow; net Cost
≤ P1+P3 on every row; open_routes==0 and validation=ok everywhere.** Honest reporting of rows where no
FT path exists (topology-bound, §8.2). ~doc + harness. **Closes the P3 addendum's axis-locked
dependency.**

**Ordering rationale.** T0 fixes the scorer unit so the objective is coherent (without it, every later
task optimizes a unit-inconsistent cost — this is *the* prerequisite). T1 nails the edge-weight math in
isolation (the §8.1 unit trap lives here). T2-T4 assemble the router behind pure, tested pieces. T5
guarantees we never emit an open route (the one hard-fail), making T6's swap Pareto-safe. T7 measures.
Every task falls back to "reproduce the old router" if the cost path finds nothing better — regression
is impossible by the comparison gate (T5).

---

## 10. Risks & open questions

- **R0 (BLOCKING) — scorer unit mismatch (§8.1).** `channel_overflow` in nets vs `feedthrough_mismatch`
  in µm², both ×`1e6`, makes FT-vs-channel incomparable and *inverts* the official 2.4× area win.
  **Mitigation:** T0 area-denominates overflow first. **This design cannot deliver its promised win
  until T0 lands** — flag prominently to the orchestrator; consider a Codex second opinion on the unit
  analysis before building, since it reinterprets a P0 "done" item.
- **R1 — axis-crossing sign error (per-direction history/usage).** Same class as P3's R1: surcharging
  the wrong direction relieves nothing. **Mitigation:** T1/T4 test per-direction weights in isolation;
  reuse the A3 mapping (`routing.cpp:190-191`) verbatim.
- **R2 — center-manhattan WL proxy vs guiding-point WL (§3.1).** The search cost uses α·n·center-distance
  but the scored WL is guiding-point HPWL; a path minimizing the proxy may not minimize true WL.
  **Mitigation:** the area terms dominate on overflow-bound cases (the P2 target), so WL-proxy error is
  second-order there; document the gap, revisit with P4's proxy-alignment work if a WL-bound case shows
  it.
- **R3 — FT conversion is topology-bound (§8.2).** P2 can only use soft blocks that geometrically abut
  the flow; a case with no adjacent soft block on the locked axis is irreducible by routing alone.
  **Mitigation:** honest per-row reporting (T7); note that P4 abutment moves are the complementary lever.
- **R4 — negotiation non-convergence / thrash.** **Mitigation:** R≤3 rounds hard cap, best-round
  selection (never worse than round 0 which ≈ the greedy single-pass), history decay (§4.2). Bounded by
  construction; determinism gate (§4.4).
- **R5 — robustness to the unknown judge weight (§7).** **Mitigation:** FT-vs-channel decision depends
  only on the *ratio* of two equal weights (cancels), so absolute-magnitude error is harmless for the
  core decision; named constants allow one-line re-tuning.
- **R6 — cache/determinism leak (§6).** **Mitigation:** all history/usage/acc are call-local; T4 asserts
  bit-identical repeat routing; no `static`/global/RNG in routing.
- **Q1 — should FT and channel share one history ledger or two?** Design uses two (per-channel-direction
  + per-soft-block). Simpler and matches the per-resource PathFinder model; revisit only if profiling
  shows cross-resource oscillation.
- **Q2 — interaction with P3 (§ compose).** P2 routes (picks FT vs channel), P3 sizes the channels P2
  chose. P2 runs *inside* `routeProblem`, so P3 (which calls `routeProblem`) automatically sees P2's
  routing and sizes accordingly. No ordering conflict — P3 is downstream of whatever `RoutingResult`
  P2 produces (`p3-design.md` §7-Q2 already anticipated this).
- **Q3 — should adaptive chunking also *merge* under-full chunks?** v1 only splits. Merging (fewer PATH
  lines) is a cosmetic/output-size optimization, not a Cost lever; defer.
