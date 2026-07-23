# B6 Report — Rank-fidelity instrumentation (Kendall tau harness) + flag-gated adaptive interval

Source: `.superpowers/sdd/b6-brief.md`, research doc `docs/research/2026-07-23-research-refresh-and-strategy.md` §2 lit (saACM-ES GECCO 2012, Lau & Ho JOTA 1997, Gelfand & Mitter JOTA 1989) and §7 item B6. HEAD at start: `da4676b` (B4.1 landed, route-pricing portfolio).

## What changed

- **`src/rank_stats.hpp`** (new, header-only, no deps beyond STL): pure rank-correlation helpers.
  - `kendallTauB(pairs)` — Kendall's tau-b over `(x_i, y_i)` samples, O(n^2) all-pairs, standard
    tie-aware denominator (a pair tied on x, y, or both is excluded from concordant/discordant and
    contributes to the n1/n2 tie-pair counts per the usual tau-b definition). Returns 0.0 for n<2
    or a degenerate (zero-variance) input rather than propagating NaN/inf.
  - `windowedTauB(pairs, window = kDefaultFidelityWindow=64)` — tau-b over the trailing `window`
    entries (or the whole sequence if shorter).
  - `spearmanRho(pairs)` — Pearson correlation of the two variables' average-tie ranks.
  - `EliteDiagnostics eliteDiagnostics(elite_pairs)` — `exact_best_cheap_rank` (1-based position in
    the cheap-rank-ordered pool of the exact-best member), `top1_agreement` (bool), `pool_size`.
- **`src/search.hpp`**: `SearchOptions` gained `bool adaptive_interval = false;` (the
  `--adaptive-interval` flag, default off). `SearchResult` gained
  `std::vector<std::pair<double,double>> fidelity_pairs` (SA-loop cheap/exact pairs) and
  `elite_pairs` (elite-pool cheap-rank-ordered snapshot) — both observation-only, documented as
  never read by any decision.
- **`src/search.cpp`** (`runPlacementSearch`):
  - At the existing periodic/cheap-beats-best exact-eval trigger site, records
    `(cand_score.value, exact.value)` into `result.fidelity_pairs` for every `exactCached` call
    there (cache hit or miss), unconditionally.
  - New preliminary loop at the start of the elite pass (before its sweep/compaction/respace
    adoptions run) snapshotting each of the `top_candidates` pool members' `(cheap, exact)` value
    via the same `exactCached` (memoized — a member already exact-scored during the SA loop is a
    cache hit, not a new eval) into `result.elite_pairs`, in the pool's existing cheap-rank order.
  - Flag-gated adaptive interval: when `options.adaptive_interval` is on, every
    `kAdaptiveWindow` (=64) recorded fidelity pairs recomputes `windowedTauB`; `tau_w >= 0.7`
    multiplies the live `exact_interval` by 1.5 (capped at 4x the CLI/options value); `tau_w < 0.4`
    halves it (floored at 1). Off (default), this block never executes and `exact_interval` is
    never reassigned after its initial `std::max(1, options.exact_interval)`.
  - Documented (at both fold sites) that `runHybridPlacementSearch`/`runPricingPortfolio`'s
    whole-struct-copy leg pick keeps only the picked leg's `fidelity_pairs`/`elite_pairs` — other
    legs' recorded pairs are discarded, never merged.
- **`src/main.cpp`**: `--adaptive-interval` flag parsed into `options.adaptive_interval` (default
  off); usage string updated. Stderr summary gained, immediately after `pricing=`:
  `tau=<overall> tau_w=<trailing-64-window> pool_tau=<elite pool tau> exact_best_cheap_rank=<r>/<K>`
  — all computed from `result`'s (picked-leg) `fidelity_pairs`/`elite_pairs` via the new header.
- **`test/regression_b6_20260724.cpp`** (new) + **`test/run_b6_regressions.sh`** (new, 16th suite):
  rank_stats.hpp unit tests (hand-computed sequences below) plus an end-to-end determinism smoke
  (two `runPricingPortfolio` invocations on the same case/seed with `adaptive_interval=false`
  produce byte-identical `.cfg` output).
- **`.gitignore`**: added `test/b6_regression_bin`.

## rank_stats.hpp hand-derivations (pinned in the regression file)

