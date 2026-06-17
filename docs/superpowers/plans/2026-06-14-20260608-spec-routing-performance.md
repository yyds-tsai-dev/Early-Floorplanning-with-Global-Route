# 20260608 Spec Routing Performance Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Update the solver for the 2026-06-08 Problem E statement and Q&A, then improve routing-aware scoring and runtime without replacing the whole architecture.

**Architecture:** Keep the existing C++17 module split. Add explicit model fields for port constraints and route accounting, extend parser and validation first, then make routing capacity directional and feedthrough-aware. Performance work stays conservative: avoid GPU and external libraries, cache repeated exact routing work, and split large bundles only when congestion benefits justify it.

**Tech Stack:** C++17 standard library, `make`, shell regression tests.

---

### Task 1: PDF/QA Gap Audit

**Files:**
- Read: `docs/official/E_20260608.pdf`
- Read: `docs/official/E_QA_20260608.pdf`

- [ ] Re-check the latest statement and Q&A for all implementation-affecting rules.
- [ ] Record uncovered gaps in the final response and tests.

### Task 2: Regression Tests First

**Files:**
- Create: `test/E_testcase_20260608_port.csv`
- Create: `test/run_20260608_regressions.sh`

- [ ] Add a minimal testcase using the new `PORT` column before FT conversion rates.
- [ ] Add assertions that parser output remains valid and no bogus FT rate is caused by the `PORT` value.
- [ ] Add assertions for edge LOCATION `TL,LT` as an AND/corner condition.
- [ ] Run the script and verify it fails before production changes.

### Task 3: Spec Compatibility Layer

**Files:**
- Modify: `src/model.hpp`
- Modify: `src/io.cpp`
- Modify: `src/floorplan.cpp`
- Modify: `src/validate.cpp`

- [ ] Add `port_edge` and preserve FT rates for both old and new CSV formats.
- [ ] Treat multi-token edge LOCATION as AND constraints.
- [ ] Validate Edge/Macro fixed orientation and edge block flush/region constraints.
- [ ] Validate port edge constraints for routing endpoints.

### Task 4: Routing Evaluator

**Files:**
- Modify: `src/model.hpp`
- Modify: `src/routing.cpp`
- Modify: `src/eval.cpp`
- Modify: `src/validate.cpp`

- [ ] Track channel horizontal and vertical usage separately.
- [ ] Compute directional overflow from path step orientation instead of `min(width,height)`.
- [ ] Count soft-block feedthrough traversal and compare output dimensions against required inflated area.
- [ ] Allow future route splitting by storing route accounting per bundle.

### Task 5: Low-Risk Performance

**Files:**
- Modify: `src/main.cpp`
- Modify: `src/eval.cpp`
- Modify: `src/routing.cpp`

- [ ] Avoid exact re-routing when placement geometry is unchanged.
- [ ] Add congestion-aware edge weights so exact routing avoids already-overfull channels.
- [ ] Keep runtime CPU-only and deterministic under `--seed`.

### Task 6: Verification

**Files:**
- Run: `make clean && make`
- Run: `bash test/run_case00.sh`
- Run: `bash test/run_20260608_regressions.sh`

- [ ] Confirm all builds/tests pass.
- [ ] Report any remaining spec risks that require official evaluator feedback.

## Self-Review

- Spec coverage: Covers parser, port constraints, edge LOCATION semantics, directional channel density, feedthrough overflow, and CPU-only performance.
- Placeholder scan: No task depends on unspecified tools or external libraries.
- Type consistency: New model fields are consumed by parser, floorplan, routing, evaluation, and validation.

## Follow-Up Direction

As of the 2026-06-14 follow-up discussion, this conservative routing-performance plan is no longer the main optimization strategy. The long-term direction is documented in `docs/superpowers/specs/2026-06-14-long-term-placement-engine-design.md`: implement a shared placement engine interface, add both Sequence-Pair and real B*-Tree backends, and compare `sp`, `bstar`, and `hybrid` modes under the same evaluator. Future performance work should prioritize that placement/search architecture.
