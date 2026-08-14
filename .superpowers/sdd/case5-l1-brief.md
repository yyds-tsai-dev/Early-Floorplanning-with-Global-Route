# case5 L1 — cheap-tier objective realignment (`--cheap-proxy-v2`)

**Basis**: `.superpowers/sdd/case5-design.md` §4-L1 (L1a/L1b/L1c/L1d) — read it in full first; this
brief only pins the code facts and closes the open implementation decisions. The design's §0/§2.4
carry the why (the v1 FT-bbox proxy is ~107× the entire real objective on case5 and its variation
40× the real dynamic range; congestion term identically zero; WL proxy blind to abutment).

## Flag

`Problem.cheap_proxy_v2` (model.hpp, default false, doc comment citing case5-design.md §2.4/§4-L1)
+ hidden `--cheap-proxy-v2` in main.cpp (same precedent block as `--undirected-bundles`). Every v2
behavior change sits behind `if (problem.cheap_proxy_v2)` branches (never multiply-by-one). Flag-off
byte-identity on all 8 cases is an acceptance gate.

## Pinned code facts (verified by the controller — build on these)

1. `cheapScore` resolves `(w,h)` at `eval.cpp:150-152` (`placement.outline_w/h`, else
   `usedOutline`). The existing L2 comment at `eval.cpp:170-175` PROVES this resolution is
   bit-identical to `routeAndScoreAt`'s own `(w,h)` parameters on every call path (stamped / unset /
   grow). So: give the new proxy the signature
   `double routeAwareProxyPenaltyV2(const Problem&, const Placement&, double w, double h)`;
   `cheapScore` passes its already-resolved `(w,h)`; `routeAndScoreAt` subtracts
   `routeAwareProxyPenaltyV2(scoped, placement, w, h)` with its own parameters at the `eval.cpp:227`
   site (branch: v2 flag → subtract v2, else subtract v1 — the add site at `eval.cpp:200` branches
   identically). The pair stays exactly cancelling by construction; the v2 function must be a pure
   deterministic function of its arguments (per-call locals only, no statics).
2. `routeAndScoreAt` overwrites `score.wirelength` with routed WL and recomputes `score.value`
   (`eval.cpp:232,256`), so L1b's cheap WL proxy never leaks into the exact tier — no cancellation
   needed for the WL change, only for the penalty term.
3. The exact-tier FT aggregate L1a must mirror is `routing.cpp:594-604`: per block,
   soft → `ftRequiredArea(base, ftExtend(nets_i, ftTierRate(block, nets_i)))` with
   `base = ft_base_area > 0 ? ft_base_area : area`; non-soft → `block.area`. Charge
   `kBetaFt * max(0.0, est_ft_area_sum - w*h)` (same constant the exact tier uses at
   `eval.cpp:247`).
4. The overflow shape for L1c's hard excess is `officialOverflowPenalty(R, w*h)`
   (route_cost.hpp) with `R = hard_overflow / max(1.0, cap_total)`.

## The three sub-changes (one commit, they only work together — design §4-L1 preamble)

- **L1b — boundary-gap WL** (replaces the wire term `eval.cpp:187-198` under v2): per bundle,
  `dx = max(0, max(a.x,c.x) - min(a.right(),c.right()))`, same for `dy`;
  `wire_proxy += (dx+dy) * nets`. The v2 branch does NOT consult `abutment_credit` (the credit
  approximates exactly this; document that it is inert under v2).
- **L1c — cut-line sweep** (replaces the v1 congestion term under v2): per design §4-L1c pseudo-code.
  Vertical cuts = midpoints between consecutive distinct block x-edges clipped to `(0,w)` (≤2n-1);
  per cut: `free_h = h - Σ heights of blocks covering the cut`, `cap = 25*free_h`,
  `demand = Σ nets of bundles whose src/tgt centers straddle the cut`, `excess = max(0, demand-cap)`;
  if any SOFT block covers the cut, distribute excess into `est_ft_nets[i]` weighted by covering
  softs' heights, else `hard_overflow += excess`. Symmetric y pass (free_w, weight by width).
  `cap_total` accumulates cap over both axes. Note `free_h` can go negative when blocks overlap
  mid-SA — clamp `cap` at ≥0 so an illegal intermediate can't earn negative capacity.
- **L1a — aggregate FT term**: from `est_ft_nets` per pinned fact 3, charged per pinned fact 3's
  formula. `est_ft_nets` is a per-call `std::vector<double>`.

The v2 penalty = L1c's `officialOverflowPenalty(...)` + L1a's `kBetaFt*max(0, ...)`. Nothing else —
the v1 FT-bbox loop and v1 congestion term are entirely replaced under v2.

## Gates (all must pass before the commit)

1. `make` warning-clean.
2. Flag-off byte-identity vs pre-L1 HEAD on all 8 cases (batch protocol from
   `.superpowers/sdd/case5-batch1-brief.md`: cases0-4+6,7 at `--iters 200 --seed 1`, case5 at
   `--iters 50 --seed 1` with ≥900000ms per-run timeout, per-case Bash calls, worktree baseline).
3. `bash test/run_selfconsistency_regressions.sh` and `bash test/run_cfg_score_regressions.sh`
   green — the exact-tier-untouched enforcement (design §6.1 item 3). Also
   `run_case00.sh`, `run_b2_regressions.sh`, `run_p1_regressions.sh`.
4. Flag-ON self-consistency spot check: case5 `--cheap-proxy-v2 --iters 50 --seed 1` → rc==0,
   `validation=ok`, and the golden rescorer (`bin/efgr_rescore` — see
   test/run_selfconsistency_regressions.sh for invocation) agrees with the solver score on the
   produced cfg (v2 changes the SEARCH, not the scoring — self-consistency must hold with the flag
   on too).
5. New regression tests (new file `test/regression_cheap_proxy_v2.cpp` + `test/run_cheap_proxy_v2_regressions.sh`,
   link line copied from run_undirected_regressions.sh's kitchen-sink set):
   - v2-off: `cheapScore` on a small hand-built problem equals the v1 value (guard the branch).
   - Cancellation: for a hand-built problem WITH the flag on, `exactScore`(or `routeAndScoreAt` via
     the public API) equals the same call with the flag off on the SAME placement (the proxy must
     cancel out of the exact tier both ways).
   - L1b zero-at-abutment: two abutting rects → v2 wire term 0; separated → equals gap*nets.
   - L1c: a hand-built two-block + one-bundle case where demand exceeds a cut's capacity with a soft
     block covering → est_ft flows into the FT term (penalty > 0 iff est_ft_area_sum > w*h);
     with no soft coverage → hard_overflow > 0.

## Measurement (M2 — run after the commit, report numbers, no adoption decision)

case5, fixed `--iters 1000`, budget OFF, seeds 1/2/3: v2 on vs off (six runs, each its own Bash
call, ≥1200000ms timeouts, same-load pairing — run the on/off pair for a seed concurrently, pairs
sequentially). Report score/OA/WL/overflow/ft_area_sum/wall per arm from the stderr summary +
golden-rescore each cfg. Expected direction (design §5 M2): OA down AND routed WL down together.
Abort criterion (design §6.3): if v2 does not improve case5 here, STOP — report, do not tune.

## Out of scope

Adoption wiring (auto-rule/threshold — §6.2 decides AFTER M4), registry/variant changes, L3b/L3c,
any exact-tier change.
