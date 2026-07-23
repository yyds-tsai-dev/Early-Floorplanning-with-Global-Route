### Task 0: area-denominate channel overflow (design §8.1 R0 — prerequisite, scoring-model change)

**Files:** `src/routing.cpp` (overflow accounting ~222-225), possibly `src/model.hpp` (comment/constant), tests: new p2 pair + updates to any test asserting overflow magnitudes (grep all test/*.cpp for channel_overflow).
- [ ] RED: unit test — channel 80µm wide, span 610µm, 3600 nets vertical usage ⇒ `channel_overflow ≈ (3600−2000)/25 × 610 = 39,040 µm²` (current code yields 1600). Second fixture for horizontal direction (span = width). Official-example comparability assert: FT mismatch for the same flow (base 372,949, tier 40%) within ~1.1× of that overflow area.
- [ ] Implement per Global Constraint 2. Audit + update dependents: respace `channelDemands` uses usage/25 directly from routing usage fields (UNAFFECTED — verify by test), P3 case1 tests assert overflow>0 (still true, magnitude changes — relax exact-magnitude asserts only), paydown/sweep preference rules are relative (unaffected).
- [ ] GREEN + all suites + note in commit body that all recorded sweep numbers re-baseline at T7. Commit `fix: denominate channel overflow in area units`.

