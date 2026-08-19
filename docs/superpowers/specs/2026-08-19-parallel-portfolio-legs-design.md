# Parallel portfolio legs (pthread) — design spec (2026-08-19)

Status: **design only, no code changed.** Basis: `docs/research/2026-08-19-beta-rerun-position-and-final-plan.md`
§2/§5-P2, `docs/research/2026-08-17-case5-r2-tuning-900s-and-l2a-verdicts.md` §2/§3, code audit of
`src/search.cpp` / `src/eval.cpp` / `src/placement_engine.cpp` / `src/routing.cpp` / `src/io.cpp` at
HEAD `075a192` (branch `fable5`), plus two new measurements taken for this spec (§6).

---

## 0. TL;DR — what to build, and the honest value split

The lever is **not** "make the solver faster". Wall-clock is already fixed at the 600 s budget on
7/8 cases, and R2 (08-17 §2) proved that *more search at the same budget buys ≈0* on those cases
(900 s vs 600 s: 7/8 byte-identical or noise, case4 ran 47 legs / 148K exact evals and improved 0).
Parallelising phase 1 or phase 2 therefore has **EV ≈ 0 on its own**. Saying otherwise would be the
easiest way to burn the last week for nothing.

The value is in exactly two places:

| Increment | Mechanism | Blast radius | EV estimate |
|---|---|---|---|
| **S1 — parallel deep legs + seed rounds** | case5's deep phase runs 3 legs *sequentially* on one seed; run them concurrently and spend the freed workers on **extra seed rounds**, folding by value. Harvest the ±8M seed variance instead of fighting it. | deep-mode instances only (case5 at 600 s; provably nothing else — §6.3) | **≈ −5.3 M** (measured seeds {60.21, 70.53, 54.92} M ⇒ min-of-3 vs shipping seed 1) |
| **S4 — budget-vs-N policy (optional)** | N threads at budget `B` does the same *leg work* as 1 thread at budget `N·B`. Since quality is flat above ~600 leg-seconds, run `B ≈ 600/N` and bank the runtime penalty. | all 8 cases | **≈ −5.8 M** (Σ rt 8.3 M → ~2.5 M) — **only if the A/B proves quality holds** |
| S2/S3 — parallel phase-1 / phase-2 waves | more legs in the same 600 s | all budget-mode cases | **≈ 0** standalone; they are the *enabler* for S4 and cheap once S1's scheduler exists |

Combined realistic EV **≈ −10 M** against a ~190 M current-HEAD total and a ~39 M gap to the 0811
champion — the largest single item on the 8/28 board, which matches the plan doc calling P2 "the one
big architecture change worth the extended deadline". Build order is therefore **S0 → S1 → S2/S3 →
S4**, and S1 is shippable alone.

---

## 1. Architecture

### 1.1 Worker model: wave-synchronous `std::async`, not a hand-rolled pool

**Decision: waves of `std::async(std::launch::async, …)` futures, joined in job-index order.**

A *wave* is up to `N` legs launched together; the orchestrator then consumes their futures **in job
index order** (`futures[0].get()`, `futures[1].get()`, …) and folds each into `PortfolioFold` as it
is retrieved. Nothing else in the process runs concurrently with the fold.

Why this and not a free-running work-stealing pool:

1. **Deterministic fold for a fixed leg set falls out for free.** Consuming futures in index order
   makes the admission sequence a function of the job list alone, never of completion order. This is
   load-bearing — see corruption risk #3 (§8).
2. **Incremental anytime writes are preserved.** `fold.admit` fires `on_improved` → `writeCfg`
   (`src/search.cpp:1312`, `src/main.cpp:265-267`). Blocking on `futures[0]` first does not lose
   parallelism (all wave members were already launched), so a leg that finishes early is published
   as soon as every lower-index leg is done. A join-everything-then-fold design would delay the
   first deep-phase checkpoint by ~500 s and weaken the B2 anytime safety net that the
   `anytime-write-partial-cfg-gotcha` memory exists for.
3. **Exception safety for free.** `future::get()` rethrows on the *orchestrator* thread. A raw
   `std::thread` whose functor throws calls `std::terminate` — with `writeCfg` throwing
   `std::runtime_error` (`src/io.cpp:255`) and `std::bad_alloc` reachable under N-way memory
   pressure, that is a real hazard.
4. **Least code.** No queue, no condvar, no atomics beyond what libstdc++ already does. At the 8/23
   gate, lines-of-new-concurrency is the risk metric that matters.

Cost: a wave is as long as its slowest member (tail latency). Accepted — the legs in any one wave
are the same registry/variant family, walls vary <2×, and the reasoning simplification is worth more
than ~10-20 % throughput at this deadline.

`N == 1` **must create no threads at all**: the wave runner takes the inline branch and calls
`runLeg` directly on the calling thread. That makes "degrade gracefully to N=1" structurally
identical to today's code path rather than merely numerically equal.

### 1.2 The one uniform semantic rule: *leg → wave*