| Test | x | y | Hand-derivation | tau_b |
|---|---|---|---|---|
| Perfect agreement | (1,2,3,4,5) | (1,2,3,4,5) | all 10 pairs concordant | 1.0 |
| Perfect inversion | (1,2,3,4,5) | (5,4,3,2,1) | all 10 pairs discordant | -1.0 |
| Hand-computed 5-element | (1,2,3,4,5) | (2,1,4,3,5) | 8 concordant, 2 discordant ((1,2),(3,4)), no ties | 0.6 |
| Ties handling | (1,2,2,3) | (1,2,3,3) | 4 concordant, 0 discordant, tie_x=1 ((2,3) idx), tie_y=1 ((3,4) idx) -> (4-0)/sqrt(5*5) | 0.8 |
| All tied in x | (1,1,1) | (1,2,3) | n0-n1 = 0 -> degenerate | 0.0 (guarded) |
| n<2 | {} / 1 point | — | guarded | 0.0 |

`windowedTauB` fixture: 5 internally-discordant pairs followed by 5 internally-concordant pairs
(all cross pairs concordant too) gives whole-run `tau = 25/45 = 5/9` but `windowedTauB(pairs, 5)`
(the trailing block only) `= 1.0` — the windowed/overall distinction the `tau`/`tau_w` stderr
fields report separately.

## Suites

All 16 suites green (15 pre-existing + `run_b6_regressions.sh`), warning-clean build
(`-Wall -Wextra -pedantic`):

```
PASS test/run_case00.sh (10s)
PASS test/run_placement_engine_regressions.sh (22s)
PASS test/run_20260618_regressions.sh (19s)
PASS test/run_20260608_regressions.sh (18s)
PASS test/run_cfg_score_regressions.sh (9s)
PASS test/run_selfconsistency_regressions.sh (72s)
PASS test/run_integer_output_regressions.sh (30s)
PASS test/run_testdata_regressions.sh (7s)
PASS test/run_p0_regressions.sh (10s)
PASS test/run_p1_regressions.sh (28s)
PASS test/run_p2_regressions.sh (12s)
PASS test/run_p3_regressions.sh (13s)
PASS test/run_b1_regressions.sh (47s)
PASS test/run_b3_regressions.sh (26s)
PASS test/run_b4_regressions.sh (34s)
PASS test/run_b6_regressions.sh (10s)

=== SUMMARY: 16 passed, 0 failed (of 16) ===
```

`run_b6_regressions.sh` itself: 10 tests (6 `kendallTauB` cases incl. the brief's named perfect
agreement/inversion/5-element/ties set, `windowedTauB`'s trailing-window behavior, `spearmanRho`
sanity, `eliteDiagnostics` rank/agreement/empty-pool, and the determinism smoke) all pass — see
"rank_stats.hpp hand-derivations" above for the exact math each pins.

## Byte-identity verification (hard acceptance gate)

