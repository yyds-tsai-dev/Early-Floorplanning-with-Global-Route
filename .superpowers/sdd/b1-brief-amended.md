# B1 (AMENDED): legacy paydown walk demoted to candidate/seed generator, selection value-only

Supersedes `.superpowers/sdd/b1-brief.md` for the parts below (the comparator changes it already specifies
are kept as-is). Issued by the controller after the original brief's byte-identity premise was empirically
falsified for cases 2/3/4 (see `b1-report.md` / the executor's stop-and-report). Saved verbatim as received.

---

The coordinator sent a message while you were working:
Your stop was correct and the diagnosis is accepted — the brief's byte-identity premise is falsified for cases 2/3/4. Here is the amended design (controller-adjudicated). First: save this message verbatim as `.superpowers/sdd/b1-brief-amended.md` (precedent: beta-task-3-brief-amended.md), then execute.

ANSWERS TO YOUR QUESTIONS
Q1: Keep post-finalize reseeding AND add the value-best reseed — both, see spec below. Q2: case3 +24% is NOT acceptable; the amended design guarantees per-case ≤ old. Q3: rewrite the 3 unlisted tests to the new semantics as specified below (they encoded the old objective; the new truth table is the correct one).

AMENDED SPEC — "legacy paydown walk demoted to candidate/seed generator, selection is value-only"

1. Keep your comparator changes in src/search.cpp/.hpp exactly as implemented.

2. `finalizeFeedthrough`: while walking the SAME candidate stream (generation must stay byte-identical — you proved it is decision-independent; the addition below must be read-only observation, re-verify this still holds), additionally simulate the OLD acceptance rule to compute the legacy endpoint:
   - Maintain `legacy_best` (placement+routing+score), initialized from the pre-finalize `result.best_*`.
   - For every candidate the walk generates (including the compactTowardOrigin variants and repair-loop candidates), apply the OLD gate against `legacy_best`: open_routes==0 AND (mismatch strictly decreases by >kEps, OR equal-mismatch-within-kEps AND lower value) AND validateSolution ok → update `legacy_best`. This must reproduce the pre-B1 committed endpoint exactly (the old rule compared against the evolving old best — replicate that evolution).
   - Store into new `SearchResult` fields: `legacy_paydown_placement`, `legacy_paydown_routing`, `legacy_paydown_score` (and set them = best_* when finalize early-returns at mismatch<=kEps so they are always populated).
   - `result.best_*` commits stay value-gated (your new rule), unchanged.

3. `runHybridPlacementSearch` becomes three legs:
   - Leg 1: sp as today (iters/2, seed).
   - Leg 2 (bstar_a): `std::mt19937 rng_a(bstar_options.seed); seed_a = bstar->seedFromPlacement(problem, sp_result.best_placement, rng_a);` then runPlacementSearch — i.e. today's code path but now seeded from the VALUE-best sp placement.
   - Leg 3 (bstar_b): `std::mt19937 rng_b(bstar_options.seed); seed_b = bstar->seedFromPlacement(problem, sp_result.legacy_paydown_placement, rng_b);` then runPlacementSearch with the SAME bstar_options. This must reproduce the OLD bstar trajectory exactly (fresh rng seeded identically, same options, seed placement = the old post-finalize sp placement). SKIP leg 3 when placementKey(legacy_paydown_placement) == placementKey(sp_result.best_placement).
   - Final pick: fold {sp_result, bstar_a, bstar_b} with the NEW `paydownBetter` (open_routes-lex then value). iterations/exact_evaluations = sums over executed legs. Time budget split: sp gets 0.5×, each bstar leg 0.25× (document; budget-off unaffected).
   - Update the leg-pick comment accordingly.

4. THE GUARANTEE (verify, don't assume): with leg 3 reproducing the old bstar trajectory and the old sp endpoint having value >= your new sp result (new rule accepts every value-improving candidate from the same stream), the new hybrid final must satisfy per-case checker cost <= the old defaults you measured: case0 5,781,379 / case1 14,031,473.20 / case2 74,930,410.79 / case3 44,704,168.45 / case4 91,952,579.01 — CORRECTION: case4 old default was 113,982,466.84. Run all 5 default-flag cases and check <= against the OLD values. Any violation = STOP and report (most likely an rng-sequence mismatch in leg 3; diff your leg-3 trajectory against a pre-B1 build's bstar leg if needed — you may build a throwaway pre-B1 binary from git stash/HEAD for diagnosis only).

5. Tests:
   a. New `test/regression_b1_20260723.cpp` + `test/run_b1_regressions.sh` (copy g++ line style from run_p1_regressions.sh): comparator truth table — value dominates mismatch (both functions); open_routes dominates value (paydownBetter); exact-value tie -> lower mismatch wins; worse value + lower mismatch -> REJECTED (the pinned regression); plus a hybrid-consistency test on case1 at low iters (e.g. --iters 200): hybrid final value <= each standalone leg value it was folded from (expose what you need or assert via the 5-case <= check instead if leg introspection is impractical — say which you did).
   b. `test/regression_p0_20260706.cpp` testAcceptFeedthroughPaydownRule + testPaydownBetterLegSelection: rewrite assertions to the NEW truth table, keep names, add a comment citing docs/research/2026-07-23-research-refresh-and-strategy.md §2 and this amendment.
   c. `test/regression_p3_20260707.cpp` testFinalizeFeedthroughRespaceCongestedVariant: repurpose — the fixture now asserts the LEGACY SIM reaches mismatch 0 on that synthetic (preserving the fixture's original proof, now about legacy_paydown_*) AND the value-gated best never has worse value than pre-finalize.
   d. `test/regression_p1_20260706.cpp` the two FinalizeFeedthroughAcceptance tests: rewrite to assert validation ok + open_routes==0 + final value <= the OLD-rule recorded value for that exact invocation. If you don't have the old value at those params, measure it once from a pre-B1 throwaway build and pin it as a named constant with a comment.
   e. New e2e regression (in the b1 file): the five default-flag <= constants from item 4 as upper bounds.
6. Full battery: make warning-clean; all suites green (12 + new b1 suite).
7. Sweep matrix per the ORIGINAL brief (budget {0,15,45} x {hybrid,bstar}, seed 1, golden-rescored; compare against beta-task-5-report.md Part B). Acceptance: no budget cell > its budget-0 baseline by more than 1%.
8. Deliverables: .superpowers/sdd/b1-report.md (include: the falsified-premise story, per-case before/after table, the <= guarantee check, sweep matrix, adjudicated tests list), progress.md ledger line, ONE commit of only your files, message: `fix(search): value-gate paydown orderings; legacy paydown walk demoted to seed generator (B1)` + the standard Co-Authored-By: Claude Fable 5 <noreply@anthropic.com> footer.
Same environment rules and STOP conditions as before.

Address this before completing your current task.
