# P3 Task 7 report: final sweep + docs (plus rider fixes)

**Status:** GREEN. Rider fixes applied and committed; full 15-row sweep run; strategy doc and plan
updated with honest findings. All suites pass, `make` warning-clean.

**Commits:**
- `935a15a` `fix: memoize finalize respace scorer and cross-ref respace contracts`
- `5c56845` `docs: record P3 demand-respacing results`

## Rider fixes
1. `src/search.cpp` (`finalizeFeedthrough`, ~line 273-289): `inflatedExact` now caches by
   `placementKey` in a local `std::unordered_map<std::string, std::pair<Score, RoutingResult>>`,
   same pattern as `runPlacementSearch`'s `exactCached` — avoids a duplicate real `exactScore`
   call per `respaceFixedPoint` invocation (up to 2x per pass x 3 passes).
2. `src/respace.cpp` (`respaceToDemand`, ~line 389-392): added the requested one-line comment
   cross-referencing `commitOutlineAt`'s `edgeRectLegal` contract; verified against
   `placement_engine.cpp` (`commitOutlineAt` calls the containment/overlap/edge-location check
   internally, confirmed by reading `placement_engine.cpp:766,781,934`).

## Sweep highlights (P3 vs P1 appendix, iters 2000 seed 1)
- **case1/sp**: score 1.34604e+11 → 5.93053e+10 (−55.9%), overflow 134405 → 58181 (−56.7%) — largest win.
- **case1/hybrid**: score 6.08190e+10 → 3.13355e+10 (−48.5%), overflow 59986.7 → 30516.5 (−49.1%).
- **case1/bstar**: score/overflow both −11.8%/−11.9%.
- **case2/bstar**: score 1.12121e+11 → 7.90397e+10 (−29.5%), overflow 109632 → 77493.3.
- **case2/hybrid**: score 1.05866e+11 → 7.88814e+10 (−25.5%), overflow 104514 → 77719.4.
- 10/15 rows (case0 all, case2/sp, case3 all, case4 all) byte-identical to the P1 appendix — axis-locked, no free axis for the H/V relax to widen into.

**Honest finding:** P3's machinery (T1-T6) is fully in place, warning-clean, Pareto-safe, and its
synthetic proof case still holds (125→0 overflow). On the real cases at seed 1 the predicted
overflow collapse landed only where axis slack existed (case1 all engines, case2 bstar/hybrid);
the remaining rows are axis-locked (core x-extent pinned at MAX per the T4 report) so spacing alone
cannot relieve them. P2 (cost-driven routing, steering flow off locked axes / into FT) is the
strategic next phase for those rows — not overclaimed as done in the docs.

Report path: `.superpowers/sdd/p3-task-7-report.md`.