Procedure (git-stash/rebuild pattern, matching B4.1's own byte-identity verification): with the
working tree at post-B6 (HEAD `da4676b` + this task's diff, `--adaptive-interval` off — the
default), built and ran all 5 default-flag cases, saving `.cfg` output + md5sums. Then
`git stash push -u` (stashes all tracked+untracked B6 changes back to bit-for-bit HEAD `da4676b`),
rebuilt, ran the identical 5 default-flag invocations again, saved md5sums. Then `git stash pop` to
restore, confirmed `git status` matches the exact B6 diff, and rebuilt post-B6 one more time to
leave the tree in the finished/built state.

```
case0: 8814c8771d96eb0d63ffe8ca797c2c43  (pre-B6 == post-B6)
case1: bb738dc520417e0a60412dab0a2687b5  (pre-B6 == post-B6)
case2: 8d82ef67153147ec3a8a39426dcea9c3  (pre-B6 == post-B6)
case3: 3388a0a277109fd7fb28469e22e4b8b7  (pre-B6 == post-B6)
case4: 30c60ab3097c625bbf06019d84727c62  (pre-B6 == post-B6)

=== RESULT: ALL 5 CASES BYTE-IDENTICAL (pre-B6 vs post-B6, default flags, adaptive-interval off) ===
```

**All 5 cases BYTE-IDENTICAL — no STOP triggered.** This confirms the theoretical argument the
instrumentation's design rests on: `fidelity_pairs`/`elite_pairs` recording never touches
`result.best_*`/`exact_interval`/`rng`, the elite-pool snapshot's extra `exactCached` calls are
transparent because `exactScore`/`routeProblem` are pure deterministic functions of
`(Problem, Placement)` with no static/global mutable state (independently verified by grep — no
`static` state in `eval.cpp`, none in `routing.cpp` beyond comments explicitly noting its absence),
and the adaptive-interval mutation of `exact_interval` is fully gated behind
`options.adaptive_interval` (false by default, so the block never executes and `exact_interval` is
never reassigned after its initial `std::max(1, options.exact_interval)`).

## Measurement deliverable (design input for B5)

5 public cases, default flags and `--iters 4000`, `--adaptive-interval` off both times (measuring
the baseline, not the experiment hook). `pricing`/all four fidelity fields are the PORTFOLIO's
picked leg (`runPricingPortfolio` folds official-vs-legacy pricing by value; hybrid itself folds
sp/bstar_a/bstar_b by value inside that) — per the brief's "for hybrid, report the picked leg's
numbers" instruction. `iters` is `runPricingPortfolio`'s existing summed-iterations telemetry
(sum of both pricing legs' `SearchResult.iterations`, each itself already a sum of that leg's
executed hybrid sub-legs — pre-existing accounting from B4.1/B1, unchanged by B6); at `--iters
4000` this sums to ~12000 because the portfolio runs the full hybrid dispatch (up to 3 sub-legs)
twice (official+legacy pricing).

### Default flags (iters=1000 nominal)

| case | pricing | wall time | iters | exact_evals | tau | tau_w | pool_tau | exact_best_cheap_rank/K |
|---|---|---:|---:|---:|---:|---:|---:|---|
| 0 | official | 0.441s | 2500 | 4263 | 0.067 | 0.067 | 0.828 | 1/16 |
| 1 | official | 6.373s | 3000 | 4288 | 0.689 | 0.689 | -0.433 | 9/16 |
| 2 | official | 11.257s | 3000 | 3693 | 0.689 | 0.689 | 0.300 | 7/16 |
| 3 | **legacy** | 17.649s | 3000 | 5730 | 0.600 | 0.600 | -0.067 | **16/16** |
| 4 | official | 27.078s | 3000 | 5775 | 0.200 | 0.200 | 0.600 | 1/16 |
| **Σ / mean** | | **62.798s** | | | 0.449 | 0.449 | 0.246 | |

### `--iters 4000`

| case | pricing | wall time | iters | exact_evals | tau | tau_w | pool_tau | exact_best_cheap_rank/K |
|---|---|---:|---:|---:|---:|---:|---:|---|
| 0 | **legacy** | 1.147s | 12000 | 5304 | 0.629 | 0.629 | 0.583 | 1/16 |
| 1 | official | 9.909s | 12000 | 4312 | 0.807 | 0.807 | 0.167 | **15/16** |
| 2 | official | 30.587s | 12000 | 4556 | 0.530 | 0.530 | 0.567 | 5/16 |
| 3 | official | 37.339s | 12000 | 5864 | 0.462 | 0.462 | 0.500 | 11/16 |
| 4 | official | 61.003s | 12000 | 5771 | 0.481 | 0.481 | 0.183 | 11/16 |
| **Σ / mean** | | **139.985s** | | | 0.582 | 0.582 | 0.400 | |

(Runtime note: `--iters 4000` totals 139.985s, ~2.23x the default's 62.798s — not the naive 4x an
iteration-count-only view predicts, since a large share of per-run wall time is exact-eval routing
work whose count doesn't scale linearly with `iters`, and case3 actually picks a DIFFERENT pricing
leg than at default. Well under any suite-battery time budget either way.)

### Findings for B5 (what this table says about the elite pool / proxy)

1. **`tau_w` never differs from `tau` in any of these 10 runs.** `windowedTauB`'s trailing-64
   window only bites when a leg's `fidelity_pairs` exceeds 64 entries; at exact_interval=50 and
   these iters counts, the picked leg's own SA-loop trigger count stays under 64 in every single
   measured run here (confirmed directly: the earlier ad hoc case0 smoke recorded only 10
   `fidelity_pairs`). **The windowed/overall distinction is real (proven in the unit test) but
   does not yet show up at iters=1000/4000** — it needs either a much higher iteration count or a
   smaller `--exact-interval` to accumulate more than 64 recorded pairs per leg. This is itself a
   useful, honest data point: the adaptive-interval hook (and any future "densify late" schedule,
   Gelfand & Mitter) is aimed at a longer-run regime than the current default budget reaches, not
   at today's iters=1000/4000 scale.
2. **Overall `tau` is weak-to-moderate and highly case-dependent** (0.067 to 0.807 across the 10
   runs, mean ~0.52) — confirms the brief's premise directly: the cheap proxy is a real but
   noisy predictor of exact ranking, never near-perfect, sometimes barely better than chance
   (case0 default: 0.067).
3. **`pool_tau` (the elite pool's OWN cheap-vs-exact correlation) is frequently worse than the
   whole-run `tau`, and twice NEGATIVE** (case1 default -0.433, case3 default -0.067) — within the
   16-candidate elite pool specifically, cheap and exact orderings can be uncorrelated or even
   mildly inverted, not just noisy.
4. **`exact_best_cheap_rank` is the sharpest, most actionable signal.** 5 of the 10 measured runs
   (case1 both flags, case3 default, case4 `--iters 4000`, case3 `--iters 4000`) place the
   exact-best pool member at cheap rank > 8 — i.e. in the WORSE HALF of the 16-slot pool. Case3
   default is the extreme: the exact-best member sits at cheap rank **16/16**, dead last — the
   pool's cheap ranking would have to be read backwards to find it. This directly supports the
   brief's own hypothesis: "if exact-best routinely sits at cheap rank > 8, the K=16 pool is too
   small (Lau-Ho)" — on this evidence it is, for at least cases 1 and 3, roughly half the time.
5. Practically for B5: the abutment-credit / wire-median cheap-score changes (§5.1-3) should be
   re-measured against this SAME harness afterward (`tau`/`pool_tau`/`exact_best_cheap_rank`
   before vs. after) to confirm they actually raise rank fidelity rather than just relocating
   where the disagreement sits; and case1/case3's persistent bottom-half `exact_best_cheap_rank`
   is concrete evidence for widening K (or thinning the ladder some other way) rather than
   assuming today's fixed 16 is already enough.

## Deviations / honest findings

- No STOP condition was triggered. Byte-identity held on the first attempt (no fix-forward
  needed) — the observation-only design (record into new vectors; never read by any decision;
  adaptive-interval mutation fully gated behind its own flag) held up exactly as designed.
- `exact_evaluations`/`iters` reported in the stderr line CAN differ run-to-run for the SAME case
  at the SAME flags depending on which top-K pool members the elite-pool snapshot loop (new in
  B6) happens to find already cached vs. not — this affects only the REPORTED counters (pure
  bookkeeping, read by nothing), never `result.best_*`, so it does not threaten byte-identity of
  the `.cfg` output (verified empirically above) and is explicitly called out in
  `SearchResult.fidelity_pairs`'s doc comment.
- The elite-pool snapshot loop is a SEPARATE preliminary pass over `top_candidates` (not folded
  into the existing elite loop's own iteration body) — a deliberate reading of "at the start of
  the elite loop" as "before the elite loop's own work begins," matching the brief's "snapshot"
  language; net effect is identical either way since it is pure recording.
- Per the brief's own text, the pre/post-B6 `.cfg` byte-diff is the load-bearing check; the
  in-suite `test/regression_b6_20260724.cpp` instead pins determinism-across-two-invocations plus
  the existing suite battery (including b1/b3's upper-bound pins) as the ongoing regression guard,
  since hand-pinning a whole `.cfg`'s bytes as a literal constant would be an unmaintainable,
  brittle duplicate (same reasoning `regression_b4_20260724.cpp` already documents for its own
  analogous byte-identity property).
- No architectural or scope changes beyond the brief: `placement_engine.cpp`, `eval.cpp`,
  `routing.cpp`, `respace.cpp` untouched; `finalizeFeedthrough` and the two post-finalize
  outline-sweep passes untouched (not instrumented — brief names exactly two recording sites, the
  SA-loop trigger and the elite-pool snapshot).

## Commit

`feat(search): rank-fidelity instrumentation + flag-gated adaptive interval (B6)` — see
`git log` for the hash (recorded in `progress.md`'s ledger line).
