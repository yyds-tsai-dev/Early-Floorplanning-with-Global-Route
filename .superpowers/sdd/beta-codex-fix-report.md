# beta Codex-fix report: corrective-grow effective placement returned to callers

Commit: `bcb5c90` — `fix(eval): return corrective-grow effective placement to callers`
Branch: `fable5` (parent `da8f940`). Status: DONE, all gates green.

## The bug (re-verified against code)

`exactScore` (src/eval.cpp:190-261) works on `Placement local = placement`. When the tight declared
outline has `open_routes > 0`, the one-step corrective grow builds `Placement regrow = local`,
`anchorAtOutline`s it at a larger integer-ceiled outline (re-anchoring EDGE blocks), re-routes, and on
`prefer_grown` sets `local = regrow; score = grown_score; routing = grown_routing`. Pre-fix the
function returned only `Score` + `*routing_out` — never `local`. Every caller kept its OWN input
`Placement` while storing the returned score/routing, so a winning grown candidate emitted BLOCK rects
from the tight geometry paired with CHANNEL/PATH lines from the grown geometry. `validateSolution`
never cross-checks channel geometry against paths, so the inconsistent cfg still exits 0; the official
checker would see paths referencing channels outside the emitted blocks' outline.

## Fix shape

1. `exactScore` gains `Placement* placement_out = nullptr` (decl eval.hpp, defn eval.cpp) and does
   `if (placement_out) *placement_out = local;` — the placement actually scored/routed (grown or not).
2. The propagation could not stop at the direct call sites: `grep exactScore src/*.cpp` finds 5 sites
   in search.cpp, but two of them go through the `ExactCachedFn` cache callback, and its two consumers
   (`outlineSweep` in search.cpp, `respaceFixedPoint` in respace.cpp) store the INPUT placement while
   keeping the returned routing — the same bug one level up. So `ExactCachedFn` (search.hpp) was
   widened to `Score(const Placement&, RoutingResult*, Placement*)` and threaded through.

## Cache-semantics decision

Both exact-eval caches (`exact_cache` in `runPlacementSearch`, `inflated_exact_cache` in
`finalizeFeedthrough`) now store a `(Score, RoutingResult, Placement)` triple (`ExactEvalEntry`,
file-local struct in search.cpp) **keyed by `placementKey(INPUT placement)` — unchanged**. Dedupe
semantics are therefore identical; the added value is the effective placement. Because `exactScore`
is deterministic, the input→triple mapping is deterministic, so a cache hit reproduces the exact same
(score, routing, effective placement) a miss would have produced. Key stays the input placement (not
the effective one) so two different inputs that grow to the same geometry still dedupe by input, and
callers that look up by their own candidate always get a hit.

## Call-site inventory (what each now stores)

| Site | File:region | Now stores |
|------|-------------|-----------|
| `exactCached` lambda | search.cpp ~388 | triple in `exact_cache`; returns effective via out-param |
| seed best | search.cpp ~405 | `result.best_placement = effective` (temp, then assign) |
| cheap-pool seed | search.cpp ~431 | `rememberCandidate(current_place,…)` — kept as the cheap-scored placement, NOT best_placement, so a grown seed never mis-pairs a grown placement with the ungrown cheap score (byte-identical when grow off) |
| main-loop best | search.cpp ~449 | `result.best_placement = effective` |
| `outlineSweep` main rung | search.cpp ~209 | `OutlineSweepResult{score, effective, routing}` |
| `outlineSweep` fallback | search.cpp ~229 | `OutlineSweepResult{score, effective, routing}` |
| elite adopt (raw+compacted) | search.cpp ~451/463 | already reads `swept.placement` = effective (fixed inside outlineSweep) |
| `maybeAdoptRespace` | search.cpp ~133 | reads `respaced.placement` = effective (fixed inside respaceFixedPoint) |
| `finalizeFeedthrough` decode | search.cpp ~294/328 | `decode_effective` → `tryCommit` |
| `finalizeFeedthrough` inflatedExact | search.cpp ~310 | triple in `inflated_exact_cache` |
| `finalizeFeedthrough` compacted | search.cpp ~342/343 | `compacted_effective` → `tryCommit` |
| `finalizeFeedthrough` repair | search.cpp ~361/362 | `cand_effective` → `tryCommit` |
| `respaceFixedPoint` baseline | respace.cpp ~421 | `best.placement` receives effective (seeded from base, overwritten by exactCached) |
| `respaceFixedPoint` per-iter | respace.cpp ~438 | `best.placement = candidate_effective`; loop's `current` intentionally stays the un-grown candidate (relax seed) |
| hybrid-leg arbitration | search.cpp ~505 | unchanged — it selects a whole SearchResult; each leg's best_placement is already consistent |