Every place the budget arithmetic today counts **legs**, it counts **waves** instead, where
`waves_left = ceil(jobs_remaining / N)`. At `N == 1`, `waves_left == legs_left` and every formula
reduces to today's, term for term. Three call sites (§3). This is the whole "budget semantics under
parallelism" answer; resist inventing anything richer.

### 1.3 Proposed surface

New `src/parallel.hpp` (+ `.cpp`):

```cpp
namespace efgr {
// Resolved worker count for budget-mode leg parallelism (§4). Never returns < 1.
int resolveThreadCount(int requested /*0 = auto*/, bool disabled);
// One-shot std::async smoke test, memoised. False on a fully-static binary whose
// libpthread weak symbols were elided (§5) -- caller falls back to N=1.
bool asyncRuntimeOk();
}
```

`SearchOptions` gains **`int threads = 1;`** (search.hpp). Default 1 ⇒ every existing caller,
including all ~30 regression suites and every unit-test-constructed `SearchOptions`, is structurally
sequential without touching a line of them. `main.cpp` sets it **only** in budget mode:

```cpp
if (options.time_budget_sec > 0.0)
    options.threads = resolveThreadCount(argInt(argc, argv, "--threads", 0),
                                         hasFlag(argc, argv, "--no-parallel"));
```

That is the double gate for requirement 1 (§2).

---

## 2. Requirement 1 — explicit-iters byte identity

**Decision: parallelism is active only when `options.time_budget_sec > 0.0`.** Justification:

- `src/main.cpp:112-118` resolves an explicit `--iters` (without `--time-budget`) to budget **OFF**.
  Every regression suite passes explicit `--iters` (`grep -ho '--iters [0-9]*' test/run_*.sh` →
  25/40/50/200/500/1000). So a budget-mode-only gate leaves all of them **structurally** untouched,
  not merely numerically — the same discipline P0a already uses at `src/search.cpp:1434-1438`
  ("Budget-off short-circuits on the FIRST operand, so nothing past it … is even evaluated").
- Budget-off phase 1 *is* the definition of the default run and carries the repo's hardest gate
  (`src/search.hpp:548-551`: "md5-identical .cfg on all 5 default cases").
- Note the honest counter-argument: parallel phase 1 with an index-order fold *would* be bitwise
  identical, because each leg is a deterministic pure function computed entirely on one thread (no
  cross-thread FP reduction anywhere). We gate anyway, because the ~30 suites are then a **control**
  that keeps proving the sequential path is untouched, and because the gate costs nothing (the
  contest harness always invokes bare ⇒ always budget-on, `src/main.cpp:119-120`).
- Two suites touch a budget: `test/run_f1_regressions.sh` (parsing marker only, 2 s, never reaches
  the search) and `test/run_wl_ladder_regressions.sh:36` (`--time-budget 30` smoke, asserts
  `validation=ok`, not bytes). The latter becomes a **free parallel-path smoke test** — keep it.

---

## 3. Requirement 2 — shared-state audit and isolation table

Method: full read of `runPricingPortfolio` / `runHybridPlacementSearch` / `runPlacementSearch` /
`finalizeFeedthrough`, plus `grep -n "static"` (excluding `static_cast`) and a file-scope-variable
scan across all of `src/`.

**Headline finding: there is no non-const static or global anywhere in `src/`.** The only `static`
tokens in the tree are `static constexpr` arrays (`search.cpp:353,360`; `respace.cpp:575`) and
`static` member functions (`placement_engine.cpp:1132,1144`). This codebase was written pure by
accident of the GC6 determinism discipline, and that is why this lever is affordable at all.

