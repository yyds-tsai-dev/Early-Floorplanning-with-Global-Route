# Deep-mode tuning (L4c adoption) + full 8-case 600 s battery (2026-08-15, evening session)

Basis: the 2026-08-15 ledger NEXT queue (deep-mode tuning on the new path, then the full 8-case
600 s same-load battery M4 left uncovered), and M2 §1's sanctioned probe
(`docs/research/2026-08-15-case5-l1-m2-verdict-and-m1-wall-breakdown.md`: "v2-guided SA over
L4b-seeded starts — fidelity may pay once the right basin is already in the pool").

Protocol: case5, `--time-budget 600`, same-load pairing (all arms of a round launched
concurrently); every score is the solver's stderr `score=` (aligned metric of record,
b5a-report.md §10.5), spot-confirmed by the golden rescorer (`tools/rescore.cpp`) on every round-1
arm — all exact, `validation=ok`, `rc=0`, walls 529–564 s (≤ 700 ceiling) throughout.

## 1. Round 1 — four arms, seed 1 (4-way concurrent)

Control = `ec69c48` (M4 close-out HEAD). Probe binaries are one-knob patches of it.

| arm | knob | score | Δ vs control |
|---|---|---|---|
| control | — | 76,580,832 | — |
| deep5 | `deepLegCount` cap 3→5 | 84,331,771 | **+10.1% (worse)** |
| **v2deep** | `cheap_proxy_v2` forced on shelf-seeded deep legs | **60,394,215** | **−21.1%** |
| wideshelf | shelf grid nrows 2–8 × factors {0.70,0.85,1.08,1.30,1.50}, best-3, all 3 legs seeded | 70,307,558 | −8.2% |

- **v2deep's 60.39M is the best case5 result in repo history** (previous best: 74.65M, M2's
  noise-lucky v1 seed 3). OA 34.96M, 26 overflow nets, ft_penalty 1.44M — rescorer-exact.
- deep5 confirms depth beats breadth: 5 shallower slices (~35 s SA each) lose to 3 deeper ones.

## 2. Round 2 — {control, v2deep, combo} × seeds {1,2,3} (9-way concurrent)

combo = v2deep + wideshelf in one binary (all 3 deep legs shelf-seeded AND v2-forced — no
unseeded-v1 hedge left).

| seed | control | v2deep | combo |
|---|---|---|---|
| 1 | 84,331,771 | **64,471,567** | 74,106,705 |
| 2 | **85,111,424** | 89,978,796 | 87,341,712 |
| 3 | 73,879,547 | 80,134,288 | **66,736,837** |
| Σ | 243.3M | 234.6M | 228.2M |

Two structural observations:

- **The noise band is ~±8M.** Control at seed 1 scored 76.58M in round 1 and 84.33M in round 2 —
  same binary, seed, budget; only the concurrent load differed. Wall-budget SA is load-sensitive
  (the `(iter & 63)` wall check moves the cut point, and everything downstream of it), so
  single-cell deltas inside that band are weak evidence. Round-2's control-s1 landing EXACTLY on
  round-1's deep5 score (84,331,771.3553, 12 digits) is the same phenomenon seen from the other
  side: both runs' winning leg was the same unseeded deepVariants[2] leg at seed 1, whose SA had
  plateaued before either cut point — identical endpoint despite different slices.
- combo loses to v2deep by ~10M at the shipping seed: seeding+v2-forcing ALL three legs removes
  the unseeded v1 exploration leg, and the wider grid changes which shelves win the cheap-score
  pick. The hedge is load-bearing.

## 3. Round 3 — fresh-seed validation, {control, v2deep} × seeds {4,5} (4-way concurrent)

| seed | control | v2deep | Δ |
|---|---|---|---|
| 4 | 63,296,544 | 61,021,192 | −3.6% |
| 5 | 88,790,589 | 69,663,182 | −21.5% |

## 4. Decision: adopt v2deep as **L4c** (commit `59c0719`); reject deep5 / wideshelf / combo

