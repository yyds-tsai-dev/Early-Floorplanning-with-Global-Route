# case5 attack plan — final week (deadline 2026-08-21)

**Author**: deep-reasoner, 2026-08-14. **Scope**: read-only analysis of the tree at `fable5` (working copy
carries P0a). **Inputs**: `docs/research/2026-08-14-final-levers-verdicts-and-0811-intel.md` §3/§4/§5,
`docs/research/2026-08-13-external-levers-v3-survey.md`, `CONTEXT.md`, `src/{search,eval,routing,placement_engine,respace}.cpp`,
`src/{route_cost,route_geometry,ft,model}.hpp`, `test/E_testcase_20260811/testcase_upload_20260712/2026 ICCAD_case.case5.csv`.

---

## 0. Verdict in one paragraph

**The binding constraint on case5 is not throughput — it is objective fidelity.** `cheapScore`'s
route-aware proxy (`eval.cpp:77-118`) charges a feedthrough term that on case5 is **~1.1e10 to 1.5e10**,
while the two terms the checker actually scores are `outline_area` ~2e7-6e7 and `alpha*WL` ~5e7-8e7. The
proxy term is therefore **~110x the entire real objective**, and its *variation* across arrangements
(~2e9) is **~40x larger than the entire dynamic range of area+WL**. Worse, that term is a local surrogate
for a *global* constraint the exact tier prices as `kBetaFt * max(0, ft_area_sum - outline_area)`
(`eval.cpp:247`) — a quantity that is **exactly zero** on every case5 solution we produce
(`ft_area_sum` ~2e7-3e7 vs `outline_area` 5.01e7). So for ~14,500 SA iterations we optimize a term the
scorer charges nothing for, at 100x the weight of everything it does charge. That is the whole story of
the 47.9M gap; iteration count is a second-order consequence.

The corroborating evidence is already in the verdicts doc: **case6 runs 693,000 iterations and is still
+28.9% off champion; case7 runs 307,000 and is +39.3% off.** Cases with 20-50x more iterations per block
than case5 are just as far behind. Throughput is not what separates us from the leaders.

