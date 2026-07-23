### Task 4.5 (amendment 2026-07-06): bisection shrink — replace linear grow (evidence: T4 smoke shrank case0 only 0.16%)

**Why:** T3's grow loop (×1.05^≤6 from the per-axis lower bounds, both axes coupled) exhausts before reaching the true feasible frontier on case0 (feasible W ≈ core_right + inward-moving corner-block width, ≫ lower_bound × 1.34) ⇒ falls back to MAX on most decodes; SA only sees shrink after lucky mutations. Feasibility is monotone in the outline ⇒ bisection finds the near-minimal legal outline deterministically.

**Files:** Modify `src/placement_engine.cpp` only (`shrinkWrapOutline` + `shrinkWrapEstimate` search strategy; feasibility predicates `placementLegalAt`/`edgeBlocksFeasibleAt` unchanged). Test in p1 test file.
**Algorithm:** t-interpolation bisection: candidate(t) = lerp((W_lb,H_lb) → (MAX_w,MAX_h), t ∈ [0,1]); feasible(t) monotone ⇒ binary search smallest feasible t (≤12 iterations or until relative gap < 0.5%); then two per-axis refinement bisections (shrink W alone holding H, then H holding W, same tolerance). Keep MAX fallback; stamp result; determinism (pure arithmetic midpoints).
- [ ] Failing test: case0 bstar seed 1 RAW initial decode ⇒ stamped area ≤ 0.9 × MAX area (currently ≈ MAX = RED); T3's whole invariant suite stays green; byte-determinism across two runs.
- [ ] Implement → GREEN + all suites + commit `perf: bisection shrink-wrap finds near-minimal legal outline`.