Full same-load record, v2deep vs control: 6 pairings, **4 wins**, Σ 425.6M vs 472.0M (**−9.8%**),
means 73.7M vs 80.0M, and both of the repo's two best-ever case5 results (60.4M, 64.5M) — both at
seed 1, the bare-invocation shipping seed. Its two losses (s2 +5.7%, s3 +8.5%) sit at the edge of
the §2 noise band. The causal story matches M2 §1 exactly: v2's faithful gradients were a liability
when they had to FIND the basin (M2's global rejection stands), and are an asset once the shelf
seed has already chosen it.

Implementation: one forced flag on the leg-local Problem copy inside deep mode
(`force_proxy_v2 = shelf != nullptr` — seeded legs only, the unseeded leg keeps v1 as the
exploration hedge). Non-deep legs and budget-off paths structurally untouched.

Gates at `59c0719`: warning-clean build; budget-off byte-identity **8/8** vs `ec69c48`
(cases 0–4,6,7 at iters200, case5 at iters50); suites l3bc / l4b / cheap-proxy-v2 / b2 / f1 /
case00 / selfconsistency / cfg_score all green.

Rejected arms stay archived as measurement here (no code kept): deep5 (+10.1%), wideshelf alone
(−8.2% round 1 but subsumed by the combo test), combo (Σ-competitive but −10M worse than v2deep at
the shipping seed and hedge-less).

## 5. Full 8-case 600 s same-load battery — old (`a9e70db`) vs new (`59c0719` = HEAD+L4c)

Run overnight 08-15→16 from the durable workspace `~/efgr-l4c-battery/` (the first attempt died
when a session restart deleted the /tmp scratchpad under the detached runner — memory
`detached-runs-need-durable-dirs` records the lesson). Two case-pairs (4 processes) per batch,
`--time-budget 600 --seed 1`, every run `rc=0`, `validation=ok`, walls 480–570 s (≤ 700 ceiling).

| case | old | new (L4c) | Δ |
|---|---|---|---|
| 0 | 4,305,930 | 4,305,930 | 0 (cfg byte-equal) |
| 1 | 7,229,638.2 | 7,229,638.2 | 0 (cfg byte-equal) |
| 2 | 36,629,849.35 | 36,629,849.35 | 0 (cfg byte-equal) |
| 3 | 23,080,365.75 | 23,080,365.75 | 0 (cfg byte-equal) |
| 4 | 39,721,207.2569 | 39,721,207.2569 | 0 (cfg byte-equal) |
| **5** | 94,805,125.65 | **55,866,304.955** | **−41.1%** |
| 6 | 5,355,290 | 5,355,290 | 0 (cfg byte-equal) |
| 7 | 10,467,784.3626 | 10,467,784.3626 | 0 (cfg byte-equal) |
| Σ | 221,595,190.6 | **182,656,369.9** | **−17.6%** |

- **All seven non-case5 cases produced byte-identical .cfg files across the two binaries**
  (`cmp` on each pair): `deep=0` everywhere outside case5, so the entire L3bc/L4b/L3c.1/L4c stack
  is provably confined to case5-shaped instances at 600 s — the strongest possible form of the
  M4 guard, now covering all 8 cases.
- **case5 55,866,305 is the new all-time best** (previous: 60.39M this session, 74.65M before
  today): golden-rescored exact (`checker_cost` 55,866,304.955 == solver score), OA **30.90M**
  (beats the 32.6M record), **0 overflow nets**, ft_penalty 390K, `deep=1`/3 shelf-seeded legs,
  279,251 iters, `sa_wall` 421 s. The old arm landed on its known bad-middle-regime 94.8M basin
  (M3's new600 number, reproduced here by the OLD binary — the regime L3c.1 was built to kill).
- Note the old arm's case5 draw is unlucky-side (its own historical draws: 88.3M/94.8M); against
  old's BEST known 600 s draw (88.29M) L4c's 55.87M is still −36.7%.
- Champion-gap trajectory (0811 ranking, champion ≈ 43.3M): submitted 91.1M (+110%) → M4 76.25M
  (+76%) → **55.87M (+29%)**.

## 6. State at close + next

- HEAD `59c0719` (fable5): L4c landed, all gates green, 8-case battery clean. Battery artifacts
  (16 cfgs + stderr) kept in `~/efgr-l4c-battery/runs/`.
- NEXT (8/16+): optional further deep-mode tuning (candidates: shelf-grid shape at fixed hedge,
  deep-leg budget rebalance toward the winning seeded legs); then 8/19 freeze — `make static` +
  rockylinux:8 glibc check (memory `submission-binaries-must-be-static`) before upload; Codex
  post-freeze verification after 8/20 (memory `codex-unavailable-until-2026-08-20`).