Ranked plan: **L1 cheap-tier objective realignment** (the lever), **L2 feedthrough-regime enablement**
(cheap, directly targets the champion's solution family), **L3 exact-tier budget governor + iteration
realloc** (enabler, unlocks 10-100x SA), **L4 shape-aware seed leg + SA throughput micro-fix**.

---

## 1. Case5 facts (measured from the CSV + derived from the code)

| Fact | Value | Source |
|---|---|---|
| Blocks | 30, **all SOFT**, **zero EDGE blocks** | CSV rows 4-33 |
| AR range | `0.25,4` for every block | CSV col 5 |
| Block areas | 50,664 .. 1,938,273; **sum 17,949,100**; `sum sqrt(A_i) = 21,140`; mean side 705 | computed |
| MAX outline | 8200 x 7500 = 61.5M | CSV `MAX` row |
| alpha | 0.05 | CSV |
| Bundles | 602 directed, **value-symmetric**, 301 after merge; `sum nets = 345,386`; mean 1,147; max 3,332 | computed |
| Netlist structure | **near-regular**: every block has **exactly 9** partners with >=1000 nets; net-degree per block 23,400-25,100 (spread <7%) | computed |
| FT rates | 20% / 40% / 80% / 100% at the <=3000 / <=6000 / <=9000 / >9000 tiers, identical for all 30 blocks | CSV |
| Our merged 600s run | score 88.3M = OA 50.1M + 0.05 * 764M(=38.2M) + 0 penalty; overflow 0; 17,000 iters; 636s | verdicts §3.3 |
| Champion | Area 27,421,020 + 0.05 * 316,594,862 (=15.83M) = **43.25M** | verdicts §4 |

Two derived quantities that matter more than any of the above:

- **Champion utilisation**: 17.95M block area / 27.42M outline = **65%**. Ours: 17.95M / 50.1M = **36%**.
- **Champion WL per net**: 316,594,862 / 345,386 = **917 um**. Ours: 764M / 345,386 = **2,212 um**.
  Mean block side is 705 um. 917 um is *one block traversal plus a short channel hop*.

---

## 2. §1 — Binding-constraint analysis (work accounting from the code)

### 2.1 What one SA iteration costs

Per iteration (`search.cpp:713-775`):

| Step | Anchor | Work on case5 |
|---|---|---|
| `engine.mutate` | `placement_engine.cpp:704-768` (v1 table) | O(1); v2 move legs pay an extra full `decode` in `mutateWireMedian`/`mutateGreedyReposition` (`placement_engine.cpp:860,871`) |
| `isValidState` | `placement_engine.cpp:832-835` | 2 sorts of 30 ints + a `movableBlocks` allocation |
| `decode` (SP) | `placement_engine.cpp:770-830` | 30 x `dimensionFor` window search (~1.5K ops) + 435 SP constraints + **30 relaxation passes x 435 = 13,050** + 30 x `placeWithContour` (each O(n) collision/contour scans) + `shrinkWrapOutline` (O(n^2) legality, 1 probe with no edge blocks). ~20-25K ops |
| `cheapScore` | `eval.cpp:148-203` | 435 overlap tests + 301 wire terms + `routeAwareProxyPenalty`: **301 bundles x 30 blocks = 9,030** `intersectsOpen` tests plus `sqrt`/`ceil`/`round` for each hit (~1,500-2,500 hits). ~15-70K ops |
| `rememberCandidate` | `search.cpp:726` -> `rememberBounded` `search.cpp:206-216` | **every iteration**: `placementKey` builds an `ostringstream` with 121 integers (`search.cpp:99-110`), then a linear scan of 16-48 strings, then a `push_back` that **copies the whole `Placement`**, then `std::sort`, then `resize`. Est. 5-15 us |

Total: **~50-150 us/iteration, i.e. 7,000-20,000 iterations/second.** Over 14,500 iterations that is
**~1-2 seconds** of the 828.8 s run. The cheap tier is not the bottleneck; it is a rounding error.

### 2.2 What one exact evaluation costs

`exactScore` -> `routeAndScoreAt` -> `routeProblem(cost_driven=true)` (`routing.cpp:72-900`):

- `extractChannels` (`routing.cpp:34-70`): 62 x-cuts x 30 rects; yields roughly 60-150 channels.
- Adjacency: O((30+C)^2) `positiveTouch`, ~8.6K tests at C=100.
- **Negotiation rounds** (`routing.cpp:688-871`, `kNegotiationRounds = 3`, `route_cost.hpp:419`), each
  routing all 301 bundles. Per bundle the greedy chunk loop (`routing.cpp:735-774`) calls `findRoutePath`
  (one Dijkstra over ~131 nodes / ~1,000 directed edges) once per chunk. With `max_chunks = 2*kMaxChunks
  = 32` under the merged auto-rule (`routing.cpp:686`) but channel slack usually >= remaining, the
  typical bundle takes 1-3 chunks. **~300-900 Dijkstras/round.** The overflow-free early exit
  (`routing.cpp:870`) collapses this to a single round when a round ends bump-free — which is exactly
  what the merged case5 run achieves (overflow 0), and is most of the measured -22% wall.
- **Plus one unconditional legacy pass** (`routing.cpp:894`): 301 bundles x `min(8, ceil(nets/1000))`
  chunks x up to 2 passes = **~900-1,800 Dijkstras**, every exact eval, always.

So one exact eval on case5 is **~1,200-2,700 Dijkstras**, empirically **20-70 ms**.

### 2.3 Where the 828.8 s actually goes

Count exact evals per `runPlacementSearch` on case5:

| Site | Anchor | Exact evals |
|---|---|---|
| seed | `search.cpp:632-638` | 1 |
| SA loop periodic + cheap-beats-best | `search.cpp:739-742` | ~10-50 at `iters=500`, `exact_interval=50` |
| elite-pool fidelity snapshot | `search.cpp:795-800` | 16 (v1) / 48 (v2) |
| `buildEliteFullSet` rung-0 | `search.cpp:254-260` | +~12 new (v2 only) |
| **elite loop: 2 x `outlineSweep` per candidate** | `search.cpp:818,830` | **16 candidates x 2 sweeps x ~28 rungs = ~896** |
| `maybeAdoptRespace` per candidate | `search.cpp:842`, `respace.cpp:439-461` | 16 x <=4 = <=64 |
| `maybeAdoptCapacityGapRungs` x2 | `search.cpp:875,918` | <=12 |
| `finalizeFeedthrough` | `search.cpp:441-592` | 3 passes x (1 + <=4 + 1 + <=4 + **32 uncached repair attempts**, `search.cpp:578-590`) = **~126** |
| post-finalize sweeps | `search.cpp:898,907` | ~56 |

**~1,150-1,250 exact evals per `runPlacementSearch`.** A `hybrid` leg is three of those
(`search.cpp:941,948,973`) -> **~3,500/leg**; the 10-leg registry (`search.cpp:1019-1040`) ->
**~35,000 exact evals**. At 828.8 s that is ~24 ms each — consistent with §2.2.

The 28-rung count comes from `outlineSweep` (`search.cpp:290-439`): 7 margins (`search.cpp:299`) + 7 pads
x 3 variants (`search.cpp:306,409-417`) = 28 distinct `(W,H)` targets, each a full re-route. Case5 has
**no edge blocks**, so every rung has *byte-identical block rects*; only the declared outline differs.

**Conclusion of the accounting:**

```
SA loop        : ~1-2 s      (0.2%)
elite sweeps   : ~640 s      (77%)     <- 16 elites x 2 sweeps x 28 rungs
finalize repair: ~90 s       (11%)
respace + B7   : ~55 s       (7%)
routing overhead in all of the above dominates everything else
```

**Binding constraint verdict.** The wall clock is spent almost entirely in a *fixed-size* post-SA rung
ladder whose cost scales with instance size, while the SA iteration count is a *constant* (`--iters 1000`)
independent of instance size. That is why case5 gets 14,500 iterations while case6 gets 693,000. But —
critically — **buying more iterations is not the lever**, because:

- **2x iterations** (29,000): with today's objective, expected effect **~0**. The cheap objective on case5
  is ~99.3% a term the exact tier deletes at `eval.cpp:227` and charges 0 for at `eval.cpp:247`. Doubling
  the sampling of a mis-specified objective buys nothing measurable.
- **10x iterations** (145,000): still ~0 with today's objective. Direct evidence: case6 at 693K iterations
  is +28.9% off; case7 at 307K is +39.3% off. Both have 20-50x more iterations *per block* than case5 and
  are proportionally just as far behind. There is no iteration cliff we are sitting below.
- **10-100x iterations *after* L1 fixes the objective**: this is where throughput becomes real value, and
  §5's L3 shows the machinery realloc can deliver 100-400x (from ~1,000 to ~100,000+ SA iterations/leg).

So: **objective first, throughput second, and throughput only pays once the objective is fixed.**

### 2.4 The three broken terms in `cheapScore`, quantified

`cheapScore` (`eval.cpp:148-203`) = `outline_area + alpha*wire_proxy + penalty`, where `penalty` includes
`routeAwareProxyPenalty` (`eval.cpp:77-118`). On case5:

**(a) The congestion term is identically zero.** `eval.cpp:84-95` sets
`capacity = 25 * max(1, min(a.h, b.h))` (or the widths). At the near-square shapes the search actually
visits, the smallest block side is 225 um (BLK30) -> capacity >= 5,625 > the largest merged bundle
(3,332). Even at the AR extremes the residual is ~1e4 against a 1e10 penalty. **The cheap tier has no
congestion signal at all on case5.**

**(b) The FT term is ~110x the entire real objective and is charged for a free resource.**
`eval.cpp:106-115` charges, per (bundle, soft block whose rect intersects the source/target
centre-to-centre bbox), `max(0, ftRequiredArea(base, ftExtend(nets, rate)) - placed_area) * 1000.0`.
Numerically (verified by direct computation over the real CSV, 8-sample average over random and
shelf-packed layouts):

| Case | n | soft | bundles | OA | alpha*WL | **FT-bbox proxy** | ratio |
|---|---|---|---|---|---|---|---|
| 0 | 5* | 3 | 3 | 5.4e6 | 5.7e5 | 5.1e6 | 0.9 |
| 1 | 14* | 13 | 82 | 7.0e6 | 1.1e7 | 1.3e9 | **70** |
| 2 | 16* | 8 | 74 | 3.7e7 | 2.5e7 | 2.3e9 | **37** |
| 3 | 13* | 8 | 49 | 2.0e7 | 5.9e6 | 3.1e8 | **12** |
| 4 | 18* | 10 | 37 | 3.4e7 | 2.0e7 | 6.2e8 | **12** |
| **5** | **30** | **30** | **602** | **5.6e7** | **8.1e7** | **1.5e10** | **107** |
| 6 | 8* | 8 | 26 | 1.3e7 | 6.2e6 | 7.8e7 | 4.0 |
| 7 | 9* | 7 | 54 | 4.3e6 | 2.0e6 | 3.0e7 | 4.7 |

(*a quick standalone parser; a few EDGE rows with multi-line quoted fields are missed, so `n` is a lower
bound. Order of magnitude is unaffected.)

The exact tier deletes this term wholesale (`eval.cpp:227`) and replaces it with
`kBetaFt * max(0, ft_area_sum - outline_area)` (`eval.cpp:247`, `kBetaFt = 3.0`). On every case5 solution
we produce, `ft_area_sum` (>= 17.95M, at most ~30M with heavy feedthrough) is far below
`outline_area = 50.1M`, so **the exact tier charges exactly 0** while the cheap tier charges ~1.4e10.
This is, by itself, a complete explanation of the measured `tau` of 0.067 in `b6-report.md`.

**(c) The WL proxy models the wrong distance.** `eval.cpp:191` uses *centre-to-centre* Manhattan. The
scorer's WL is the guiding-point chain (`route_geometry.hpp:102-137`), and a direct block-to-block
abutment is a 2-node path producing **exactly one guide point and therefore exactly 0 wirelength**
(`route_geometry.hpp:93-94`). Two abutting 1,000x1,000 blocks: proxy says `1000*nets`, scorer says `0`.
B5b's abutment credit (`eval.cpp:62-75`) is a partial patch on this, is only on 4 of 10 registry legs, and
is applied to a term that is 1% of the cheap value anyway.

---

## 3. §2 — Structural gap analysis: reconstructing the champion's solution

### 3.1 The champion is in a feedthrough-dominant regime; we are in a channel-dominant one

Model the champion. Let `k` be the mean number of block traversals per net. Distribute `k*345,386`
crossings uniformly over 30 blocks and evaluate
`ft_area_sum = sum_i (sqrt(A_i) + ftExtend(C_i, rate(C_i)))^2` with `ftExtend` from `ft.hpp:19-21`:

| k | crossings/block | tier rate | extend (um/side) | `ft_area_sum` |
|---|---|---|---|---|
| 0.5 | 5,756 | 40% | 47 | 20.0M |
| 0.6 | 6,908 | 80% | 111 | 23.0M |
| **0.8** | **9,210** | **100%** | **185** | **26.8M** |
| 1.0 | 11,513 | 100% | 231 | 29.3M |
| 2.0 | 23,026 | 100% | 461 | 43.8M |

**Champion Area = 27.42M lands at k ~ 0.85.** Independently, champion WL/net = **917 um**, and the mean
block side is **705 um** — i.e. one block traversal plus a short channel hop. Two independent numbers
agree on the same picture:

> The champion packs near-tight, routes ~0.85 block-crossings per net *through* soft blocks, and sizes the
> outline so `outline_area ~= ft_area_sum` — the point where the official
> `kBetaFt * max(0, ft_area_sum - A)` term is exactly on its knee.

### 3.2 Why feedthrough is not optional on case5

The netlist is near-regular (every block has exactly 9 heavy partners, net-degree spread <7%). For any
vertical cut line at `x = c`, the straddling demand is `~2f(1-f) * 345,386`, peaking at **~172,700 nets**
at the median cut. Pure-channel capacity at that cut is `25 * (H - sum of heights of blocks covering c)`
(`routing.cpp:544-564`), so a channel-only solution needs **>= 6,908 um of free vertical extent at the
median x** — with `H_max = 7,500` that leaves under 600 um of block height at that x, which is
impossible against 17.95M of block area. The same argument holds on the y axis.

**Therefore any compact case5 layout must carry a large fraction of its traffic through soft blocks.**
Our channel-first solution escapes this only by inflating the die to 50.1M, which buys the channel
cross-section at the cost of both terms of the objective.

### 3.3 Which of our known weaknesses the evidence implicates

Ranked by how much of the 47.9M gap each explains:

1. **The cheap tier actively fights the winning regime (dominant).** The FT-bbox term
   (`eval.cpp:106-115`) charges precisely for "a soft block sits between two connected blocks" — which is
   the *definition* of a feedthrough opportunity, and the geometric signature of a dense packing. It is
   the largest term in the cheap score by 100x. The SA is being steered away from the champion's solution
   family at maximum weight.
2. **The router overprices FT on 8 of 10 legs (major).** The conservative branch
   (`routing.cpp:299`) charges `kBetaFt * ftMismatchDelta(...)` — the *per-block* required-vs-placed
   mismatch — even when the official V5 *aggregate* has slack. The correct marginal already exists as
   `ft_pricing_aggressive` (`routing.cpp:271-290`) but only on registry legs 8 and 9
   (`search.cpp:1037-1038`), i.e. **exactly the legs P0a's new phase-1 budget gate
   (`search.cpp:1141-1157`) will cut first on case5.** Post-P0a, case5 may never run FT-aggressive pricing
   at all. This is a regression risk introduced by a fix, and it must be handled this week.
3. **Blind AR mutation leaves all 30 blocks square (major).** `initialAspect` (`placement_engine.cpp:402`)
   starts every soft block at AR 1.0, and `mutateSoftAspect` (`placement_engine.cpp:499-511`) is a single
   `x[0.85, 1.15]` nudge on one block, fired on 20% of moves. With ~500 SA iterations per sub-run, each of
   the 30 blocks receives ~3 nudges of +-15%: the ARs never leave a +-50% band around 1.0. Packing 30
   squares whose sides span 225..1,392 um structurally wastes ~50% of the die. Measured directly on the
   real areas: a naive first-fit shelf of squares reaches 21.7M; a row pack with AR-flexible row heights
   reaches **17.95M (100% utilisation)**.
4. **Whitespace is unshaped, not merely excessive (contributing).** Our 32M of whitespace is distributed
   as packing slop, not as channels with useful cross-section. The cheap tier is blind to channel shape
   (§2.4a) and so cannot trade "less whitespace, better placed" against "more whitespace, badly placed".
5. **The elite outline sweep is the wrong tool at this scale (contributing).** 28 rungs x 32 sweeps
   consumes 77% of the wall clock to tune the *declared outline* of an arrangement whose *arrangement* is
   the problem. And its ladder (`{tight * margins} U {tight + pads}`) does not contain the one target the
   §3.1 analysis says matters: `A = ft_area_sum`.

**What is not implicated**: iteration count (§2.3), negotiation-round pricing (case5's overflow penalty is
2.3M unmerged and **0** merged — B1 constant-gap has almost nothing to win here), and fixed-topology WL
redistribution (C1 — it optimises coordinates for a topology that is itself the defect, and it is an
exact-tier consumer competing for the budget L3 is trying to free).

### 3.4 Sanity check on the size of the prize

Direct computation on the real CSV, using boundary-gap HPWL (`sum nets * (dx_gap + dy_gap)`) as a WL
estimator that respects the zero-WL-abutment rule:

| Layout | Area | boundary-gap WL | `A + 0.05*WL` |
|---|---|---|---|
| **Ours today (merged, 600 s)** | 50.1M | 764M (actual routed) | **88.3M** |
| Naive 5-row pack, AR-flexible, 35% whitespace | 24.2M | 470M | **47.7M** |
| Same, W tuned to 4,200 | 24.2M | 455M | **47.0M** |
| **Champion (actual)** | 27.4M | 317M (actual routed) | **43.25M** |

**An unoptimised, connectivity-blind row pack with sensible shapes already lands within ~10% of the
champion and ~40M ahead of us.** The gap is not an optimisation-quality gap; it is a "we never produce a
compact, shape-aware packing at all" gap. That is the single most important number in this document.

---

## 4. §3 — The plan: four levers, fully designed

Design constraints applied throughout: every lever is behind a flag that defaults to today's behaviour;
budget-off paths (every regression script) must stay structurally untouched, following the P0a precedent
at `search.cpp:1146` (short-circuit on `options.time_budget_sec > 0.0` as the first operand); no external
dependencies (static-link constraint, memory `submission-binaries-must-be-static`).

---

### L1 — Cheap-tier objective realignment (`Problem::cheap_proxy_v2`)

**The lever.** Replace `routeAwareProxyPenalty` and the WL proxy with scorer-unit surrogates of the terms
the exact tier actually charges. Three sub-changes that must land **together** — (a) alone leaves WL
mismodelled; (b) alone without (c) drives degenerate zero-channel slabs.

**Files**: `src/eval.cpp` (all of it), `src/model.hpp` (one flag), `src/main.cpp` (one hidden flag),
`src/search.hpp`/`search.cpp` (one `SearchVariant` field, optional).

#### L1a — Aggregate FT term (replaces `eval.cpp:104-115`)

Mirror the exact tier's own form (`eval.cpp:247`) instead of a per-pair local surrogate:

```
// est_ft_nets[i] is filled by L1c below (cut-line excess distribution).
double est_ft_area_sum = 0;
for each block i:
    est_ft_area_sum += isSoft(i)
        ? ftRequiredArea(base_i, ftExtend(est_ft_nets[i], ftTierRate(block_i, est_ft_nets[i])))
        : block_i.area;
penalty += kBetaFt * std::max(0.0, est_ft_area_sum - w * h);
```

`base_i = block.ft_base_area > 0 ? block.ft_base_area : block.area` (same fallback as
`routing.cpp:570`). This is byte-for-byte the functional form of `eval.cpp:247` and of
`routing.cpp:594-604`, so the cheap and exact tiers now agree on *what* FT costs.

Magnitude check: §3.1's table says `est_ft_area_sum` lands at 20M-30M for realistic `k`, so the term is 0
above ~27M of die and rises at 3 um^2 per um^2 below it. It is a **correctly-scaled floor on compaction**,
not a 1e10 noise field — and §3.1 shows that floor is exactly where the champion sits.

#### L1b — Boundary-gap WL proxy (replaces `eval.cpp:187-198`)

```
double dx = std::max(0.0, std::max(a.x, c.x) - std::min(a.right(), c.right()));
double dy = std::max(0.0, std::max(a.y, c.y) - std::min(a.top(),   c.top()));
wire_proxy += (dx + dy) * b.nets;
```

Zero for abutting pairs — matching `route_geometry.hpp:93-94`'s "a path of <2 nodes has 0 wirelength" —
and equal to the true facing-border separation otherwise, which is a good estimate of the guide-point
chain length through a channel grid. **When `cheap_proxy_v2` is on, force `abutment_credit` off**
(`eval.cpp:196`): the credit exists to approximate exactly this and would double-count.

#### L1c — Cut-line stabbing-density -> feedthrough estimator (replaces `eval.cpp:84-95`)

This is survey lever B5 (Yoshimura-Kuh density) composed with A5's whitespace-shape intent, but wired to
*produce* the FT load L1a prices rather than to charge a separate penalty. Pseudo-code, O((n+m) * cuts):

```
// vertical cuts: midpoints between consecutive distinct block x-edges, clipped to (0, w)
xs = sorted unique {r.x, r.right() for all rects} intersect (0, w)
for each c in midpoints(xs):                                   // <= 2n-1 = 59 cuts
    free_h = h - sum over blocks covering c of r.h              // O(n)
    demand = sum over bundles straddling c (cx_src < c) != (cx_tgt < c) of nets   // O(m)
    cap    = 25.0 * free_h
    cap_total += cap
    excess = max(0, demand - cap)
    softs  = soft blocks covering c
    if softs nonempty:  distribute excess into est_ft_nets[i], weighted by r_i.h
    else:               hard_overflow += excess
// symmetric pass on y: free_w, cap = 25 * free_w, weight by r_i.w
penalty += officialOverflowPenalty(hard_overflow / max(1, cap_total), w * h);   // route_cost.hpp:59
```

Rationale for routing the excess into `est_ft_nets` instead of charging it as congestion: feedthrough has
**no capacity limit** in `routeProblem` (`routing.cpp:94-98` allows any soft block as an intermediate);
it is limited only by the FT area price. So demand that cannot fit the channels does not overflow — it
becomes feedthrough, and L1a prices it at the official rate. Only demand crossing a cut with *no* soft
block available is true overflow.

Cost: 59+59 cuts x (30 + 301) = **~39K ops**, vs today's 9K. Roughly 2-4x the current
`routeAwareProxyPenalty` cost, i.e. SA iteration cost goes from ~50-150 us to ~80-200 us. Acceptable
given L3 multiplies the SA budget by 100x. If it measures hot, prefix-sum the block coverage per cut
(O(n log n) once per eval) — but do not optimise before measuring.

#### L1d — Implementation invariants (non-negotiable)

1. **The exact tier must remain untouched.** `routeAndScoreAt` subtracts the proxy exactly
   (`eval.cpp:227`). The new function's signature must become
   `routeAwareProxyPenaltyV2(problem, placement, w, h)` and be **subtracted with the same `(w, h)` that
   `routeAndScoreAt` was scoped at** — `routeAndScoreAt` already has them as parameters, so write
   `score.penalty -= routeAwareProxyPenaltyV2(scoped, placement, w, h);`. With this, exact scores are
   provably unchanged and `test/run_selfconsistency_regressions.sh` and
   `test/run_cfg_score_regressions.sh` are green **by construction**.
2. **Byte-identity when off.** Gate with an `if (problem.cheap_proxy_v2) { ... } else { ...existing... }`
   — a branch, never a multiply-by-one, per the B5b/L2/L6 precedent (`eval.cpp:101`, `eval.cpp:196`).
3. `est_ft_nets` is a per-call `std::vector<double>` (no statics, deterministic).

**Effect channel**: OA (compaction becomes visible), alpha*WL (abutment becomes valuable), and the
FT/overflow balance (correctly priced for the first time in the cheap tier).
**Effort**: ~200-250 LOC in `eval.cpp` + ~15 elsewhere. **Risk**: medium — it changes the SA landscape on
every case where it is on. Contained by the flag + the scoping decision in §6.
**Abort criteria**: if the composite (L1 on) is worse on case5 at fixed `--iters 1000` (budget off, same
work, isolating the objective change) **and** the diagnosis in §2.4 cannot be re-derived from a live run,
stop and fall back to L1a alone (the FT rescale is the piece with the strongest independent evidence).

---

### L2 — Feedthrough-regime enablement (small, high-value, independent of L1)

Three surgical changes that make the champion's regime *reachable*.

#### L2a — Add an `A = est_ft_area_sum` rung to `outlineSweep`

`outlineSweep`'s ladder (`search.cpp:299,306`) is `{tight * margins} U {tight + pads}`. Neither family
contains the target §3.1 identifies as the optimum: the outline where `outline_area == ft_area_sum`. Add
one rung, computed from the *routed* result of the tight rung (which `outlineSweep` already evaluates
first, at `search.cpp:398` with `margin == 0.0`):

```
// after the m-ladder, before the pads:
// tight rung's RoutingResult carries ft_area_sum (routing.cpp:594-604).
double target_area = tight_routing.ft_area_sum;
if (target_area > tight_w * tight_h) {
    double s = std::sqrt(target_area / (tight_w * tight_h));
    tryTarget(min(MAX_w, ceil(tight_w * s)), min(MAX_h, ceil(tight_h * s)), false);
}
```

This requires `tryTarget` to hand back the tight rung's routing — a 5-line change to capture it in the
lambda. **~20 LOC, essentially zero risk** (one more rung, adjudicated by the same
`outlineSweepBetter` gate, Pareto-safe by construction).

#### L2b — Expose `ft_pricing_aggressive` as a CLI flag and get it into case5's affordable leg set

Today it is reachable only through registry legs 8-9 (`search.cpp:1037-1038`) — the two legs P0a's gate
(`search.cpp:1141-1157`) cuts first on an expensive instance. Two parts:

1. Add a hidden `--ft-aggressive` flag in `main.cpp` next to `--undirected-bundles`
   (`main.cpp:150`), purely so the A/B in §6 can isolate it. ~5 LOC.
2. **Reorder `defaultVariants()` for the budget-bounded path only** (or, preferably, define a separate
   `deepVariants()` used by L3's deep mode) so an FT-aggressive leg runs **second or third**, not tenth.
   Do *not* reorder `defaultVariants()` itself: leg order is load-bearing for byte-identity on the
   budget-off path (every regression). Put the reorder inside L3's deep-mode branch.

#### L2c — Sanity check the conservative FT price (analysis task, may become a fix)

`routing.cpp:299` charges `kBetaFt * ftMismatchDelta(b, acc, nets, placed_area)` where
`placed_area = round2(w*h)` of the block as placed — i.e. it charges for mismatch even when the *aggregate*
`ft_area_sum` is far under the outline and the official penalty is 0. On case5 this is a systematic
overcharge on 8 of 10 legs. If the §6 A/B shows `--ft-aggressive` alone wins on case5, that is the
confirmation; the follow-up (only if time allows) is to make the aggressive branch the default under the
same auto-rule as L1.

**Effort**: ~40 LOC total. **Risk**: low. **Effect channel**: WL (feedthrough paths are short and
straight) and OA (via L2a's outline target).

---

### L3 — Exact-tier budget governor and iteration reallocation

Three independent pieces; land them in this order.

#### L3a — Overflow-gated pad ladder (principled, ~15 LOC)

In `outlineSweep`, the 21 pad rungs (`search.cpp:409-417`) are a pure **capacity-denominator** play: they
raise `channel_capacity_total` to lower `R = overflow_nets / capacity_total` in
`officialOverflowPenalty` (`eval.cpp:244-246`). If the tight rung already routes with
`open_routes == 0 && channel_overflow_nets == 0`, the pads can only *increase* `outline_area` (cost
+`pad * H` >= 5,000 um^2 per um of pad) against a penalty that is already 0. Their only residual upside is
a second-order WL shift from boundary guide points moving.

```
if (include_pads && !(tight_routing.open_routes == 0 && tight_routing.channel_overflow_nets == 0)) {
    ...existing pad loop...
}
```

On the merged case5 run (overflow = 0) this removes **21 of 28 rungs**, i.e. cuts the elite machinery by
~70% and the whole run by ~55%. Keep it behind the same flag as L3b/L3c so a case that does need pads is
unaffected (it will have nonzero overflow at the tight rung and take the pads anyway).

#### L3b — Cost-aware elite and repair budgets (~40 LOC)

`kFullTreatmentSize = 16` (`search.cpp:845`), `kTopCheapCandidates = 16/48` (`search.cpp:644`) and
`kFinalizeRepairAttempts = 32` (`search.cpp:578`) are constants tuned on 7-24-block instances. Make them
a function of the *measured* seed exact-eval wall time (`search.cpp:632-638` already performs exactly one
exact eval before any pool exists — time it):

```
// t_exact = wall seconds of the seed exactCached call
// budget the whole post-SA machinery at <= kMachineryShare of the leg's slice
int elites = clamp((int)(machinery_budget_sec / (t_exact * rungs_per_elite)), 3, 16);
int repairs = clamp((int)(repair_budget_sec / t_exact), 4, 32);
```

Deterministic-enough caveat: this makes leg behaviour machine-speed dependent. That is already true of
the whole budget path, and the budget-off path (all regressions) must not evaluate any of it.

#### L3c — Deep mode: reallocate the freed budget into SA iterations (~120 LOC)

Today phase 1 forces `time_budget_sec = 0` for every leg (`search.cpp:1127`) and runs all 10 at
`options.iters = 1000`. On case5 that is a fixed 10 x 1,500 = ~15,000 SA iterations regardless of the
600 s budget. Design:

1. Add `double sa_wall_time_sec` to `SearchResult` (`search.hpp:69-195`), stamped in
   `runPlacementSearch` around the SA loop (`search.cpp:713-775`), summed across sub-runs in
   `runHybridPlacementSearch` (`search.cpp:997-1000`).
2. In `runPricingPortfolio`, leg 0 runs **exactly as today** (the probe, and the anytime-output
   guarantee — `PortfolioFold::admit` fires `on_improved` on the first leg, `search.cpp:1064-1073`).
   Record `T0 = leg_wall_times[0]`, `S0 = leg_result.sa_wall_time_sec`, `M0 = T0 - S0`.
3. **Deep-mode trigger** (budget-on only, first operand short-circuits):
   `options.time_budget_sec > 0.0 && variants.size() * T0 > options.time_budget_sec`
   — i.e. "the registry provably cannot fit the budget". On case5 with L3a: `T0 ~ 35 s`,
   `10 * 35 = 350 < 600` — so **L3a alone may take case5 out of deep mode**. That is fine and is the
   point: if the registry fits, run the registry. Keep the trigger as the guard for the case where it
   still does not fit.
4. In deep mode, run `N_deep = clamp(floor((budget - T0) / (kPhase2LegMargin * M0)), 1, 3)` further legs
   from a small explicit `deepVariants()` list — **`{(official,v1,credit-off,moves-off,ftagg-ON),
   (official,v2,credit-off,moves-off,ftagg-ON), (official,v2,credit-off,moves-on,ftagg-off)}`** per L2b
   and L1b (credit off when `cheap_proxy_v2` is on) — each with
   `leg_options.iters = 2000000` and
   `leg_options.time_budget_sec = max(0, (budget - elapsed) / N_deep - kPhase2LegMargin * M0)`.
   `runPlacementSearch` already honours this exactly: `iter_bound` becomes 2e6 (`search.cpp:709`) and the
   `(iter & 63)` wall check (`search.cpp:714-718`) ends the SA loop, after which the (now cheap, per L3a
   and L3b) machinery runs.
   Note `runHybridPlacementSearch` splits a leg's budget 0.5/0.25/0.25 across its three sub-runs
   (`search.cpp:938-939`); `M0` already measures all three sub-runs' machinery, so the arithmetic is
   consistent.
5. Phase 2 is skipped in deep mode (the existing `phase1_complete` gate at `search.cpp:1181` already does
   this whenever phase 1 stopped early; extend it to deep mode).

**Arithmetic on case5**: with L3a the machinery drops to ~10 s/leg; a 180 s SA slice at ~10,000
iterations/s yields **~1.8M iterations per leg**, vs 1,500 today — a **1,000x** increase in search
depth. Even at a pessimistic 2,000 iterations/s that is 360,000, i.e. 240x.

**Effect channel**: throughput. **Only valuable after L1.** **Effort**: ~175 LOC total across the three
pieces. **Risk**: low-medium; the entire mechanism is inside `if (options.time_budget_sec > 0.0)` so every
regression script (all use explicit `--iters`) stays byte-identical.
**Abort criteria**: if deep mode ever produces a *worse* fold than phase 1 alone, that is a bug — leg 0
is always in the fold (`PortfolioFold::admit` is a value-gated total preorder, `search.cpp:1059-1074`),
so the result is structurally >= the probe leg. If wall time exceeds 700 s on case5, reduce `N_deep`.

---

### L4 — Shape-aware seeding, plus the SA throughput micro-fix

#### L4a — `rememberBounded` early-out (~4 LOC, do this first, zero risk)

`search.cpp:206-216` builds a `placementKey` (an `ostringstream` over 121 integers), copies a whole
`Placement` into the pool, sorts and resizes — **on every SA iteration** (`search.cpp:726`). Add:

```
if (pool.size() >= cap && value > pool.back().value) return;   // strictly worse can never enter
```

Provably a no-op: a strictly-greater value appended to a full, sorted pool lands last and is dropped by
the `resize`. Use `>` not `>=` so tie behaviour under the non-stable `std::sort` is untouched. Saves
~15-25% of SA iteration cost and removes a per-iteration allocation site.

#### L4b — Shape-aware seed placement (~150 LOC, `placement_engine.cpp` + one call site)

§3.4 shows a naive AR-flexible row pack already lands at 24.2M / 47.7M-equivalent. Build one
deterministically and use it as a seed state:

```
Placement shelfSeed(const Problem& problem, int nrows, double W);
// 1. partition blocks into nrows groups by descending area, greedy least-loaded (deterministic)
// 2. row height h_k = (sum of that row's areas) / W
// 3. FEASIBILITY: block i can sit in a row of height h iff sqrt(A_i)/2 <= h <= 2*sqrt(A_i)
//    (from ar in [0.25,4] and w*h = A). Reject/re-balance rows that violate it -- on case5
//    sqrt(A) spans 225..1392 (ratio 6.2 > 4), so the largest and smallest blocks CANNOT share a row.
// 4. emit rects left-to-right within each row, rows bottom-to-top
```

Then seed both engines from it. `SequencePairEngine::seedFromPlacement` (`placement_engine.cpp:685-702`)
and `BStarEngine::seedFromPlacement` (`placement_engine.cpp:926-945`) both call `baseState(problem)`,
which resets `soft_aspect` to `initialAspect` (all 1.0, `placement_engine.cpp:401-409`) — **the shapes are
thrown away**. Fix: after `seedFromPlacement`, stamp `state.soft_aspect[i] = rect.w / rect.h` for every
soft block (clamped to `[ar_min, ar_max]`). This is the single highest-value line in L4.

Sweep `nrows in {3,4,5,6}` x `W in {4200, 5300, 6400}` as *cheap-scored* seeds and keep the best 1-2 as
the initial states of the deep-mode legs (§L3c step 4). Deterministic; no RNG.

Optional follow-on if time allows (survey D3): a `mutateSoftRowSnap` move that sets a soft block's aspect
so its decoded height equals a neighbour's height in the last decoded placement. The B5c moves already
call `decode` inside `mutate` (`placement_engine.cpp:860,871`), so the pattern exists.

**Effect channel**: OA primarily, alpha*WL secondarily. **Risk**: low — an extra seed can only add a
candidate to a value-gated fold. **But it is only *selectable* once L1 lands**: under today's objective a
dense shelf scores catastrophically on the FT-bbox term and will be rejected.

---

## 5. §3 — Schedule and dependencies

```
        L4a  (throughput micro-fix)          -- independent, 30 min
        L2a  (ft_area_sum rung)              -- independent, 1 h
        L2b  (--ft-aggressive flag)          -- independent, 30 min
         |
        L3a  (overflow-gated pads)           -- independent, 1 h   <-- biggest single wall-clock win
         |
        L1   (cheap_proxy_v2: a+b+c)         -- the lever, 1.5 days
         |
        L3b/L3c (budget governor, deep mode) -- needs L3a's measurement, 1 day
         |
        L4b  (shelf seed + aspect stamp)     -- needs L1 to be selectable, 0.5 day
```

| Day | Owner | Work | Gate |
|---|---|---|---|
| **8/15 AM** | fast-worker | L4a + L2a + L2b + L3a, each its own commit, `make` warning-clean | byte-identity md5 of `.cfg` at `--iters 200 --seed 1` on all 8 cases with flags off; all `test/run_*.sh` green |
| **8/15 PM** | fast-worker | **Measurement M1**: case5 `--iters 50` vs `--iters 1000` (budget off) wall times -> isolates machinery vs SA cost, validating §2.3. Then case5 600 s with L3a on/off | L3a must cut case5 wall by >=40%; if not, re-derive §2.3 before proceeding |
| **8/16** | deep-reasoner + fast-worker | L1 implementation behind `--cheap-proxy-v2` | Exact-tier byte-identity: `run_selfconsistency_regressions.sh` + `run_cfg_score_regressions.sh` green; flag-off md5 identity on all 8 |
| **8/16 PM** | fast-worker | **Measurement M2**: case5 at fixed `--iters 1000` (budget OFF), v2 on vs off, seeds 1/2/3 | same work, different objective -> isolates L1. Expect OA down and routed WL down together |
| **8/17** | fast-worker | L3b + L3c deep mode | budget-off structural identity (P0a precedent); case5 600 s wall <= 700 s |
| **8/17 PM** | fast-worker | **Measurement M3**: case5 600 s, arms {baseline, +L3a, +L1, +L1+L2, +L1+L2+L3} | same-load batch per the B7 lesson; golden rescore each `.cfg` |
| **8/18** | fast-worker | L4b shelf seed + aspect stamp | case5 600 s A/B |
| **8/18 PM** | fast-worker | **Measurement M4**: full 8-case 600 s A/B, composite on vs off, same-load batch | Sigma must improve; per-case regressions inspected individually |
| **8/19** | orchestrator | Scoping decision (§6), freeze, `make static`, rockylinux:8 docker glibc check | all regressions + submission checklist |
| **8/20** | orchestrator | Buffer, resubmission verification, docs | |

Parallelisable by a mechanical executor without conflicts: L4a / L2a / L2b / L3a touch four disjoint
regions (`search.cpp:206-216`, `search.cpp:387-417`, `main.cpp:150`, `search.cpp:409`). L1 is a single-file
change to `eval.cpp`. L3c is a single-function change to `runPricingPortfolio`.

---

## 6. §4 — Guard rails and the scoping decision

### 6.1 Non-negotiable invariants

1. **Flag-gated with an `if`, never a multiply-by-one.** Every lever adds a `bool` to `Problem` or
   `SearchOptions` defaulting to today's behaviour. The acceptance gate is the repo's existing one:
   **md5-identical `.cfg` on all 8 cases at `--iters 200 --seed 1` with the flag off**
   (`eval.cpp:101`, `eval.cpp:196`, `search.cpp:1146` are the precedents to copy).
2. **Budget-off is structurally untouched.** Every L3 branch begins with
   `options.time_budget_sec > 0.0 &&` as the first operand so nothing past it is evaluated on the
   explicit-`--iters` path. All regression scripts use explicit seeds/iters, so they are unaffected by
   construction, not merely numerically.
3. **The exact tier is not modified by L1.** The `+`/`-` pair at `eval.cpp:200` and `eval.cpp:227` must
   stay exactly cancelling, with the same `(w, h)`. `run_selfconsistency_regressions.sh` and
   `run_cfg_score_regressions.sh` are the enforcement.
4. **Anytime output keeps working.** Leg 0 always runs to completion before deep mode
   (`search.cpp:1141-1157` loop guard `i > 0` is already the anytime guarantee); `PortfolioFold::admit`
   still drives `on_improved` on every improvement (`search.cpp:1064-1073`). Deep mode must not delay the
   first write.
5. **900 s per-case ceiling.** Deep mode reuses `kPhase2LegMargin = 1.5` (`search.cpp:1049`) for its own
   slice arithmetic. Target case5 <= 650 s so the 900 s limit keeps >=27% margin on an unknown judge
   machine.
6. **Static binary.** Nothing in this plan adds a dependency. All new code is arithmetic over existing
   headers (`ft.hpp`, `route_cost.hpp`).

### 6.2 How to scope L1 (the one real judgement call)

Three options, in order of preference:

- **(A) Measured-dominance auto-rule (recommended).** At the seed placement, compute both the legacy
  proxy and `outline_area + alpha * wire_proxy`. Enable v2 when
  `legacy_proxy > kProxyDominanceFactor * (outline_area + alpha * wire_proxy)`. §2.4b's table gives the
  observed ratios: `{case0: 0.9, case6: 4.0, case7: 4.7, case3: 12, case4: 12, case2: 37, case1: 70,
  case5: 107}`. A threshold of 8 selects `{1,2,3,4,5}`; a threshold of 20 selects `{1,2,5}`; a threshold
  of 80 selects `{5}` alone. **Set the threshold from the M4 measurement, not from this table** — pick the
  smallest threshold whose selected set is a per-case win. Generalises to hidden cases; deterministic;
  self-diagnosing. This is the same shape as the L6 value-symmetry auto-rule already adopted.
- **(B) Default-on.** If M4 shows Sigma improves and no case regresses, just turn it on. Cleanest, and
  §2.4b's table suggests it is plausible (6 of 8 cases have the same pathology).
- **(C) Structural rule** (`all blocks SOFT && n >= 20`) — selects case5 alone among the 8. Least risky
  for the known set, most brittle for hidden cases. Fallback only.

**Do not add L1 as an 11th registry leg.** On case5 the registry is the resource we are trying to free;
doubling it defeats L3.

### 6.3 Global abort criteria

- Any lever that cannot demonstrate flag-off md5 identity is reverted, not debugged under time pressure.
- If M2 (fixed-iters, objective-only A/B) does not show case5 improving, **stop the L1 line** and ship
  L2 + L3a + L4a only — those are cheap, low-risk, and L3a alone removes the 900 s timeout risk entirely.
- Freeze all code by end of 8/19. 8/20 is verification and buffer only.

---

## 7. Explicitly de-prioritised (and why), against the v3 survey shortlist

| Survey lever | Verdict for case5 | Reason |
|---|---|---|
| **B1 constant-gap negotiation** (#4) | **Skip** | case5's overflow penalty is 2.3M unmerged and **0** merged. There is nothing to win. May still be worth it for case4/case7. |
| **C1 MCF whitespace redistribution** (#1) | **Skip for case5** | Optimises coordinates for a *fixed topology*; case5's topology is the defect. It is also an exact-tier consumer competing for the budget L3 exists to free. |
| **B2 incremental exact tier** (#5) | **Skip** | High effort, medium risk, and after L3a the exact tier is no longer the wall-clock hog. Revisit only if L3a underdelivers. |
| **D2 internal-weight-ladder legs** (#3) | **Skip for case5** | With only 3-4 affordable legs, spending one on a weight variant is a poor trade against L3c's iteration depth. Cheap to add later for the small cases. |
| **A5 whitespace-shape scores** (#2, partial) | **Absorbed into L1c** | The cut-line density formulation delivers the same "the cheap tier can finally see channel shape" outcome in scorer units, with a direct link to the FT term. |
| **C3 LNS local-group re-placement** (#7) | **Skip** | Exact-tier consumer; same budget argument as C1. |
| **A4 joint discrete AR selection** (#6) | **Reduced to L4b** | The full ILP/DP is not needed; §3.4 shows a deterministic AR-flexible shelf already captures most of the value. |

---

## 8. Appendix — how the numbers in this document were derived

All figures are from direct computation over
`test/E_testcase_20260811/testcase_upload_20260712/2026 ICCAD_case.case5.csv` (and the other seven CSVs
for the §2.4b table), reproducing the solver's own arithmetic:

- FT: `ftTierRate` / `ftExtend` / `ftRequiredArea` transcribed from `src/ft.hpp:10-29`.
- FT-bbox proxy: the exact loop at `src/eval.cpp:104-115`, including the `max(1.0, ...)` bbox degeneracy
  guard and the `* 1000.0` weight.
- Congestion capacity: `25 * max(1, min(side))` from `src/eval.cpp:84-90`.
- WL: boundary-gap Manhattan, justified against `src/route_geometry.hpp:102-137` (guide-point chain) and
  its "path of <2 nodes has 0 wirelength" note at lines 93-94.
- Channel capacity: `25*h` horizontal / `25*w` vertical from `src/routing.cpp:544-564`.
- Layout families evaluated: (i) random squares in MAX, (ii) first-fit shelf of squares, (iii) AR-flexible
  row packs at `nrows in {4,5,6,7}` and `W in {3300..6000}`, (iv) a 40,000-step SA over
  (permutation, row cuts, W) under `A + 0.05 * gap-WL`.
- The 2-row degenerate optimum found by (iv) is precisely what motivated L1c: gap-WL alone, without a
  cut-line capacity term, prefers a zero-channel slab. The cut-line term is not optional.