| # | State | Where | Today's scope | Isolation decision |
|---|---|---|---|---|
| 1 | non-const statics / globals | — | **none exist** (verified) | nothing to do |
| 2 | `exact_cache` (`unordered_map<string, ExactEvalEntry>`) | `search.cpp:730`, closure `731-747` | function-local to each `runPlacementSearch` call | **per-leg, keep local. NEVER share** — see risk #1 |
| 3 | `inflated_exact_cache` | `search.cpp:632` | local to one `finalizeFeedthrough` pass | per-leg by construction |
| 4 | main SA rng `std::mt19937 rng(options.seed)` | `search.cpp:698`, `722` | per `runPlacementSearch` | per-leg copy; seeds derive from `options.seed` + fixed offsets only |
| 5 | insurance rng `ins_rng` | `search.cpp:823-824` | per call, `seed ^ 0x5eedu` | per-leg |
| 6 | finalize rng `mt19937(0x9e3779b9u)` | `search.cpp:552` | per call, constant seed | per-leg (constant seed is fine — no sharing) |
| 7 | hybrid seeding rngs `rng_a`/`rng_b`/`seed_rng` | `search.cpp:1129`, `1154`, `1118`, `1391` | per call | per-leg |
| 8 | `Problem p = problem;` leg-local copy | `search.cpp:1352-1381` | already a fresh copy per leg | **keep**; capture outer `problem` as `const&` read-only in the worker. This pre-existing property is what makes the whole design viable |
| 9 | engine objects | `makePlacementEngine`, `search.cpp:1386`, `1095-1096`; ctors `placement_engine.cpp:709`, `953` | `unique_ptr` per leg; classes hold only an immutable `move_portfolio_v2_` bool, all virtuals `const`, no `mutable` members | per-leg instances (already); would even be safe to share read-only |
| 10 | `PortfolioFold fold` | `search.cpp:1398`, `admit` at `1295-1315` | mutated per leg | **orchestrator thread only**, after `future::get()`, in job-index order. Never passed to a worker |
| 11 | `on_improved` → `writeCfg` (single `path + ".tmp"`) | `search.cpp:1312`; `io.cpp:243-255` | called from `fold.admit` | serialised by (10). Belt-and-braces: make the temp path unique (`path + "." + pid + "." + counter + ".tmp"`); rename stays atomic. See risk #2 |
| 12 | `leg_wall_times`, `phase1_max_leg_wall`, `phase1_legs_run`, `deep_mode`, `leg0_sa_wall_sec` | `search.cpp:1413`, `1423`, `1424`, `1431`, `1432` | phase-1 loop bookkeeping | orchestrator only. Workers return their own measured wall inside the per-job result struct; orchestrator stores into `leg_wall_times[i]` after join |
| 13 | negotiation history `history_channel` / `history_ft` | `routing.cpp:117-118`, doc `110-116` | explicitly call-local ("never static/global") | nothing to do |
| 14 | `rr.soft_feedthrough_nets` accumulation | `routing.cpp:255` comment | call-local | nothing to do |
| 15 | stderr | `main.cpp:127`, `291-344` only; `routing.cpp:766-796` is `#ifdef EFGR_ROUTE_TRACE` (off) | main thread, before/after search | no interleaving today. **Rule: no worker may write to `std::cerr`.** Any future per-leg logging must buffer per-leg and be flushed by the orchestrator |
| 16 | `std::ostringstream` in `placementKey` | `search.cpp:100-111` | fresh stream per call | thread-safe (nobody calls `std::locale::global`). **Perf caveat:** ostringstream construction touches shared locale facet refcounts; if scaling is sub-linear at N=8, replace with a manual `char` buffer producing byte-identical strings. Follow-up, not a blocker |
| 17 | glibc malloc arenas | — | implicit | high allocation churn (Placement/RoutingResult copies, map nodes, strings) across N threads. Default arena count (8×cores) is adequate; flag as a measured-scaling risk only |
| 18 | `SearchResult` whole-struct copies (hybrid fold, `search.cpp:1170-1178`) | | per-leg locals | nothing to do |

**Conclusion: no mutex is needed anywhere.** The isolation is achieved entirely by (a) legs already
being pure functions of a leg-local `Problem` copy, and (b) confining `PortfolioFold` + the anytime
writer to the orchestrator thread. If a design review finds it wants a mutex, that is a signal
something crossed the worker boundary that should not have.

---

## 4. Requirement 3 — budget semantics under parallelism

### 4.1 Deep phase (S1) — the primary change

Today (`src/search.cpp:1518-1533`): `n_deep = deepLegCount(budget, T0, M0) ∈ [1,3]` legs run
**sequentially**, leg `d` getting `slice = deepLegSlice(budget, elapsed, n_deep - d, M0)` and
variant `deep_variants[d % 3]`, with `shelf_seeds[d]` for `d < 2` and `force_proxy_v2 = (shelf != nullptr)`.
The only sequential dependencies are `elapsed` (which shrinks later slices) and `fold`.

Proposed job list:

```
rounds R = max(1, min(kMaxDeepSeedRounds /*4*/, N / n_deep))          // integer division
jobs    = [(r, d) for r in 0..R-1 for d in 0..n_deep-1]               // r-major, |jobs| = R*n_deep
job(r,d): variant = deep_variants[d % 3]
          shelf   = d < shelf_seeds.size() ? &shelf_seeds[d] : nullptr
          seed    = options.seed + kDeepSeedRoundStep * r             // 1000; r=0 ⇒ options.seed
          iters   = 2'000'000, force_gated_pads = true,
          force_proxy_v2 = (shelf != nullptr)
```

Scheduler:

```
done = 0
while done < |jobs|:
    wave       = min(N, |jobs| - done)
    waves_left = ceil((|jobs| - done) / N)
    elapsed    = now - phase1_start
    slice      = deepLegSlice(budget, elapsed, waves_left, M0)     // unchanged helper
    if slice <= 0: break
    futures = [async(runLeg(job)) for job in jobs[done .. done+wave)]
    for k in 0..wave-1: fold.admit(problem, futures[k].get(), variant_k, seed_k, on_improved)
    done += wave
```

