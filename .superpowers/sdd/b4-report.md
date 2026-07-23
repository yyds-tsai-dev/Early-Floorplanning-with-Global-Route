# B4 Report — Official per-edge marginal pricing in the cost-driven router

Status: **DONE, landing with an honest known limitation** (per coordinator adjudication, 2026-07-24).
Implemented exactly per `.superpowers/sdd/b4-design.md`; all 14 suites green, warning-clean. The
5-case default sweep shows 4/5 cases improve substantially (Σ −8.87%) but **case3 regresses
end-to-end (+7.46%)** — a proven search-trajectory effect, not a router-correctness defect (see
"The case3 finding" below). B4.1 (next commit) restores the per-case ≤-pre-B4 guarantee via a
route-pricing portfolio.

## What changed

- **`src/route_cost.hpp`**: added `marginalOverflowNets(ch, horizontal, usage, n)` (design §3.2,
  exact piecewise marginal overflow in NETS, RAW caps); `ftMismatchDelta(...)` (design §5, the
  unweighted Δmismatch core, factored out of the retained `softFtMarginalCost` so the router can
  weight it by the official `kBetaFt` without duplicating tier math); `channelPathSlack(pred_rect,
  ch, succ_rect)` (design §4, the both-edges/halving `pathSlack` core, promoted to a free function
  specifically so it is directly unit-testable — it would otherwise be an unreachable private
  lambda); `roundCostOfficial(rr, alpha, outline_area)` (design §7.2, the re-denominated
  comparison-gate/round-selection key, likewise promoted to a free function so its algebraic tie to
  `exactScore` is testable against the actual production formula, not a reimplementation). The old
  `kOverflowAreaWeight`/`kFtMismatchWeight`/`channelMarginalCost`/`channelOverflowArea`/
  `softFtMarginalCost` are KEPT, no longer called by the router, retained per design §7.4 for their
  existing T1 unit fixtures (and, as it turned out, for B4.1's `route_pricing_official=false` path).
- **`src/routing.cpp`**: `outline_area`/`raw_capacity_total`/`p_ovf` computed once near the top of
  `routeProblem`. Channel-hop pricing gained the symmetric `[u is channel]` exit clause alongside
  the existing `[v is channel]` entry clause (design §3.3) — both now price
  `p_ovf * marginalOverflowNets(...)` + `p_ovf * history * n`. FT pricing now prices
  `kBetaFt * ftMismatchDelta(...)` + `kBetaFt * history_ft * n` (design §5). `pathSlack`'s channel
  branch now calls `channelPathSlack` (both-edges halving, design §4). `roundCost` now calls
  `roundCostOfficial` (design §7.2). Legacy path (`cost_driven=false`), negotiation structure,
  chunking constants, and commit accrual (`route_geometry.hpp`, untouched) are unchanged.
- **`test/regression_p2_20260707.cpp`**: 24 → 33 tests. 9 wholly new (4 for `marginalOverflowNets`,
  2 for `channelPathSlack`, 1 reverse FT-vs-channel fixture, 1 fresh straight-pass-through fixture,
  1 algebraic-tie fixture); 4 re-derived (details below); the rest unchanged.

## Fixture results (design §8, items i–v)

All confirmed via the actual built router (measured, not hand-derived, for anything involving the
negotiation/chunking loop — matching this codebase's own established practice of scratch-probing
before transcribing numbers into permanent tests).

- **(i) Straight pass-through, both-edges pricing** — `testStraightPassThroughBothEdgesPricingSaturatesChannelExactly`
  (new, minimal fixture: `cap_H=100`, bundle=100). B4: `pathSlack` returns 50, chunk1=50 saturates
  the channel exactly (`hu=100=cap`, `overflow_nets=0`), chunk2=50 diverts to FT. Analytically, the
  OLD (pre-B4) code's unhalved `pathSlack` would have returned 100 (both old and new marginal-cost
  functions price a probe of exactly 100 against a cap of exactly 100 as free, so old code sees no
  reason to split), committing the whole bundle in one chunk and overshooting to `hu=200`
  (`overflow_nets=100`) — exactly design §2.2's overshoot.