Direct `exactScore(...)` calls in tests (p0:136/263, p1:536/676, p3:563/713/790/947/974) are unchanged
— they rely on the new `placement_out` default of `nullptr`. Five test lambdas that build an
`ExactCachedFn` (or a raw callable passed where one is expected) were updated to the 3-arg shape:
p1 `mockExactCached`/`probe`/`exactCached` (the last made a faithful triple cache), p3 two forwarders.

## Part D (integrality) verification

When the grow fires and is adopted, `local.outline_w/h` = the outline `anchorAtOutline` stamped, which
= `ceiling.outline_w/h` = `grown_w/grown_h`, both `std::ceil(... - kEps)` clamped to the integer MAX
(eval.cpp:225-226). `routeAndScoreAt` never mutates `regrow`'s stamp. So the surfaced effective
placement carries an integral outline stamp; the new regression asserts `outline == floor(outline)`.

## Deviation from the brief's staging list (documented)

The brief said "Stage only src/eval.hpp, src/eval.cpp, src/search.cpp (+ test files)". The correct,
complete fix additionally required **src/search.hpp** (the `ExactCachedFn` typedef) and
**src/respace.cpp** (`respaceFixedPoint` stores `RespaceResult.placement`, which becomes
`result.best_placement` via `maybeAdoptRespace`). The brief's own requirement #2 ("update every call
site that STORES the score/routing … also store the effective placement") mandates the respace change;
`grep exactScore src/*.cpp` does not surface it because respace calls the `ExactCachedFn` callback, not
`exactScore` directly. Staged set: the 7 files above only. The hard rule (NEVER stage CLAUDE.md /
docs/ / .superpowers/) was honored; this report lives in `.superpowers/sdd/` and is intentionally NOT
staged.

## Gate results (all green)

- `make`: warning-clean (`-Wall -Wextra -pedantic`), exit 0.
- 12-suite battery: case00, placement_engine, r20260618, r20260608, p0, p1, p2, p3, testdata,
  cfg_score, selfconsistency, integer_output — all PASS.
- New regression `testCorrectiveGrowPlacementConsistency` (appended to test/regression_p1_20260706.cpp)
  PASS: non-vacuous fixture (tight route open, grow clears it), asserts channels ⊂ effective outline,
  re-route of effective reproduces open_routes==0, validateSolution ok, effective outline integral.
- Packaging (`/tmp/beta_package.sh`): 5/5 exit 0, build warning-clean, all cases fractional-lines=0.
- Self-consistency: 5/5 (solver == golden re-scorer to ≤1e-6 rel, 0 open routes).

## Did any suite value shift? No.

No suite asserting exact internal scores changed. Definitive check: built the pre-fix binary from a
detached worktree at `da8f940`, ran all 5 contest cases at default (no-flag) settings pre and post,
byte-diffed the cfgs — **case0..case4 all IDENTICAL**. This proves (a) requirement #3 (byte-identical
output when the grow does not fire on the winning path) and (b) the report question below.

## Does the grow path fire on the 5 cases at default settings?

Not on the winning candidate of any of the 5 cases at default settings — the pre/post cfgs are
byte-identical, which could not hold if a winning candidate had been grown+re-anchored. The corrective
grow is a latent last-resort path (the outlineSweep margin ladder and the router's channel-first +
soft-FT fallback keep winning candidates routable at their tight stamp), so this fix changes no contest
output today; it removes a real consistency hazard that a future placement/routing change could expose.
The synthetic regression is what exercises the grow firing.

## Concerns / residual notes

- `respaceFixedPoint`'s loop `current`/`current_routing` still advance from the un-grown candidate (the
  relax seed), by design — pairing an effective (grown) placement into best is what matters for
  emission; the relax trajectory is quality-only and byte-identical when grow is off. Not a correctness
  gap.
- `best_state` in the main loop still tracks the ungrown topology (it is `(void)`-discarded), so no
  attempt is made to keep it in sync with a grown effective placement.
