### Task 2: constraint-graph relax (pure) — design decision 4

**Files:** Modify `src/respace.hpp/.cpp`; tests in p3 file.
**Interfaces →:** `bool relaxToDemand(const Problem&, Placement&, const std::vector<StripDemand>&);` — build H/V graphs over core blocks + walls (order from current coords), min-gap edges from demands (max-merged per pair), longest-path re-coordinate (monotone increase only), reject/partial-scale when exceeding MAX (proportional slack per design failure-mode 1). Core order preserved by construction; edge blocks untouched here.
- [ ] Failing tests: 3-block row with a `need_w` demand between A,B ⇒ B (and C) shift right exactly by the deficit; demand exceeding MAX ⇒ partial widening, coordinates ≤ MAX, function still returns true with residual flagged (return value semantics per design). Order-freeze test: no pair swaps under any demand.
- [ ] Implement. GREEN + suites + commit `feat: longest-path respacing over core blocks`.

