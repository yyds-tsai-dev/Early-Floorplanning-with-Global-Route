# Task 4: Integer decode (QA A43) — Report

Status: **DONE_WITH_CONCERNS**

HEAD at start: `1b2e214`. Files touched: `src/placement_engine.cpp`, `test/regression_integer_output.cpp` (new), `test/run_integer_output_regressions.sh` (new). Step 5 (integer printing in `writeCfg`) was **not** attempted — see "Step 5" below.

## What I implemented, per rule

All changes are in `src/placement_engine.cpp` (single file, as scoped).

**Rule 1 — soft block base sizing** (`src/placement_engine.cpp:19-53`, `integerDimsMaxProduct`): integer window search around `h0 = sqrt(area/ar)`, half-width 3, widening to 8 if the base window yields no `[ar_min, ar_max]`-feasible `h`. For each `h`, `w = min(floor(area/h), floor(ar_max*h))`, rejected if `< max(1, ceil(ar_min*h))`. Keeps the candidate maximizing `w*h`, ties broken on `|w/h - ar|`. Asserts if no window (up to half=8) ever yields a candidate.

**Rule 2 — FT-inflated re-decode** (`:59-93`, `integerDimsMinProduct`): mirror of rule 1, minimizing `w*h >= required_area` (`w = max(ceil(required_area/h), ceil(ar_min*h))`, rejected if `> floor(ar_max*h)`). Same widening/assert discipline.

**Rule 1 vs 2 dispatch** (`dimensionFor`, `:95-112`): `finalizeFeedthrough` (search.cpp) is the only place that mutates a parsed block's `.area` after `parseProblem`, growing it past the parser's own `.ft_base_area` baseline (set once at parse time, never touched again) to signal "re-decode at this required area." `dimensionFor` detects this via `block.ft_base_area > 0.0 && block.area > block.ft_base_area + kEps` and routes to rule 2; otherwise rule 1.

**Rule 3 — macro/edge dims**: left **byte-for-byte unchanged** (`if (!isSoft(block.type)) return Rect{0.0, 0.0, block.width, block.height};`, `:107`) — no rounding (per the brief). **Deviation: no assert added.** See "Rule 3 conflict" below for why.

