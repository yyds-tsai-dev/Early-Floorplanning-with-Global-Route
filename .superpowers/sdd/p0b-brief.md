# P0b — L6 value-symmetric auto-merge rule

**Basis**: `docs/research/2026-08-14-final-levers-verdicts-and-0811-intel.md` §3.3 adoption decision +
§5 item 2. Blanket default-on was REJECTED (old-5 Σ +1.2%); the adopted form is an **automatic rule**:
merge bundles undirected **iff the CONN matrix is fully value-symmetric** (every directed nonzero
entry has a mirror entry with an equal net count). Measured: fires exactly on the winners
{case1 −4.8%, case5 −3.1% + wall −22%} and excludes every loser (case0/2/3/4/6/7 — case7 +7.5% is
excluded, case6's forgone −2.5% is the rule's accepted conservatism cost). Bare invocation (the
contest harness style) passes no flags, so the rule must be automatic.

## Changes

### 1. `src/io.hpp` / `src/io.cpp` — new pure helper next to `mergeUndirectedBundles`

```cpp
bool bundlesValueSymmetric(const std::vector<Bundle>& bundles);
```

True iff for every unordered pair {s,t} (s≠t), the summed directed nets s→t equals the summed
directed nets t→s (build a `std::map<std::pair<int,int>, long long>` of per-directed-pair sums, same
idiom as U2's check; parseProblem emits one bundle per nonzero entry so sums are single values in
practice, but summing is the robust contract). One-sided pairs (mirror absent = mirror sum 0 vs
nonzero) → false. Self-pairs (s==t) are trivially symmetric — skip them. Empty list → true
(vacuous; merge is a no-op anyway). Pure, no I/O, never called by `parseProblem`.

### 2. `src/main.cpp` — wiring (solver entry ONLY; rescorer/regression mains untouched)

Replace the current two lines at `main.cpp:150-151` with precedence **forced-off > flag-on > auto**:

- `--no-undirected-bundles` (new hidden measurement flag, same not-in-usage-banner precedent):
  never merge — the A/B force-off arm on symmetric cases.
- `--undirected-bundles` (existing): always merge.
- Neither: merge iff `bundlesValueSymmetric(problem.bundles)` — the L6 auto-rule.

Set `problem.undirected_bundles` accordingly before the (unchanged) single
`mergeUndirectedBundles` call. Track which arm fired in a small local
(`off` / `flag` / `auto` / `forced-off`) and print it in the stderr summary line as
`undirected=<arm>` (place it near `legs_run=`/`phase1_legs=`). Update the model.hpp doc comment on
`Problem.undirected_bundles` (opt-in flag → flag OR auto-rule) and io.hpp's `mergeUndirectedBundles`
comment's "wired in by main.cpp's hidden flag" sentence to mention the auto-rule.

### 3. Tests — extend `test/regression_undirected_20260730.cpp` (suite: `test/run_undirected_regressions.sh`)

- **U9 (pure contract)**: hand-built lists — exactly symmetric → true; mirror missing → false;
  mirror present but value differs → false; empty → true; a self-pair entry alone → true.
- **U10 (real-case adoption criterion)**: `bundlesValueSymmetric(parseProblem(...).bundles)` must be
  **true for case1** (20260727 dir, same `newCasePath` helper) and **true for case5**, **false for
  case0, case6, case7** (case5/6/7 live in
  `test/E_testcase_20260811/testcase_upload_20260712/2026 ICCAD_case.caseN.csv`). This pins the
  rule to the measured winner set.
- **Binary-level wiring (shell, append to `run_undirected_regressions.sh` after the C++ bin)**:
  build once, then with `--iters 50 --seed 1`:
  - case1 bare → stderr contains `undirected=auto`, rc==0
  - case0 bare → `undirected=off`
  - case0 `--undirected-bundles` → `undirected=flag`
  - case1 `--no-undirected-bundles` → `undirected=forced-off`

### 4. Verification gates (before commit)

- `make` warning-clean.
- `bash test/run_undirected_regressions.sh` green (old U1–U8 + new).
- Byte-identity on NON-symmetric cases: bin/efgr `--iters 200 --seed 1` md5 vs pre-P0b HEAD binary
  (worktree build) must match on case0/2/3/4 (case1 CHANGES by design — record its new md5 in the
  report instead). Also spot-check case6/7 (0811 dir) rc==0 `validation=ok`.
- `bash test/run_case00.sh`, `bash test/run_f1_regressions.sh` green (main.cpp touched).

## Out of scope

No `defaultVariants()` change, no rescorer change, no parseProblem change, no edge-free interaction.