**N = 1 reduces exactly to today**: `R = max(1, 0) = 1` ⇒ `jobs` = the same 3 legs at
`options.seed`; `wave = 1`; `waves_left = 3, 2, 1` == today's `n_deep - d`; slices, variants, shelf
seeds, `force_proxy_v2`, fold order, and `on_improved` sequence are all identical. This is the
strongest N=1-equivalence argument in the design and should be asserted by unit test (§7.2).

N = 3 ⇒ one wave, `waves_left = 1`, each leg gets `slice = remaining − 1.5·M0` (≈ 355 s at 600 s
budget vs today's 27.8/50.6/96.1 s — see §6.2). R2 says the extra depth buys ≈0; take it anyway, it
is free. N = 8 ⇒ `R = 2`: two seed draws × 3 variants in one wave (2 workers idle — accepted for
explainability). N = 12+ ⇒ `R = 4` (capped).

**The directive that follows from R2: spend free workers on SEED ROUNDS, not longer slices.** The
750 s run reproducing the 600 s score to 12 digits, and slicebias landing exactly on control@900's
score, both prove deep legs plateau inside their slice. Depth is saturated; variance is not.

### 4.2 P0a phase-1 launch gate (S2)

Today (`src/search.cpp:1438-1446`): before leg `i>0`, break if
`elapsed + 1.5·phase1_max_leg_wall > budget`. Under waves, apply the **identical** predicate **per
wave**, with `phase1_max_leg_wall` = running max over all *completed* legs' walls. Concurrency
inflates those walls, which over-estimates the next wave's cost ⇒ the gate stops **earlier**, never
later — the safe direction, and it is self-consistent (the next wave will also be contended).
Wave 0 always runs, preserving the anytime guarantee (leg 0 unconditional).

Behaviour change to flag in the A/B: with `N ≥ 10` the whole registry is one wave, the gate never
fires, so `phase1_complete` is true on instances where it is false today ⇒ phase 2 / the wl-ladder
block become reachable there. That is the "cheap machine" case and is the intended win, but it is a
behaviour change and must be measured, not assumed.

### 4.3 Deep-mode trigger

Today: `deepModeTrigger(budget, registry_size, T0)` = `2.0 · registry_size · T0 > budget`
(`search.cpp:1279-1282`, `1457`), with `T0` = leg 0's own wall. Under a parallel wave 0, leg 0's wall
is contended and would fire deep mode too readily. Generalise on the same *leg → wave* rule:

```
2.0 · ceil(registry_size / N) · T_wave0 > budget          // T_wave0 = max leg wall in wave 0
```

At `N = 1`: `ceil(10/1) = 10` waves and `T_wave0 = T0` ⇒ **byte-identical to today's formula**.
This is the principled generalisation: deep mode asks "can the registry fit twice *at the achieved
parallelism*", which is exactly what it always meant.

### 4.4 L3b governor — leave it alone

`src/search.cpp:771-780` sizes `full_treatment_cap ∈ [3,16]` and `finalize_repair_attempts ∈ [4,32]`
from `seed_exact_wall_sec` (one measured exact eval) against `options.time_budget_sec`. Under
contention `seed_exact_wall_sec` inflates ⇒ the governor budgets *less* machinery. **That is
correct and self-correcting**: a contended core genuinely makes each exact eval cost more wall, and
wall is exactly what the official cost function charges. The seed eval happens at leg start when the
wave is at full contention, so the sample is representative. **Decision: no change.** Same for the
SA loop's own `(iter & 63)` wall check (`search.cpp:856-860`) — wall-based, so it self-corrects.

One derived note: the post-SA machinery tail is *not* budget-checked; it runs to completion. With N
legs the tails run concurrently, so the overshoot is `max`, not `sum` — strictly better than
sequential. The `1.5·M0` reserve still covers ~1.3× HT inflation.

### 4.5 Phase 2 seed sweep (S3)

Today (`search.cpp:1621-1643`): rounds × variants, each leg gated by
`shouldRunPhase2Leg(remaining, leg_wall_times[i])`. Under waves: form a wave of up to N legs from
the round's remaining variants and gate the **wave** on
`shouldRunPhase2Leg(remaining, max_over_wave(leg_wall_times[i]))`, since the wave costs the max of
its members. At N=1 that is today's per-leg gate exactly. Round seed stays
`options.seed + 1000·round` — a function of the round index, never of completion order ⇒ unchanged
determinism. Termination rule ("a round that ran zero legs ends the sweep") carries over verbatim.

### 4.6 Which-legs-ran vs fold determinism

Budget mode is already wall-clock/machine dependent (leg launch gates, the L3b governor, the deep
trigger and every slice all read elapsed wall), and two runs of *today's* binary at 600 s are
already not byte-identical to each other. So a scheduling-dependent **leg set** is acceptable and
pre-existing. What must be schedule-independent is the **fold over a fixed leg set** — guaranteed by
index-order `future::get()` (§1.1) plus the tie caveat in risk #3.

---

## 5. Requirements 4/5/6/7 — N, memory, static build, kill switch

### 5.1 Thread count

```
N_hw   = std::thread::hardware_concurrency()                 // 0 ⇒ unspecified
N_aff  = CPU_COUNT of sched_getaffinity(0, …)                // respects cpuset/taskset; glibc 2.28 has it
N_mem  = max(1, floor(0.5 · total_RAM_bytes / kPerLegBytes)) // kPerLegBytes = 1 GiB, §5.2
N      = clamp(min(N_hw>0?N_hw:1, N_aff, N_mem, kMaxThreads /*8*/), 1, …)
if (--no-parallel || EFGR_THREADS==1 || !asyncRuntimeOk()) N = 1
if (--threads K given) N = clamp(K, 1, kMaxThreads)          // dev/A-B override, wins over auto
```

- `hardware_concurrency()` may return 0 (unspecified) ⇒ fall back to 1.
- **Use `sched_getaffinity` too.** In a CPU-limited container `hardware_concurrency()` reports the
  *host* core count; oversubscribing would slow every leg and inflate the wall the cost function
  charges. Do **not** attempt to parse cgroup `cpu.max` — fragile across v1/v2, and affinity covers
  the cpuset case (`nproc` uses exactly this).
- **Do not leave a core free.** The whole machine is ours during evaluation; an idle core is a
  forfeited seed draw. But **do not oversubscribe** (`N > cores`): every leg is CPU-bound, so
  oversubscription only adds context switches and inflates each leg's wall. Exactly `N = cores`.
- Hyperthreading: `hardware_concurrency` counts logical CPUs. 2 legs per physical core run ~1.3×
  slower each for ~1.3× aggregate throughput. Since we bank throughput as *more seed draws*, HT
  oversubscription is mildly positive. Accept the full logical count; it also inflates `T_wave0`,
  which only makes §4.3 more conservative.
- Report the resolved value as a `threads=<N>` telemetry field in main.cpp's stderr summary (no
  `.cfg` byte is affected). Add `deep_rounds=<R>` and, if nonzero, `leg_failures=<k>`.