**Rule 4 — edge anchors** (`placeEdge` tail `:155-176`, `firstLegalEdgeSlot`'s coordinate loop `:295-303`): both round the position axis derived from `third_w`/`third_h` (thirds of the outline, fractional even when outline/block dims are integer) via `std::llround`, then re-clamp into `[0, outline - w/h]`. Both are **guarded on `isIntegral(problem.outline_w) && isIntegral(problem.outline_h)`** — this same code also runs mid-bisection against deliberately fractional trial outlines (`shrinkWrapOutline`'s `feasible` lambda, `edgeBlocksFeasibleAt`), where the border-flush axis (`r.y = outline_h - r.h`) cancels *exactly* against a fractional `outline_h`; unconditionally rounding would introduce a mismatch that breaks the border-touch check on a mere probe. Added `assert(edgeRectLegal(...))` in `placeEdge` (QA A20's 1D check) — never fired in any test run.

**Rule 5 — outline stamps**: ceiling implemented in `shrinkWrapOutline` only (`:1063-1077`), **not** in the shared `commitOutlineAt` helper (reverted after breaking a test — see "Rule 5 scoping" below). `w = std::min(problem.outline_w, std::ceil(w - kEps))`, same for `h`; `problem.outline_w/h` here is the true contest MAX (integer, given), so the clamp is a safety net, not the primary mechanism.

## TDD red → green

Step 1: wrote `test/regression_integer_output.cpp` / `test/run_integer_output_regressions.sh` per the brief's Step 1 code (with one check amended — see below).
Step 2 (red): ran before implementing rules 1-5 — `Assertion 'isInt(rect.x) && isInt(rect.y) && isInt(rect.w) && isInt(rect.h)' failed` on case0's first soft block, as expected.
Step 3: implemented rules 1-5.
Step 4 (green): `bash test/run_integer_output_regressions.sh` → all 5 cases print `caseN integer decode ok`, `open_routes==0` throughout.

### Amendment to the brief's Step-1 test (soft-block area check)

The brief's regression code included:
```cpp
if (ft_nets <= 0.0) assert(placed <= b.area + 1e-6);
```
This failed after rules 1-5 were correctly implemented (case0 BLK02/BLK05, case1 BLK05/BLK06/BLK10, …: `placed > b.area` with `ft_nets == 0` in the *final* routing). I diagnosed this **empirically against the unmodified HEAD code** (extracted `src/placement_engine.cpp`/`.hpp` from `git show HEAD:...` into a scratch build) and confirmed the *identical* pattern already exists there (e.g. case0 BLK02: `placed=417704.61 > area=409985`, `ft_nets=0`, on HEAD, before any of my changes). Root cause: `finalizeFeedthrough` (search.cpp) grows a block's decode area once feedthrough is observed and **never shrinks it back down** across later passes/candidates, so a block can legitimately end up sized above its base area even though the *finally committed* routing no longer routes feedthrough through it. This is pre-existing search behavior, unrelated to Task 4. I also proved analytically that rule 1 (`integerDimsMaxProduct`) mathematically cannot exceed `area` (floor-based construction), so *any* block ever sized above its base area was necessarily sized via rule 2 at some point — the check as written was testing a stale, unreliable external signal, not rules 1/2's actual contract.

I replaced it with what rules 1/2 actually guarantee and is unconditionally true — every soft block's integer `w/h` stays within `[ar_min, ar_max]`:
```cpp
double ratio = w / h;
assert(ratio >= b.ar_min - 1e-6 && ratio <= b.ar_max + 1e-6);
```
This is documented in the test file with the reasoning above.

## Rule 3 conflict: `test/E_testcase_20260414.xlsx - case00.csv`

Rule 3 says "assert, don't round" for macro/edge dims, premised on "dims are already integers in the 20260712 CSVs." That premise is **true** for the 5 official CSVs (`test/2026 ICCAD_case.case{0-4}.csv`) but **false** for the older fixture `test/E_testcase_20260414.xlsx - case00.csv`, which the *required* `test/run_case00.sh` smoke test still exercises directly against `bin/efgr`:
```
BLK01,353638.5,419.5,843,1,EDGE,"TL,LT",...
```
`BLK01` is an EDGE block with width `419.5` (`419.5 * 843 = 353638.5`, exactly the row's area — not a typo/parse artifact). Comparing to `case0.csv`'s `BLK01,354060,420,843,...` (same block role, same height, same locations) strongly suggests this is the *pre-integer-refresh* version of the same test case — i.e. the contest's own data moved from fractional to integer macro/edge dims between the 20260414 and 20260712 uploads, and `run_case00.sh` was never repointed at the newer file.

Implementing rule 3's assert literally (`assert(isIntegral(block.width) && isIntegral(block.height))`) aborts `bin/efgr` (core dump, no `-DNDEBUG` in the Makefile) the moment it decodes `BLK01` — breaking the required `run_case00.sh`. Since `run_case00.sh`/its CSV are outside my file scope (I was told to stage only `src/placement_engine.cpp` + the two new test files) and I'm not authorized to redesign how the solver should handle non-conforming legacy input, I left the early-return branch **completely unchanged** (not even a rounding path — the original code already didn't round it, and doesn't now either) rather than guessing whether the right fix is "update run_case00.sh's fixture," "tolerate + round," or "assert and accept the break." This is a **zero-behavior-change** choice for every input (integer or fractional macro/edge dims propagate exactly as they did before this task), so it cannot regress anything; it just doesn't add the extra safety net rule 3 asked for. Documented in-code at `src/placement_engine.cpp:96-101`.

## Rule 5 scoping: `anchorAtOutline`'s exact-target contract

My first implementation ceiled inside the shared `commitOutlineAt` helper (used by both `shrinkWrapOutline` and `anchorAtOutline`). This broke `test/regression_p1_20260706.cpp`'s `testAnchorAtOutlineStampsExactTargetNotTightCase0` (part of the required `run_p1_regressions.sh`), which is T7.1's deliberate, documented contract: `anchorAtOutline` must stamp *exactly* the caller's target (no bisection, no rounding) — the test constructs a fractional `target_w = w0 + 0.1*(MAX_w - w0)` and asserts `fit.w == target_w` to the microsecond, plus asserts R/T edge blocks sit flush at that exact fractional border.

I reverted `commitOutlineAt` to be un-ceiled again and moved the ceiling into `shrinkWrapOutline`'s own body (right before it calls `commitOutlineAt`), which does not touch `anchorAtOutline`'s call path at all. This satisfies rule 5 for `shrinkWrapOutline` (used by the main decode path, the outline sweep's `margin==0.0` rung, and every unconditional `shrinkWrapOutline(...)` call site) while leaving `anchorAtOutline`'s own result exactly what its caller passes in, preserving the existing test.

**Residual gap** (not fixed, out of file scope): `anchorAtOutline` is also called from `search.cpp`'s `outlineSweep` (`margin > 0.0` rungs) and `eval.cpp`'s `exactScore` corrective-grow, both with a caller-computed target that can be fractional. `eval.cpp`'s use never leaks into a real `Placement` (it only affects an internal scoring detour, discarded after `exactScore` returns a `Score`). `search.cpp`'s `outlineSweep` use **can** leak: if a `margin > 0.0` rung wins the `outlineSweepBetter` comparison, its (possibly fractional) `variant` becomes `result.best_placement`. This did not manifest in my regression (5 cases × 200 iters, all integer) or any of the 11 suites, but it's a live gap outside `placement_engine.cpp`'s scope — closing it fully requires either threading a ceiling into `search.cpp`/`eval.cpp`'s target computation before calling `anchorAtOutline`, or accepting that `anchorAtOutline` remains a "caller decides" primitive.

## A second, related gap found (not fixed, out of scope): `respace.cpp`

`respace.cpp:222` (`relaxAxis`) adds a fractional per-channel demand (`ch.vertical_usage / 25.0`, `ch.horizontal_usage / 25.0` — see `channelDemands`, `respace.cpp:343-344`) directly to core-block coordinates: `best = std::max(best, new_coord[a] + size[a] + scale * demand);`. This is invoked unconditionally once per elite candidate via `maybeAdoptRespace` (search.cpp:449) and inside `finalizeFeedthrough`. Whenever `respaceFixedPoint` actually widens a gap (`channel_overflow > kEps` at that point) and the widened candidate wins, core block coordinates become fractional again, regardless of rules 1-5.

Empirically this never fired for the 5 official cases at the tested iteration counts — `respaceFixedPoint`'s early exit (`channel_overflow <= kEps && open_routes == 0`) triggered before `relaxToDemand` ever ran, because SA + the outline sweep already found overflow-free (or near-zero) placements. But it is a live, unaddressed risk for harder/more-congested inputs. `respace.cpp` is outside my file scope (brief lists `src/placement_engine.cpp` only); flagging for a follow-up task rather than fixing.

## The 11 required suites

All green, confirmed twice (once right after the fix, once again after the benchmark investigation touched the build):
```
>>> PASSED: test/run_case00.sh
>>> PASSED: test/run_placement_engine_regressions.sh
>>> PASSED: test/run_20260618_regressions.sh
>>> PASSED: test/run_20260608_regressions.sh
>>> PASSED: test/run_p0_regressions.sh
>>> PASSED: test/run_p1_regressions.sh
>>> PASSED: test/run_p2_regressions.sh
>>> PASSED: test/run_p3_regressions.sh
>>> PASSED: test/run_testdata_regressions.sh
>>> PASSED: test/run_cfg_score_regressions.sh
>>> PASSED: test/run_selfconsistency_regressions.sh
ALL SUITES PASSED
```
Plus `test/run_integer_output_regressions.sh` (new) green.

Warning-clean build: `make clean && make` → zero warnings (`-Wall -Wextra -pedantic`).

`grep -E "\.[0-9]*[1-9]" /tmp/bench_bstar_case0.cfg | grep -v "^PATH"` (brief's Step 4 check, run against a fresh `bin/efgr` output for case0): empty — no fractional OUTLINE/BLOCK/CHANNEL values.

## Benchmark: before vs after (`scripts/benchmark.sh --iters 1000`) — THE MAIN CONCERN

Ran once at HEAD `1b2e214` (isolated `git worktree`, so it didn't race the main build) and once with my changes.

**BEFORE (HEAD):**
```
case   engine     checker_cost      rank_cost       overflow             wl  runtime
case0  bstar           7068587        7974020          0.000        9053527    0.07s
case1  bstar          16848143       14969573       6346.200       37616721    1.68s
case2  bstar          60742867       46434198        879.525      143164134    2.75s
case3  bstar          65764889       49406360      30998.725      163645991    2.55s
case4  bstar          88472322       61003648      23535.025      274791622    3.93s
TOTAL  bstar         238896807      179787799
case0  hybrid          5373410        5727719          0.000        3541361    0.16s
case1  hybrid         31091168       28206122      32417.925       57746362    1.64s
case2  hybrid         71650823       55711260       9154.775      159467238    2.38s
case3  hybrid         70649009       53984450      53472.775      166717559    3.28s
case4  hybrid        122420798       87559620      86135.750      348792645    6.69s
TOTAL  hybrid        301185208      231189170
```

**AFTER (this implementation):**
```
case   engine     checker_cost      rank_cost       overflow             wl  runtime
case0  bstar           5310955        5618133          0.000        3070700    0.10s
case1  bstar          14221883       12612686        171.000       32227562    1.57s
case2  bstar          77729016       57162862       7309.000      205754114    3.19s
case3  bstar          59495302       48099776      46165.000      114060509    4.50s
case4  bstar         116778754       80661347      73089.000      361302956    4.87s
TOTAL  bstar         273535910      204154805
case0  hybrid          5781379        6153007          0.000        3714375    0.17s
case1  hybrid         13840691       12268773        251.000       31503981    2.40s
case2  hybrid         75101362       54540830       9051.000      205739368    4.92s
case3  hybrid         44122364       32769405       3810.000      113620132    4.04s
case4  hybrid        113982467       78341984      37844.000      356636742    7.86s
TOTAL  hybrid        252828263      184073999
```

**Deltas (checker_cost, TOTAL):**
- **bstar: 238,896,807 → 273,535,910 = +14.50%** — exceeds the brief's ">2% is stop-and-investigate" threshold.
- hybrid: 301,185,208 → 252,828,263 = **-16.06%** (improvement).

Per-case bstar: case0 -24.9%, case1 -15.6%, case2 **+28.0%**, case3 -9.5%, case4 **+32.0%**.

### Investigation

1. **`open_routes`/legality**: confirmed 0 and `validation=ok` in both before and after, for every case I spot-checked (case2, case4) via direct `bin/efgr` runs. The hard requirement holds.
2. **Not seed noise**: re-ran case0 and case4 bstar at seeds 1-4 (`--iters 1000`) on the *unmodified* HEAD code — bit-identical results across all seeds. This search is deterministic in practice for these cases (the elite pass / outline sweep / compaction / respace machinery dominates the outcome, not the stochastic walk), so the before/after difference is a **reproducible, deterministic** consequence of the discretized search space, not random variance.
3. **Non-monotonic with more search budget**: case4 bstar at `--iters 5000` — HEAD gives the *identical* score to `--iters 1000` (88,472,317.99, fully converged/stable). My implementation gives **130,403,147** at 5000 iters, *worse* than its own 1000-iter result (116,783,731). More search made it worse, not better.
4. **Hypothesis**: `dimensionFor`'s integer window search discretizes each soft block's achievable `(w,h)` into a much coarser set of shapes than the continuous version. `mutateSoftAspect`'s continuous perturbation (`factor ∈ [0.85,1.15]`) now often either lands on the exact same integer `(w,h)` (a wasted mutation) or jumps discontinuously to a different pair — losing the fine-grained shape-tuning the SA previously used to squeeze down channel overflow. Since the elite pass's top-16 candidate pool is selected by *cheap* score (an overflow proxy, `eval.cpp`, untouched by this task) over the whole run, and cheap score's correlation with the real (exact, routed) overflow is plausibly noisier under coarser/discontinuous geometry, more iterations give more chances for a candidate with a deceptively good cheap score but worse real overflow to evict a genuinely good one from the pool — explaining why *more* search made case4 worse specifically after integerization.
5. **Not a rules 1-5 correctness defect**: every regression suite passes, `open_routes==0`/`validation=ok` hold everywhere tested, and rules 1/2's window search is implemented exactly as the brief specifies (verified by hand-tracing case0 BLK02: continuous 640.3007² → my code emits 640×640, matching the brief's own worked example).
6. **hybrid nets an improvement**: hybrid seeds its bstar leg from `sp`'s result rather than bstar's own from-scratch initial state, and improved overall (-16%). This points at bstar's *own* from-scratch exploration being what's sensitive here, not something fundamental to integer geometry.

**This is a search-quality interaction between the (now-necessary, contest-mandated) integer geometry and `search.cpp`'s existing cheap-score-based top-16 candidate selection — not a defect in this task's rules 1-5 implementation.** Fixing it would mean touching `search.cpp`'s candidate-selection/mutation logic, which is out of this task's file scope and is a genuine algorithmic judgment call (does cheap score need a different proxy or weighting under discretized geometry? does the aspect mutation need finer resolution or a different move set?) — squarely `deep-reasoner` territory, not something I attempted or should attempt as a mechanical executor. I did not modify anything to chase better benchmark numbers, since the brief specifies the window-search algorithm exactly and deviating from it to tune results would be unauthorized redesign.

**Recommendation**: a follow-up task to have `deep-reasoner` investigate whether `search.cpp`'s top-16 selection/`mutateSoftAspect` needs adjustment for discretized geometry (e.g., mutation resolution, or blending cheap-score selection with periodic exact-score sampling) before bstar's default-engine quality is considered acceptable post-integerization. Given `bstar` is the **default engine** (per CLAUDE.md), this is worth resolving before this change is considered fully "safe" for production use, even though it does not violate any correctness contract tested here.

## Step 5 (optional, integer printing)

**Not attempted.** Given the benchmark investigation above already surfaced a significant, unresolved concern worth the orchestrator's attention, and Step 5 was explicitly "implement ONLY if all suites are green with time to spare," I judged that spending further time on a cosmetic optional step was less valuable than fully documenting the benchmark finding. `writeCfg` still prints `%.3f` (e.g. `640.000`), which the brief already notes is safe/accepted by the alpha checker.

## Files changed

- `src/placement_engine.cpp` — rules 1, 2, 3 (dispatch only, no assert), 4, 5 (scoped to `shrinkWrapOutline`).
- `test/regression_integer_output.cpp` (new) — brief's Step 1 code with the soft-block-area check amended (see above).
- `test/run_integer_output_regressions.sh` (new) — matches `run_p1_regressions.sh`'s link line exactly (`src/ft.hpp` is header-only, no `src/ft.cpp` exists, so nothing extra to add).

## Self-review

- Build: warning-clean (`-Wall -Wextra -pedantic`), confirmed via full `make clean && make`.
- Every rule's implementation is commented in place with the QA/rule number it satisfies, matching file conventions (4-space indent, `lowerCamelCase`, `k`-prefixed constants, everything in `namespace efgr` anonymous-namespace-local helpers).
- No architectural changes; no refactor beyond what rules 1-5 required. `commitOutlineAt`'s shared-helper structure and `bisectShrinkWrap`'s bisection logic are untouched.
- Two deviations from the brief's literal text (rule 3's assert omitted; the Step-1 test's soft-block-area check replaced), both empirically justified and documented in place — see sections above.
- Two known, out-of-scope residual gaps documented (`anchorAtOutline`'s exact-target callers in search.cpp/eval.cpp; `respace.cpp`'s `relaxOneAxis`), neither of which manifested in any test I ran, both flagged for follow-up.
- The benchmark regression is the dominant concern and is reported in full above rather than glossed over.

## Concerns (summary)

1. **Primary**: bstar's checker_cost regressed +14.5% in aggregate (case2 +28%, case4 +32%) at `--iters 1000`, and gets *worse* with more search budget (non-monotonic) — a real search-quality cost of integerization interacting with existing top-16/cheap-score selection logic, not a correctness defect. hybrid improved (-16%). Recommend `deep-reasoner` follow-up before treating bstar's default-engine quality as settled.
2. Rule 3's assert was not added (case00.csv legacy fixture conflict) — zero behavior change, but the safety net rule 3 wanted is absent.
3. Two residual fractional-geometry gaps outside file scope (`anchorAtOutline`'s search.cpp/eval.cpp callers; `respace.cpp`'s `relaxOneAxis`), neither observed to fire in testing but both structurally capable of reintroducing fractional coordinates on harder inputs.

## Fix pass: respace fractional leak (post-packaging)

Closes the exact gap flagged in "A second, related gap found" above (`respace.cpp`'s `relaxOneAxis`/`relaxAxis`), which fired for real once Task 3's per-edge accounting made `respaceFixedPoint` engage under the now-default `hybrid` engine.

### Root cause

`channelDemands` (`src/respace.cpp:353-354`) converts channel usage to a demand in um via `usage / 25.0` — not generally a multiple of 1 (e.g. usage=16 -> 0.64, usage=17 -> 0.68, matching the `.640`/`.680` probe signature exactly). That fractional `need_w`/`need_h` is the ONLY consumer of the `/25.0` conversion in this file (confirmed by repo-wide grep on `need_w`/`need_h` — no cost/priority-comparison consumer exists, so there is no "must stay fractional" site here). It flows into `relaxAxis`'s two demand-folding terms:
- Term 1 (`src/respace.cpp:232`, general cross-overlap predecessor edge): `best = std::max(best, new_coord[a] + size[a] + scale * demand);`
- Term 2 (`src/respace.cpp:249`, exact-keyed demand pair, `p3-task-2`'s Critical-1 fix): `best = std::max(best, a_new + a_size + scale * entry.second);`

`new_coord[b]` from these terms is written straight through to `placement.rects[i].x`/`.y` in `relaxOneAxis`'s final loop (`src/respace.cpp:302-309`, unchanged) — no integerization anywhere on that path. Fixing only the `/25.0` source (e.g. `need_w = std::ceil(usage/25.0)`) is insufficient by itself: `relaxOneAxis`'s secant MAX-clamp fallback (`src/respace.cpp:281-299`, unchanged) re-multiplies the demand by a fractional `scale in [0,1]` when the unclamped relax would exceed MAX, which can reintroduce a fractional product even from an already-integer `need`. So the fix integerizes at the point identified in the task brief as where "the shift is accumulated through several variables" resolves into an actual coordinate adjustment — the two `best = std::max(...)` folds themselves — which correctly covers both the unscaled (`scale==1.0`) and MAX-clamped (`scale<1.0`) cases in one place, by induction on the frozen processing order (each `new_coord[a]` used by a later block is already integer once its own term was folded).

### Sites changed

- `src/respace.cpp:142-150` (new) — added file-local helper `double ceilWidening(double x) { return std::ceil(x - kEps); }` in the anonymous namespace, right before `relaxAxis`. Ceil (never round/floor) per the "widen more, never less" rule; the `-kEps` guard absorbs float noise so an already-integral contribution (e.g. `scale==1.0` on an exact `need`) isn't bumped an unnecessary extra unit.
- `src/respace.cpp:232` — Term 1: `scale * demand` -> `ceilWidening(scale * demand)`.
- `src/respace.cpp:249` — Term 2: `scale * entry.second` -> `ceilWidening(scale * entry.second)`.
- `test/regression_integer_output.cpp:49-67` (new block) — extended the regression to also run `runHybridPlacementSearch` on case1/case3 at `iters=1000` (the exact repro shape), asserting `open_routes==0` and `isInt` on every rect plus the stamped outline, alongside the existing bstar loop (case0-4, `iters=200`, unchanged).

No other `/25.0`-into-coordinates site exists in `respace.cpp` — grepped `25` across the whole file; the only other hits are two comments describing the (unrelated, read-only) capacity model (`vcap = 25*w`, `hcap = 25*h`) in `channelDemands`'s doc comment.

`src/placement_engine.cpp` (decode/anchor code) was not touched, per the task scoping.

### Red -> green

Repro script: `/tmp/frac_check.sh` (WSL), per the task's exact spec, run against `bin/EarlyFloorplanning_with_GlobalRoute` (default `hybrid` engine) on case1/case3.

RED (before fix, HEAD `6d2de22`):
```
case1 fractional-lines=38
case3 fractional-lines=27
```
(exit 1)

GREEN (after fix):
```
case1 fractional-lines=0
case3 fractional-lines=0
```
(exit 0)

### Suite results (all green, no pinned-value updates needed)

- `bash test/run_integer_output_regressions.sh` — PASS. `case0`-`case4` bstar (`iters=200`) + new `case1`/`case3` hybrid (`iters=1000`) all print `... integer decode ok` / `... hybrid integer decode ok`. Runtime ~19s total, no scope reduction needed.
- `bash test/run_p3_regressions.sh` — PASS, all 18 named tests green including `testDemandExceedingMaxClampsToOutlineAndStaysLegal` (the secant-clamp/`scale<1` path). **No pinned values needed updating** — every existing assertion already tolerated the ceil'd (never-smaller) result.
- `bash test/run_selfconsistency_regressions.sh` — PASS, all 5 cases, `open=0` throughout; `rel_score` for case1/case3 is `1.425e-11`/`6.711e-12` (both far under the `1e-6` gate; nonzero but negligible, consistent with ordinary 3-decimal `.cfg`-rounding residual, not a regression).
- `bash test/run_case00.sh` — PASS, rebuild clean, both `hybrid` runs (`iters=500`, `iters=25`) report `validation=ok`.
- `make clean && make` — warning-clean (`-Wall -Wextra -pedantic`), zero warnings.

### Concerns

None outstanding for this fix. The two residual gaps this Task-4 report previously flagged are now: `respace.cpp` — **closed** by this pass; `anchorAtOutline`'s fractional-target callers in `search.cpp`/`eval.cpp` (`outlineSweep`'s `margin>0.0` rungs, `exactScore`'s corrective-grow) — **still open**, unrelated to and out of scope for this fix (no `/25.0` involved, and not implicated by the case1/case3 probe), left for a separate follow-up if it is ever observed to fire.
