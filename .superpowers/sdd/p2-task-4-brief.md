### Task 4: negotiation rounds (design §Q2)

**Files:** `src/routing.cpp` routeProblem (new-router path).
- [ ] RED: two bundles contending one narrow channel with an alternate slack path ⇒ after ≤3 negotiation rounds total overflow strictly below round-1 (history cost diverts the later/lighter bundle); deterministic across runs.
- [ ] Implement per-direction call-local history cost, rounds ≤3, best-round-wins by (open==0, then total cost). GREEN + suites + commit `feat: negotiated rip-up reroute`.