### 5.2 Memory bound

New measurement taken for this spec (this box: 22 cores, 15 GiB; `/usr/bin/time -v`, HEAD binary):

| case | budget | wall | peak RSS | exact evals | routes | telemetry |
|---|---|---|---|---|---|---|
| case5 | 240 s | 109.5 s | **206 MB** | 1,972 | 372 | `deep=1 deep_legs=0 phase1_legs=1/10` |
| case4 | 240 s | 229.9 s | **103 MB** | 13,475 | 64 | `deep=1 deep_legs=3 phase1_legs=1/10` |

Per-entry cost scales with `RoutingResult` size, i.e. route count, not eval count: case4 took 6.8×
the evals at half the RSS. Extrapolating case5 to a full 600 s leg (~10 K evals at ~90 KB/entry)
gives **0.7–0.9 GB**, consistent with the 659 MB figure recorded in
`docs/research/2026-08-14-final-levers-verdicts-and-0811-intel.md:88`.

**Bound: `kPerLegBytes = 1 GiB`, `N_mem = floor(0.5 · total_RAM / 1 GiB)`.** On 16 GiB ⇒ 8; on 8 GiB
⇒ 4; on 4 GiB ⇒ 2. **Stated assumption: the eval box has ≥16 GiB** (typical RHEL 8.8 server; no
contrary evidence in any official document). `sysconf(_SC_PHYS_PAGES) * sysconf(_SC_PAGESIZE)` works
in a fully-static binary. `kMaxThreads = 8` caps it regardless.

The dominant term (`exact_cache`, `search.cpp:730`) is unbounded in principle. **Do not add a cache
cap in v1** — it would change `exact_evals` telemetry and buys nothing once `N_mem` bounds N. If a
future case ever OOMs, B2's anytime `.cfg` is already on disk (the safety net exists), and the
retreat is `--no-parallel`.

### 5.3 Static build

`Makefile:14-16` today: `$(CXX) $(CXXFLAGS) -static -static-libgcc -static-libstdc++ -o $(OFFICIAL_BIN) $(SRC)`
— **no `-pthread`**. Required changes:

```make
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -pedantic -pthread
STATIC_PTHREAD := -Wl,--whole-archive -lpthread -Wl,--no-whole-archive

static:
	mkdir -p bin
	$(CXX) $(CXXFLAGS) -static -static-libgcc -static-libstdc++ \
	    -o $(OFFICIAL_BIN) $(SRC) $(STATIC_PTHREAD)
```

The classic pitfall, and why `-pthread` alone is **not** sufficient on the glibc-2.28 target: on
glibc < 2.34 `libpthread` is a separate archive, and `-static` pulls in only the archive members
actually referenced. libc ships **weak no-op stubs** for `pthread_create` / `pthread_join` /
`pthread_key_create` / `pthread_cancel`, so the link succeeds while binding to the stubs. Symptoms
are runtime, not build-time: `std::system_error: Enable multithreading to use std::thread:
Operation not permitted`, a `std::call_once` deadlock, or a segfault at first thread creation.
`-Wl,--whole-archive -lpthread -Wl,--no-whole-archive` (placed **after** the objects) forces the
real implementations in. On the local glibc 2.39 host `libpthread.a` is an empty stub and the flag
is a harmless no-op — but the submission binary is built in `rockylinux:8` (memory
`submission-binaries-must-be-static`), which is exactly the toolchain where it matters. Keep it in a
variable so it can be blanked if a toolchain rejects it.

