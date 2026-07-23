### Task 2: shrinkWrapEstimate (cheap, geometry-only) (design §1.2.1, §1.2.2, §2)

**Files:** Modify `src/placement_engine.hpp/.cpp`. Test in `test/regression_p1_20260706.cpp`.
**Interfaces → later:** `struct OutlineFit { double w=0.0, h=0.0; bool legal=true; };` and `OutlineFit shrinkWrapEstimate(const Problem&, const Placement&);` (const, no mutation, O(n): per-axis lower bounds W_core/H_core + edge width/height needs + thirds feasibility by monotone probe; clamp to [1, MAX]).
- [ ] Failing tests: (a) parse `test/2026 ICCAD_case.case0.csv`, decode with bstar seed 1 ⇒ estimate.w*h < MAX area AND ≥ core bbox area; (b) synthetic problem with two corner-AND edge blocks at opposite corners sized ~MAX ⇒ estimate == MAX dims; (c) no-edge-block synthetic ⇒ estimate == core bbox (floored at 1).
- [ ] RED → implement per design §1.2.1-1.2.2 (reuse `locSegment`/`rectTouchesLocation`/`edgeRectLegal` via scoped Problem copies for the thirds probe; probe W downward: try candidate = max lower bound, verify each edge block CAN be legally placed on the scoped outline via `firstLegalEdgeSlot` on a trial copy — if any fails, bump that axis 5% + retry ≤6, else MAX).
- [ ] GREEN + suites + commit `feat: add cheap shrink-wrap outline estimate`.

