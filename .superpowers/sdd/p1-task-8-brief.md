### Task 8: finalizeFeedthrough grows outline + guard alignment (design §1.5 + final-review adjudication)

**Files:** Modify `src/search.cpp` `finalizeFeedthrough`.
Semantics (this is the reconciled rule — design §1.5 item 1 as written is NOT allowed):
- Inflated re-decode: after `engine.decode(inflated, state)`, run `shrinkWrapOutline(inflated, placement)` letting the outline settle anywhere ≤ MAX (grow for headroom) before `exactScore`.
- Accept guard: commit iff `new_routing.feedthrough_mismatch < incumbent_mismatch − kEps` **AND** `new_routing.open_routes == 0` **AND** shrink-wrap reported legal (containment/overlap by construction). Do NOT require `exact.value < best` anymore, but NEVER accept an illegal variant regardless of mismatch (final-review adjudication: legality is non-negotiable; mismatch is only a graded penalty).
- [ ] Failing test: extend the p1 regression — run hybrid on `test/2026 ICCAD_case.case1.csv` and `case3.csv` (seed 1, iters 2000): assert `feedthrough_mismatch == 0`, `open_routes == 0`, validation ok. (These are the P1 acceptance criteria; they currently fail with mismatch 14479/36988.)
- [ ] RED → implement → GREEN + all suites + commit `fix: feedthrough finalize grows declared outline for headroom`.
- [ ] Final: run the 5-case × 3-engine sweep (iters 2000 seed 1), append a P1 before/after table to `docs/research/2026-07-06-cost-reduction-strategy.md` appendix, mark §4 items 5-6 done, commit `docs: record P1 outline shrink-wrap results`.