Belt-and-braces: **`asyncRuntimeOk()`** — at startup in budget mode, `std::async` a trivial lambda
and `get()` it inside `try/catch(const std::system_error&)`. On failure, fall back to `N = 1` and
say so in telemetry. This turns the single most likely field failure mode into a graceful
degradation rather than a crashed submission. It must exercise `std::async` specifically (which
pulls `std::thread` + `std::call_once`), not just `std::thread`.

Also verify: `ldd bin/EarlyFloorplanning_with_GlobalRoute` ⇒ "not a dynamic executable";
`sched_getaffinity` / `sysconf` are static-safe (no NSS involvement; we do no name lookups).

### 5.4 Kill switches (fallback posture)

| Lever | Precedence | Purpose |
|---|---|---|
| `-DEFGR_NO_PARALLEL` (compile-time) | highest | `#if`-compiles the wave runner down to the inline branch; produces a provably single-threaded binary same-day at freeze if anything smells |
| `--no-parallel` (CLI) | next | field/battery A-B off-arm |
| `EFGR_THREADS=<k>` (env) | next | change N without a rebuild during batteries |
| `--threads K` (CLI) | next | dev/A-B, wins over auto-detect |
| auto (`min(hw, affinity, mem, 8)`) | default | shipped, **iff** the A/B in §7.4 is clean |
| `asyncRuntimeOk()` false | forces N=1 | static-pthread field failure |

Shipped default is decided by the A/B, not by this document.

---

## 6. Supporting measurements and findings from the code read

### 6.1 Contention direction is safe
Every budget decision in the tree reads **wall** (`search.cpp:757-762`, `773-780`, `853-860`,
`1443-1445`, `1519-1521`). Contention inflates wall ⇒ every governor and gate becomes *more*
conservative. There is no CPU-time-based decision anywhere that would be fooled by concurrency.

### 6.2 case5 deep slices are tiny today (and half the budget can be wasted)
From the measured case5 run: `T0 ≈ 109 s`, `sa_wall(leg0) = 18.4 s` ⇒ `M0 ≈ 90.6 s` (83 % of leg 0
is machinery). At the **600 s** budget that yields `n_deep = 3` with slices
`491/3 − 136 = 27.8 s`, then `50.6 s`, then `96.1 s` — the deep legs get 27–96 s of SA each.
Parallel, one wave, they each get ~355 s.

At the **240 s** budget the same arithmetic gives `n_deep = 1` and `slice = 131 − 136 < 0` ⇒
**`deep_legs=0`, and 130 s of the 240 s budget is burned doing nothing** (measured: wall 109.5 s of
a 240 s budget). Two consequences: (a) this is an independent finding worth a follow-up regardless
of P2; (b) **S4 must never cut case5's budget** — the deep phase degrades discontinuously.

