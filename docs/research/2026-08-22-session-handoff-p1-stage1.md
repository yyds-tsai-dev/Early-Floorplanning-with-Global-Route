# Session handoff: "research case5 & try v3 survey" pause point (2026-08-22)

Written at the pause of the case5-research / v3-survey session, before migrating to a new
server/environment. Read together with the plan doc
`docs/research/2026-08-19-beta-rerun-position-and-final-plan.md` (§5 is the authoritative
P0–P5 priority list to the 8/28 deadline). Everything below is committed on `fable5` and
merged to `main` — a fresh clone of `main` has the complete state; nothing lives only in
worktrees or scratchpads.

## 1. DONE — do not re-implement

All flag-gated, default-off unless noted; each has its own commit + regression suite.

| item | flag | commit | gate |
|---|---|---|---|
| D2 alpha-ladder portfolio legs | `--wl-ladder` | 4382f4f | run_*_regressions per commit |
| C1a WL-median whitespace redistribution | `--wl-redistribute` | 9781725 | + flag-ON self-consistency gate `test/run_c1_selfconsistency.sh` (this session) |
| D3 snap-to-neighbor soft resize | `--snap-resize` | e369ede | |
| B1 constant-gap negotiation pricing | `--const-gap-negotiation` | 39399e8 | |
| D4/A5 notch/pocket whitespace penalty | `--notch-penalty` | 075a192 | |
| C3 exact local-group LNS re-placement | `--lns-polish` | 0e591b7 | |
| **P1 plateau-stop STAGE 1 (observe-only)** | `--plateau-stop` / `--no-plateau-stop` / `--plateau-margin m` | this session | `test/run_plateau_regressions.sh` |

P1 stage 1 details (design: `.superpowers/sdd/p1-plateau-stop-design-20260819.md`, committed):
`runtimeSecondPrice` (δ·√OA, δ=0.5 verified digit-level vs official beta case_result.json),
pure predicate `plateauStopDecision` (S1–S5), `PlateauTracker` fed from the same
`PortfolioFold.admit` stream in every phase, decision point immediately before the phase-2
fit gate. **Bare invocation resolves to observe-only (`plateau_stop=false`)** — telemetry
(`plateau= plateau_t= plateau_v= plateau_spread=` on the summary line, `plateau_parse:`
marker at startup) is always stamped, but only the explicit `--plateau-stop` flag actually
breaks the phase-2 loop in stage 1.

Research verdicts already measured — do not re-run:
- 8/17 R2 battery (`docs/research/2026-08-17-case5-r2-tuning-900s-and-l2a-verdicts.md`):
  widegrid REJECT, slicebias REJECT, L2a `--ft-area-rung` inert on case5 (keep dormant),
  **900 s budget Pareto-bad vs 600 s** — case5 all-time best 54,917,186 (control, seed 3,
  600 s, HEAD 2f76d33-era binary).
- 5 consecutive deep-mode knob-probe rejections → knob-turning on case5 is exhausted; new
  mechanisms only (8/19 plan §5 P4).

## 2. NOT DONE — the resume queue (in plan-§5 priority order)

1. **P0 close-out (implemented, UNMEASURED)**: 600 s same-load 3-seed A/B batteries for the
   six batch-2 flags in §1 — family split {2,3,4} for C1a/D2/D3/C3, {4,5,7} for B1/notch,
   ±8M noise discipline on case5. Survivors go default.
2. **P1 STAGE 2 (the actual payoff)**: run an observe-mode battery (bare 600 s, cases
   0/1/6/7 especially) and read the `plateau_t`/`plateau_v` telemetry; if fires are
   value-safe, flip the bare-invocation resolution in `main.cpp` to `plateau_stop=true`
   (design §9 stage 2 — the code comment in `main.cpp` marks the exact spot) and re-battery.
   Expected win ~0.1–0.3M official cost per 100 s saved on the small cases.
3. **P2 parallel portfolio legs**: spec committed (066c722, `.superpowers/sdd/`), decision
   gate was 8/23 — not started. Requirements: deterministic fold, static `-pthread` build +
   rockylinux:8 check, adversarial review. Drop without regret if not airtight.
4. **P3/P4/P5** per plan §5: WL campaign on cases 2/3/4, case5 new-mechanism track,
   submission hygiene (refresh `submission/` from HEAD before any freeze; static build +
   docker glibc check — see memory/`submission-binaries-must-be-static`).

## 3. Environment / migration notes

- `main` == `fable5` after this session's merge; both pushed. Branches `cc/final-levers`
  and `cc/angry-brattain-6a6e6d` are ancestors of `fable5` (nothing unique).
- Old-server worktrees (`~/efgr-0817-battery/wt-{widegrid,slicebias}`) held only the two
  REJECTED experiment diffs — intentionally not committed; safe to delete with the old box.
- Test binaries `test/*_regression_bin` are build products (gitignored), rebuilt by their
  `run_*.sh`.
- Deadline: submission 2026-08-28, freeze ~08-26. Official box is ~3× faster than the dev
  WSL box (plateau thresholds transfer conservatively — design §8).
