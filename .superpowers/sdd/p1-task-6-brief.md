### Task 6: validation + writeCfg use declared outline (design §1.6)

**Files:** Modify `src/validate.cpp` (build scoped Problem from `placement.outline_w/h` fallback MAX for containment/thirds/flush), `src/io.cpp` (`writeCfg` emits declared W,H when set, fallback current 3-arg usedOutline path). Extend `testOutlineConsistencyEvalVsCfg` in the p0 OR p1 test file to a shrunk case: exactScore.outline_area == emitted OUTLINE w×h == stamped W×H.
- [ ] Failing test first (false edge-location violation on a shrunk case0 solution via real `validateSolution`; outline consistency on shrunk case).
- [ ] RED → implement → GREEN + suites + commit `fix: validate and emit against the declared outline`.