### 6.3 "Deep mode is case5-only" holds *only at 600 s*
`kDeepTriggerFitFactor = 2.0` (`search.cpp:1265-1276`) claims only case5-shaped instances can fire.
Measured: **case4 at a 240 s budget fires deep mode** (`deep=1 deep_legs=3`). So the invariant is
`budget = 600 s`-specific (case4's `T0` sits in 12 s < T0 ≤ 30 s). This matters twice: it *supports*
S1's tight blast-radius claim at the shipped budget, and it *warns* that S4's budget cut would pull
other cases into the deep path — so S1 and S4 interact and must not be A/B'd together.

### 6.4 Legality of threading
The 0811 Compilation Checklist's own recommended compile line includes `-pthread`, with no thread or
core-count rule stated (08-17 §3). That is affirmative evidence threading is expected, not merely
unprohibited. The cost function charges **wall**, not CPU, so N cores are free. If the organisers
later state a single-thread rule, `-DEFGR_NO_PARALLEL` is the one-line retreat (§5.4).

---

## 7. Test plan

### 7.1 Byte-identity (hard gate, blocking)
All ~30 `test/run_*.sh` green and unchanged, in particular `run_cfg_score_regressions.sh` (golden),
`run_selfconsistency_regressions.sh`, `run_case00.sh`, `run_integer_output_regressions.sh`,
`run_l3bc_regressions.sh`, `run_b2_regressions.sh`, `run_f1_regressions.sh`. Plus the repo's standard
8-case `--iters 200 --seed 1` md5 comparison against the pre-change binary. Structural argument:
`SearchOptions.threads` defaults 1 and `main.cpp` assigns it only under `time_budget_sec > 0`.

### 7.2 N=1 equivalence + determinism-under-concurrency (the centrepiece)
New `test/regression_parallel_20260820.cpp` + `test/run_parallel_regressions.sh`, following the
b2/l3bc "hoist pure helpers, unit-test them" precedent (`search.hpp:522-531`). Hoist the wave
scheduler behind an **injectable leg runner**
(`using LegRunnerFn = std::function<SearchResult(const SearchVariant&, const SearchOptions&, const Placement*, bool, bool, double)>`)
so it can be driven with canned results without running a real search. Assertions:

1. **N=1 identity**: for `n_deep ∈ {1,2,3}` and a canned elapsed/M0, the job list, per-job slice
   sequence, variant sequence, shelf assignment and seed sequence produced at `N=1` equal today's
   `deepLegSlice(budget, elapsed, n_deep - d, M0)` / `deep_variants[d%3]` / `options.seed`.
2. **Schedule independence**: the same canned leg set at `N=4`, with per-fake sleeps forcing several
   different completion orders, yields the **same fold winner and the same `on_improved` call
   sequence** as `N=1`.
3. **Tie behaviour**: two canned legs whose values differ by < `kEps` ⇒ the lower **job index** wins,
   in every N and every completion order (pins risk #3).
4. **Deep-trigger generalisation**: `2·ceil(10/N)·T_wave0 > budget` equals the current formula at
   `N=1` across a table of `(budget, T0)` including the case5 and case4 measured points.
5. **`resolveThreadCount`**: 0-hw ⇒ 1; `--no-parallel` ⇒ 1; memory bound clamps; cap at 8.

### 7.3 Race detection
`g++ -fsanitize=thread -std=c++17 -O1 -g` build (drop `-static`; TSan is dynamic-only), then
`case0 --time-budget 20 --threads 4` and `case4 --time-budget 60 --threads 4`. Feasible: the budget
is wall-based, so a 5–15× slowdown just means fewer iterations, not a longer test. **Prefer TSan
over Helgrind** (Helgrind is ~50× and noisier on libstdc++ internals). A suppressions file may be
needed for libstdc++/glibc internals; any hit in `efgr::` code is a blocking defect. Given §3's
audit, the expectation is zero — a hit means the audit missed something, which is precisely the
value of running it.

### 7.4 Value A/Bs (same-load, 3-seed, per the repo's B5a discipline)
- **S1**: case5 @600 s, seeds {1,2,3}, control (HEAD, sequential) vs `--threads 8`. Judge on **Σ over
  seeds**, not single cells (±8M per-cell noise, 08-17 §5). Expect ≈ −5 M. Note honestly: the
  mechanism *is* variance harvest, so a per-cell comparison is the wrong instrument by construction.
- **S1 blast-radius (hard gate)**: cases {0,1,2,3,4,6,7} @600 s ⇒ `.cfg` **md5-identical** to control,
  because deep mode does not fire there at 600 s (§6.3). A mismatch means the change leaked.
- **S2/S3**: full 8-case 600 s same-load; Σ score must not regress; Σ wall must stay ≪ 7,200 s.
- **S4** (only if S1–S3 are green by 8/23): 8-case `(600 s, N=1)` vs `(200 s, N=4)`, **case5 excluded
  from the budget cut** (§6.2). Adopt only if checker Σ is flat.
- Every run: `rc == 0`, `validation=ok`, golden rescorer spot-check, per-run timeouts, durable
  workspace with done-markers (memories `detached-runs-need-durable-dirs`,
  `anytime-write-partial-cfg-gotcha`).

### 7.5 Static / field verification
`make static` inside `rockylinux:8`; `ldd` ⇒ "not a dynamic executable"; run case0 bare inside the
container and confirm `threads=` > 1 in telemetry (i.e. `asyncRuntimeOk()` returned true and threads
really were created — not the weak-stub fallback). Repeat for `--threads 4`.

### 7.6 Repetition sanity
5× `case5 --time-budget 120 --threads 4 --seed 1`: all `rc=0`, `validation=ok`, `open_routes=0`;
score spread recorded and required to be no wider than the N=1 spread on the same box.

---

## 8. Top 3 ways this could silently corrupt results

**1. Sharing the exact-eval cache across legs (the tempting "optimisation").**
`exactCached` (`search.cpp:731-747`) closes over the **leg-local** `Problem p` — which differs per
leg in `route_pricing_official`, `abutment_credit`, `alpha`, `ft_pricing_aggressive`,
`cheap_proxy_v2`, `overflow_gated_pads` (`search.cpp:1352-1381`) — while the key is
`placementKey(placement)`, which encodes **geometry only, no pricing dimension**
(`search.cpp:100-111`). A shared cache would return a score computed under a *different pricing
model* for the same geometry, silently poisoning elite adoption, `outlineSweepBetter`, and the
portfolio fold. It would not crash, would not fail validation, and would produce plausible-but-wrong
winners. It would also race on `++result.exact_evaluations` (`search.cpp:743`).
**Guard:** never share; keep `exact_cache` a local of `runPlacementSearch` (it already is), add an
explicit "MUST NOT be hoisted/shared — key omits pricing" comment at its declaration, and add a unit
test asserting that two legs with different `route_pricing_official` yield **different** scores for
the same `placementKey`. Same argument kills any global memo table.

**2. Concurrent `writeCfg` into the single shared temp path.**
`io.cpp:252` writes `path + ".tmp"` then renames. Two workers publishing at once interleave into the
same temp file and both rename — the published `.cfg` can be a byte-salad of two placements that
still parses (right sections, right block count) but describes an inconsistent floorplan. Because a
killed run's checkpoint is *trusted* (memory `anytime-write-partial-cfg-gotcha`), that corrupt file
can silently become the submitted answer.
**Guard:** `on_improved` is invoked only from `PortfolioFold::admit` (`search.cpp:1312`), which is
only ever called on the orchestrator thread after `future::get()`; never pass `on_improved` into a
worker. Belt-and-braces: make the temp path unique per write
(`path + "." + getpid() + "." + counter + ".tmp"`) so even an accidental concurrent writer cannot
interleave; `rename` stays atomic. Optionally assert the calling thread id in a debug build.

**3. Completion-order folding flipping the winner at a `kEps` tie.**
`paydownBetter` (`search.cpp:123-130`) compares with `kEps` bands: two legs whose values differ by
< `kEps` are "not better" in *both* directions, so the **incumbent** wins and the outcome depends on
admission order. `search.hpp:541` states "fold order never changes the winner" — that is true only
for strict orderings and **false at a tie**; this spec corrects that claim. Under completion-order
folding the same leg set could yield different `.cfg` bytes run to run, destroying the one
determinism property we promised to keep. It is invisible: both answers are legal and scored within
`kEps`, so nothing fails.
**Guard:** consume futures strictly in **job index order** (§1.1) — never `wait_for_any`, never a
completion queue; unit-test the tie case explicitly (§7.2 assertion 3); document the tie caveat at
`paydownBetter` and at `PortfolioFold::admit`.

Runner-up hazards (guard, but not top-3): a worker exception reaching `std::terminate` — avoided by
`std::async` + `try/catch` around each `get()`, treating a failed leg as "not admitted" and counting
it in `leg_failures` rather than aborting a run that already has a good leg 0; OOM-kill under N-way
memory pressure — bounded by `N_mem` (§5.2), survivable via the anytime `.cfg`; and the fully-static
`libpthread` weak-symbol elision (§5.3) — turned from a crash into an N=1 fallback by
`asyncRuntimeOk()`.

---

## 9. Staged implementation plan and gates

| Stage | Content | Gate (blocking) | Target |
|---|---|---|---|
| **S0** | `src/parallel.hpp/.cpp` (`resolveThreadCount`, `asyncRuntimeOk`), `SearchOptions.threads = 1`, main.cpp wiring under `time_budget_sec > 0`, kill switches, Makefile `-pthread` + whole-archive, `threads=` telemetry. **No behaviour change — N is used nowhere yet.** | all ~30 suites green; `make` warning-clean; `make static` in rockylinux:8 + `ldd` + in-container run | 8/20 |
| **S1** | Parallel deep legs + seed rounds (§4.1), `deepModeTrigger` generalisation (§4.3), `test/regression_parallel_*` (§7.2) | §7.2 unit tests; §7.1 byte-identity; **7-case md5 byte-identity @600 s** (hard); TSan clean; case5 3-seed battery launched | 8/21 |
| **S2** | Parallel phase-1 waves + P0a wave gate (§4.2) | §7.1 byte-identity; 8-case 600 s same-load Σ not worse | 8/22 |
| **S3** | Parallel phase-2 seed-sweep waves (§4.5) | same as S2 | 8/22 |
| **decision gate** | Adopt / ship `--no-parallel` default. Codex adversarial review (returns 8/20) on §3 audit + §8 risks | all above | **8/23** |
| **S4 (optional)** | Budget-vs-N policy, **case5 excluded** (§6.2/§6.3) | its own 8-case A/B | 8/24–8/25 |
| **freeze** | static + rockylinux glibc check; full 8-case 600 s battery + golden rescorer + selfconsistency | — | ~8/26 |

**Drop-without-regret points:** S1 alone is shippable and carries most of the EV. S2/S3 alone are
shippable. S4 is strictly optional. If determinism or the static-pthread link is not airtight by
8/23, ship `-DEFGR_NO_PARALLEL` — P0/P1/P3/P4 do not depend on any of this.

**Explicitly out of scope:** sharing any cache or best-solution state between concurrent legs
(Cicirello, SoCS 2017 / arXiv:1709.02877, already cited in `search.hpp:560-572`: periodic
best-solution sharing across parallel SA chains has no statistically significant benefit over fully
independent chains — and it is precisely the change that would resurrect corruption risk #1);
intra-leg parallelism (parallel Dijkstra / parallel exact evals — much finer-grained, far higher
race surface, no measured need since SA is 65–72 % of case5's wall); any work-stealing pool.
