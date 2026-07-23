### Task 5: exactScore routes inside declared outline + 1-step corrective grow (design §1.4)

**Files:** Modify `src/eval.cpp` (`exactScore`: scope outline from stamped fields — recompute via `shrinkWrapOutline` on a local copy if unset; after routing, if `open_routes > 0`, do ONE corrective grow — re-run `shrinkWrapOutline` with the failed axis bumped by `channel_overflow/25` margin (min +5%) — and re-route once; keep the better of the two results by value with `open_routes==0` preferred absolutely).
- [ ] Failing test: case0 decode+shrink ⇒ `exactScore` outline_area < MAX area, `open_routes == 0` (post corrective grow), and returned score.outline_area == stamped W×H used for routing.
- [ ] RED → implement → GREEN + suites + commit `feat: exact scoring routes inside the declared outline`.