- **(ii) L-turn split-axis pricing** — covered via the re-derived `testComparisonGateKeepsCostDrivenWhenItWins`
  (the pre-existing `ElbowMispricingFixture` IS an L-turn scenario: CH1's V-axis is pre-saturated by
  a straight filler, the elbow bundle enters CH1 horizontally and would exit vertically if it took
  CH1 at all). Under B4 the router now avoids creating ANY overflow here (measured: 0, vs the
  pre-B4 code's own residual 4,880µm²) — both-edges pricing correctly counts the already-saturated
  V axis this hop would land on, which the old entry-axis-only code never priced. New direct unit
  coverage of the underlying mechanism: `testChannelPathSlackLTurnTighterAxisBinds` (hand-built
  rects/channel, design §4's own worked numbers: `cap_H-hu=100, cap_V-vu=30` → slack 30, the tighter
  axis binds).
- **(iii) `pathSlack` halving (unit)** — `testChannelPathSlackStraightPassThroughHalves` (100→50) and
  `testChannelPathSlackLTurnTighterAxisBinds` (100/30→30), calling the newly-promoted
  `channelPathSlack` directly (design's own worked numbers verbatim).
- **(iv) FT-vs-channel flip, both directions** — Forward: `testCostDrivenPrefersFtOverSaturatedChannel`
  (the ONE fixture design §8 names for re-derivation) re-baselined to official units: cost-driven
  now drives overflow to **exactly 0** (not merely "lower than legacy") by filling the channel to
  capacity for free then diverting the remainder to FT (`soft_feedthrough_nets[S]=2600`,
  `total_wirelength=1,533,000`); added the design §5 per-net breakeven cross-check
  (`Δreq/n < (20/3)(A/C)`) using the fixture's own measured `A`/`C`. Reverse (new, guards against
  "always-FT"): `testCostDrivenPrefersChannelWhenFtExpensiveUnderOfficialRate` — same geometry,
  `ft_base_area=5e8` (steeper FT growth, same A/C) — the router now prefers the channel outright
  (`soft_feedthrough_nets[S]==0`), proving the choice is a genuine exchange-rate comparison, not a
  fixed bias.
- **(v) Algebraic tie to the scorer** — `testRoundCostOfficialAlgebraicallyTiesToExactScore`:
  `roundCostOfficial(routing, alpha, outline_area).second == exactScore(...).value - outline_area`,
  measured diff **0.0000** (double precision) on a routed, `open_routes==0`, legal fixture
  (`SoftFtRoundConsistencyFixture` with its placement's outline stamped to the problem's own
  declared outline, so `exactScore` takes the direct `routeAndScoreAt` path with no
  `shrinkWrapOutline`/corrective-grow indirection). This is the hard proof that the gate's guarantee
  is real (design §7.2/R2).

## Existing-fixture re-baseline — two more than the brief named (deviation, explained)

The brief named exactly one fixture (`testCostDrivenPrefersFtOverSaturatedChannel`) as needing
re-derivation. Building and testing surfaced two more that also encode pre-B4-pricing-specific
numbers (both choice-based, not accrual-based, so this is squarely within the design's own §7.4/§8
"choice-based assertions move" carve-out — just a larger set than the brief anticipated):

1. **`testAdaptiveChunkingSplitsAtBottleneck` → renamed `testBottleneckFixtureNowDivertsEntirelyToFtUnderOfficialPricing`.**
   Its whole premise was the entry-axis-only bug (its own old comment said so: "pathSlack still
   sizes chunks by entry-axis capacity (documented deferred gap)"). Under B4, both-edges pricing
   makes even a single net of channel overflow on this geometry cost more than diverting the ENTIRE
   remaining bundle to FT — so the very first probe (priced at the full 3,000 nets) already finds FT
   cheaper for the whole bundle, and the channel is never touched at all (measured:
   `routes.size()==1`, `soft_feedthrough_nets[S]==3000`, channel `hu=vu=0`,
   `feedthrough_mismatch=12,023.39`). Not a defect — B4 correctly recognizes bottleneck-splitting is
   no longer the cheapest option for this specific bundle/channel/FT combination. Fixture (i) above
   (fresh, minimal, round numbers) is what now demonstrates the genuine 2-chunk
   fill-then-divert split design §8 asks for.
2. **`testComparisonGateKeepsCostDrivenWhenItWins`** (elbow_nets=300, unrenamed) — numbers updated:
   cost-driven's overflow is now exactly 0 (was a residual 4,880µm²); legacy's numbers are unchanged
   (legacy pricing is untouched by B4). Gate comparison now uses `roundCostOfficial` directly instead
   of hand-computing the old `kOverflowAreaWeight`-based key.
3. **`testComparisonGateSwitchesToLegacyWhenCostDrivenFullyRoutedButWorse`** — `elbow_nets` moved
   from 1000 to 50000. B4's pricing is effective enough at this fixture's scale that cost-driven
   keeps winning outright up through at least 10,000 nets (swept empirically); the `used_fallback`
   branch's only remaining trigger in this fixture family is a much larger elbow where both
   channels the filler/elbow can use saturate heavily regardless of pricing model.

Untouched (confirmed still valid, no changes needed): T0/T1 accrual/pure-function fixtures,
`testCostDrivenZeroWlDirectAbutUnchanged`, `testCostDrivenDeterministic`,
`testFixedSplitUnchangedForCostDrivenFalse`, `testAdaptiveChunkingRespectsMaxChunksWithNoAlternative`
(`kMaxChunks`'s last-slot-takes-remainder rule structurally caps chunk count regardless of
`pathSlack`'s values — confirmed: still exactly 16 chunks), `testAdaptiveChunkingDeterministic`,
`testNegotiationClearsRoundZeroOverflow`/`testNegotiatedRoutingDeterministic` (both fields the
fixture asserts — `vertical_usage`, `channel_overflow`, `feedthrough_mismatch`, `used_fallback` —
measured unchanged), `testFeasibilityFallbackNeverWorseThanLegacyOnGenuineDeadEnd`,
`testComparisonGateDeterministic`, `testRoundConsistencyOfSoftFeedthroughFields` (a pure
self-consistency check, independent of which routing decision the router makes).

## Suites

`make`: zero warnings. All 14 suites green:

```
PASS run_case00
PASS run_placement_engine_regressions
PASS run_20260618_regressions
PASS run_20260608_regressions
PASS run_p0_regressions
PASS run_p1_regressions
PASS run_p2_regressions        (33/33 fixtures)
PASS run_p3_regressions
PASS run_testdata_regressions
PASS run_cfg_score_regressions
PASS run_selfconsistency_regressions   (5/5 — required per brief, unaffected: commit accrual untouched)
PASS run_integer_output_regressions
PASS run_b1_regressions        (old, looser upper bounds — satisfied by a wide margin)
PASS run_b3_regressions        (case2 pin — satisfied)
```

## End-to-end: 5-case default matrix (hybrid, iters1000, seed1, golden-rescored)

"Before" independently re-measured at HEAD `207d08c` via `git stash`/rebuild/rescore (not just
copied from the brief) — matched the brief's stated baseline exactly, cross-confirming both.

| case | before checker_cost | after checker_cost | Δ | Δ% | before ovf_nets / R | after ovf_nets / R | before WL | after WL |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| 0 | 5,389,935.000 | 5,233,375.000 | −156,560.00 | **−2.91%** | 0 / 0 | 0 / 0 | 3,465,600 | 3,634,675 |
| 1 | 13,246,805.650 | 9,100,697.980 | −4,146,107.67 | **−31.30%** | 0 / 0 | 1 / 3.713e-6 | 31,742,368 | 11,968,280 |
| 2 | 59,420,652.769 | 56,045,213.300 | −3,375,439.47 | **−5.68%** | 1,831 / 0.001715 | 0 / 0 | 132,650,006 | 121,687,467 |
| 3 | 36,615,637.236 | 39,346,073.615 | +2,730,436.38 | **+7.46%** | 6,703 / 0.010949 | 1 / 1.7e-6 | 75,865,418 | **100,303,114** |
| 4 | 89,523,552.207 | 76,369,838.931 | −13,153,713.28 | **−14.69%** | 48,825 / 0.022185 | 3,703 / 0.001674 | 259,384,183 | 245,431,105 |
| **Σ** | **204,196,582.86** | **186,095,198.83** | **−18,101,384.04** | **−8.87%** | | | 503,107,575 | 483,024,640 |

The core mechanism validates exactly as designed: `overflow_nets` collapses to near-zero in 4/5
cases (case2 fully cleared 1831→0; case3 6,703→1; case4 −92.4%), and total WL drops −3.99% in
aggregate, confirming the design's prediction that cheaper, correctly-denominated overflow pricing
frees the router from unnecessary WL detours.

## The case3 finding

Case3 regresses end-to-end (+7.46%), driven entirely by WL (+32.2%, 75.87M→100.30M) despite both
overflow (6,703→1 net) and FT mismatch (area 187,401→23µm²) improving sharply. This is the brief's
named STOP condition ("any per-case end-to-end regression at default flags").

**Diagnostic (decisive): replay the pre-B4 winning placement through the B4 code.** Took case3's
pre-B4 winning placement (frozen from its `.cfg` BLOCK rects) and re-routed/re-scored that EXACT
SAME placement through the built B4 code, using the SAME checker formula as `cfg_score.cpp`
(`A + alpha*WL + kBetaLin*A*R + kBetaTail*A*max(0,R-0.05)`, no FT term, for apples-to-apples
comparison with the golden-rescored `checker_cost` — `exactScore.value` is not directly comparable
here since it additionally includes `kBetaFt*mismatch`):

```
Same placement, pre-B4 routing:  checker_cost      = 36,615,637.24
Same placement, post-B4 routing: checker-equivalent = 35,748,021.84   (−2.37%, STRICTLY BETTER)
```

**The per-placement gate floor holds** — B4's routing of the identical geometry is strictly better,
exactly as the `roundCostOfficial`/`exactScore` algebraic tie guarantees. The end-to-end regression
is therefore NOT a router-correctness defect; it is the search (hybrid SA) converging to a
genuinely different, WL-costlier final placement under B4's re-priced landscape. This is precisely
the risk the design document itself names and explicitly declines to fix within B4's scope (§6,
§9.2 R4): *"B4's overflow re-pricing drops the channel term from ~4M/net to ~200/net, so after B4
the WL proxy is a larger relative share of each routing decision than before... The center-manhattan
proxy therefore matters more post-B4 — a further argument to sequence the B6 τ-harness + B5 proxy
work right after B4, not a reason to widen B4."*

Diagnostic tooling (`/tmp/b4/replay_placement.cpp`, `/tmp/b4/probe.cpp`, `/tmp/b4/sweep.sh`) is
scratch-only, not committed, per the brief's environment convention.

## Coordinator adjudication (2026-07-24)

Per direct coordinator message: the stop was correct and the replay diagnostic settles the
question. Landing decision: **B4 lands as-is** (this commit); **B4.1 follows immediately** (next
commit) to restore the per-case ≤-pre-B4 guarantee via a route-pricing portfolio (run the full
search under both official and legacy router pricing, fold by value — the legacy leg reproduces
pre-B4 behavior bit-exactly since B4 never touched `search.cpp`, so the fold's floor is structural).
See the B4.1 section below for that work.

## Commit

`feat(routing): official per-edge marginal pricing + scorer-faithful round gate (B4)` — files:
`src/route_cost.hpp`, `src/routing.cpp`, `test/regression_p2_20260707.cpp`,
`.superpowers/sdd/b4-report.md`, `.superpowers/sdd/progress.md`.
