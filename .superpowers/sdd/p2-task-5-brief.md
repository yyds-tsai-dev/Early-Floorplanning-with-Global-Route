### Task 5: feasibility fallback — never open (design §Q5/T5)

**Files:** `src/routing.cpp`.
- [ ] RED: fixture where cost-driven pass leaves a chunk open (constructible: all finite-cost edges port-blocked... if not constructible, characterization: assert fallback path exists by code inspection test calling it directly) ⇒ legacy two-pass reroutes it; open_routes==0 whenever legacy would achieve it.
- [ ] Implement. GREEN + suites + commit `feat: feasibility fallback for cost-driven routing`.

