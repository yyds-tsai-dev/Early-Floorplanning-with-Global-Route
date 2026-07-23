### Task 7: elite-pass outline sweep (design §1.4 elite path)

**Files:** Modify `src/search.cpp` (elite top-16 + compacted loop: for each candidate run `outlineSweep`), `src/eval.cpp` or `search.cpp` static helper `outlineSweep(problem, placement)` — deterministic 5–7 additive-margin trials between the tight shrink-wrap and MAX (e.g. margins {0, 0.25, 0.5, 1, 2, 4} × (overflow/25) or geometric fractions of (MAX−tight)); exact-route each via the cached `exactCached`; pick min value with `open_routes==0` strictly preferred.
- [ ] Failing test: regression harness runs case0 sp/bstar seed 1 with sweep ⇒ final best score ≤ pre-sweep best (record numbers), validation ok. Plus determinism: two identical runs byte-identical `.cfg`.
- [ ] RED (helper missing) → implement → GREEN + suites + commit `feat: elite outline sweep finds min-cost declared outline`.

