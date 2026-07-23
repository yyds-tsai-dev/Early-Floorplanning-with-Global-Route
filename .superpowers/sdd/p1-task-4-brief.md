### Task 4: stamp in decode + cheapScore area gradient (design §1.3)

**Files:** Modify `src/placement_engine.cpp` (end of BOTH engine `decode`s: call `shrinkWrapOutline`), `src/eval.cpp` (`cheapScore` outline_area from stamped fields w/ MAX fallback; add static `channelMarginProxy(problem, placement, W, H)` = Σ per bundle `(nets/25) × (crossing span proxy)` clamped to `MAX_area − W·H`, added to outline_area; containment penalty stays vs MAX — do NOT touch the `score.value` composition line).
- [ ] Failing test: two hand-built placements (same blocks, one compact core / one spread) ⇒ compact has strictly smaller `cheapScore().outline_area`; both < MAX area when loose.
- [ ] RED → implement → GREEN + suites (existing regressions may shift SA trajectories — legality asserts must stay green; if any asserts an exact score value, update + note in commit body) + commit `feat: cheap shrink-wrap area gradient in cheapScore`.

