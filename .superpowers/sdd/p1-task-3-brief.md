### Task 3: shrinkWrapOutline (exact, re-anchor + grow loop) (design §1.2.3, §1.2.4, §3.1)

**Files:** Modify `src/placement_engine.hpp/.cpp`. Test in p1 test file.
**Interfaces → later:** `OutlineFit shrinkWrapOutline(const Problem&, Placement&, int max_grow_steps = 6);` — mutates edge rects + stamps `placement.outline_w/h`; core rects untouched; implements design §1.2.4 pseudocode verbatim (scoped Problem, `legalizeEdgeBlocks`, verify all edge blocks `edgeRectLegal` + no overlap + containment; on failure grow deficient axis `*1.05 + kEps` toward MAX; fallback = MAX with re-legalized edges = today's layout).
- [ ] Failing tests: for each of case0..case4 (parse, decode bstar seed 1, then shrinkWrapOutline): (a) `validateSolution` against a scoped Problem (outline = stamped W,H) reports ok for containment/edge-location/overlap (note: current validateSolution reads problem outline — build the scoped copy in the test, T6 fixes the real validator); (b) stamped W ≤ MAX_w, H ≤ MAX_h; (c) no core rect moved (compare before/after); (d) seeds 2,3 fuzz on case0 — same invariants; (e) a mid-side (TM-style) edge block longer than the shrunk third still legal (targeted A19/A20 regression, design Q3).
- [ ] RED → implement → GREEN + suites + commit `feat: add exact shrink-wrap with edge re-anchoring`.

