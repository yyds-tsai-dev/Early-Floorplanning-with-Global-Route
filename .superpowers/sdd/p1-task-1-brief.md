### Task 1: Placement outline fields + placementKey + p1 test harness (design §2 model.hpp, §3.2)

**Files:** Modify `src/model.hpp` (Placement), `src/search.cpp` (placementKey). Create `test/regression_p1_20260706.cpp`, `test/run_p1_regressions.sh`.
**Interfaces → later tasks:** `Placement::outline_w`, `Placement::outline_h` (double, default 0.0 = unset ⇒ callers fall back to problem outline).

- [ ] Failing test in the new p1 test file: two placements identical rects, one with `outline_w=100.0`, other unset ⇒ `placementKey` differs; unset defaults compile and equal today's key format for the same rects.
- [ ] Runner script mirrors `test/run_p0_regressions.sh` (same g++ src list, output `test/p1_regression_bin`; add to .gitignore).
- [ ] RED (field/key missing) → implement: append the two fields to `Placement`; in `placementKey` append `':' << llround(outline_w*1000) << 'x' << llround(outline_h*1000)`.
- [ ] GREEN + all suites + commit `feat: add declared outline fields to Placement and placementKey`.

