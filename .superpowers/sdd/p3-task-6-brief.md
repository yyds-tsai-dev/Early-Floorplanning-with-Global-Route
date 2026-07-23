### Task 6: finalizeFeedthrough hook — design T6

**Files:** Modify `src/search.cpp` (inside finalize: respaceFixedPoint on the inflated re-decode before tryCommit; acceptance rule unchanged — paydown + open==0 + validateSolution).
- [ ] Failing test: reuse the T5-era FT-forced fixture (see test/regression_p1_20260706.cpp finalize tests) with a congested variant ⇒ finalize commits with mismatch 0 AND overflow reduced vs no-respace.
- [ ] Implement. GREEN + suites + commit `feat: respace inside feedthrough finalize`.

