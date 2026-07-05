# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project

A C++17 solver for **ICCAD 2026 Problem E: Early Floorplanning with Global Route**. Given a set of blocks (edge / hard-macro / soft), a fixed outline, and a block-pair connection matrix, it produces a legal, non-overlapping, fully-routed floorplan written as a `.cfg` file. All code lives under `namespace efgr`.

Two companion docs define the domain and strategy and should be treated as authoritative:
- `CONTEXT.md` — contest-specific vocabulary (channels, bundles, feedthrough, the engine terms) and resolved design decisions.
- `docs/superpowers/specs/2026-06-14-long-term-placement-engine-design.md` — the **active** long-term direction (pluggable placement engines + engine comparison). Prefer long-term optimizer quality over quick patches; short-term routing/eval fixes are fine only if they support this architecture.

## Build, Test, Run

```bash
make                 # builds bin/efgr, then copies it to bin/EarlyFloorplanning_with_GlobalRoute
make clean           # removes src/*.o and both binaries
```
`make` compiles every `src/*.cpp` with `g++ -std=c++17 -O2 -Wall -Wextra -pedantic`. Keep the build warning-clean.

Run the solver:
```bash
bin/efgr input.csv [output.cfg] [--engine sp|bstar|hybrid] [--iters N] [--seed N] [--alpha X] [--exact-interval N]
```
- Default engine is `bstar`; default iters `1000`, seed `1`, exact-interval `50`.
- If `output.cfg` is omitted it defaults to the input path with a `.cfg` extension.
- `main.cpp` prints a one-line summary to **stderr** (engine, iters, score, overflow, validation, …). Exit code is `0` only when validation passes **and** there are no open routes; otherwise `1`.

Tests are shell + ad-hoc C++ regressions under `test/` (no unit framework). Each rebuilds first and passes explicit seeds for determinism:
```bash
bash test/run_case00.sh                        # smoke test: builds, runs case00, asserts OUTLINE/BLOCK/CHANNEL/PATH + 7 blocks
bash test/run_placement_engine_regressions.sh  # compiles test/placement_engine_regression.cpp, exercises sp/bstar/hybrid
bash test/run_20260618_regressions.sh          # compiles + runs test/regression_20260618.cpp
bash test/run_20260608_regressions.sh          # compiles + runs test/regression_20260608.cpp
```
Regression `.cpp` files are compiled directly (not via the Makefile), each linking only the specific `src/*.cpp` it needs — see the `g++` line in the corresponding `run_*.sh` when adding sources.

**On Windows, run these from a WSL shell at `/home/yyds/Early-Floorplanning-with-Global-Route`** (not the `\\wsl.localhost\...` UNC path) to avoid Git ownership issues and so `make`/`bash` behave.

## Architecture

The solver is a straight pipeline; understanding it requires reading `main.cpp` → `search.cpp` → `eval.cpp` → `routing.cpp` together:

1. **Parse** (`io.cpp` `parseProblem`): reads the contest CSV into a `Problem`. Handles UTF-8 BOM, quoted fields, the `BLOCK` table (area, w/h, aspect-ratio range, type, LOCATION, PORT EDGE, FT CONVERSION rates), the `MAX` outline row, the `α` weight, and the `ON CHIP INTERFACE CONNECTION` matrix → `Bundle`s (one per nonzero source/target net count).
2. **Search** (`search.cpp` `runPlacementSearch`): simulated annealing. Each iteration `mutate`s a `PlacementState`, `decode`s it to a `Placement`, and scores it with the cheap proxy. Accept/reject by SA temperature. **Two-tier evaluation** ("Periodic Exact Routing Evaluation"): most moves use `cheapScore` only; full routing via `exactScore` runs periodically (`--exact-interval`) and for new best candidates. A `placementKey`-based cache dedupes exact evaluations, and a final pass exact-scores the top-16 cheap candidates plus origin-compacted variants.
3. **Placement engines** (`placement_engine.cpp`, interface in `placement_engine.hpp`): the `PlacementEngine` abstraction (`initialState` / `mutate` / `decode` / `isValidState` / `seedFromPlacement`) decouples SA from representation. Two backends: `SequencePairEngine` (`sp`, two permutations + longest-path constraint-graph decode) and `BStarEngine` (`bstar`, tree topology + contour decode). `hybrid` (`runHybridPlacementSearch`) runs `sp` for global exploration, seeds `bstar` from its best placement, and keeps the better result. Edge blocks are anchored to outline sides by their LOCATION codes (T/B/L/R + segment) via `placeEdge`/`legalizeEdgeBlocks`, not placed by the SA topology.
4. **Scoring** (`eval.cpp`): `cheapScore` = `outline_area + alpha*HPWL + penalty`, where penalty covers overlap (`1e9`), out-of-outline (`1e12`), and a route-aware congestion/feedthrough proxy. `exactScore` replaces the HPWL proxy with real routed wirelength and adds penalties for open routes (`1e15`), channel overflow (`1e6`), and feedthrough area mismatch (`1e6`).
5. **Routing** (`routing.cpp`): `extractChannels` slices the outline into vertical strips at block edges and takes uncovered y-intervals as `Channel`s. `routeProblem` builds an adjacency graph over blocks + channels (rectangles that touch) and routes each bundle with Dijkstra using **channel-first fallback**: pass 0 routes through channels only; pass 1 additionally allows feedthrough through *soft* blocks (which then increases that block's required area per the FT conversion table). Bundles are split into ≤8 chunks of ~1000 nets; PORT EDGE constraints restrict which side a source/target may connect on.
6. **Validate** (`validate.cpp` `validateSolution`): checks outline containment, fixed-block dimensions, edge LOCATION satisfaction, block overlap, open routes, and port-edge legality. Returns messages, not exceptions.
7. **Output** (`io.cpp` `writeCfg`): writes `OUTLINE` / `BLOCK` / `CHANNEL` / `PATH` sections. The reported outline is the *used* bounding box (clamped to MAX), not necessarily the full MAX outline.

Shared data models (`Rect`, `Block`, `Bundle`, `Problem`, `Placement`, `Channel`, `Route`, `RoutingResult`, `Score`) and geometry helpers (`overlaps`, `overlapArea`, `manhattan`, `trim`, `kEps`) are all in `model.hpp`.

### Legacy note
`src/floorplan.cpp` / `floorplan.hpp` (`BTreeState`, `makeSeedPlacement`, `decodeState`, `mutateState`) is an **older order-only "first open slot" placement path**. It is *not* used by `main.cpp` or the current search — only `test/regression_20260608.cpp` links it. Do new placement work in `placement_engine.cpp`. (CONTEXT.md explicitly flags "the current order-only first-open-slot placement" as *not* real B*-tree.)

## Conventions

- 4-space indentation, C++17. Public declarations in `.hpp`, implementations in the matching `.cpp`, everything inside `namespace efgr`.
- `PascalCase` for structs/result types (`RoutingResult`, `BTreeState`); `lowerCamelCase` for functions/variables (`parseProblem`); `k`-prefixed constants (`kEps`). `exact_interval` (snake_case) is a pre-existing exception.
- Prefer small deterministic helpers; pass explicit seeds. Avoid broad refactors across unrelated modules.
- Commits use lightweight Conventional Commits (`feat:`, `fix:`, `docs:`, `docs(scope):`, `chore:`, `add:`). Do **not** commit `bin/`, object files, or temporary `.cfg` outputs (all gitignored).

## Model Orchestration (multi-model workflow)

This repo runs **Fable 5 as the dispatcher**, delegating work to cheaper/specialised executors so Fable usage is spent on coordination, not bulk work. Configured in `.claude/settings.json` (`model: claude-fable-5`, `effort: max`) and `.claude/agents/`.

| Role | Who | Use for |
|------|-----|---------|
| **Orchestrator** | Fable 5 — main model, `effort: max` | Plan, decompose, route work to the right executor, integrate results, make the final call. Avoid doing bulk edits or long grinds directly. |
| **Deep reasoning** | `deep-reasoner` subagent → **Opus** | Hard algorithm/design work, subtle root-causing, correctness & trade-off analysis. Returns plans and diagnoses, not sprawling edits. |
| **Mechanical execution** | `fast-worker` subagent → **Sonnet** | Well-specified edits, wiring, `make`, running the regression scripts. No design decisions. |
| **Peer engineer (2nd opinion)** | **Codex** (OpenAI, via the `gstack-codex` skill or `codex exec`) | Independent implementation or adversarial review from a *different model family* — cross-check the orchestrator's / Opus's conclusions. |

Dispatch rules for the orchestrator:

- **Think first, then delegate.** For anything non-trivial, produce a plan (yourself or via `deep-reasoner`) before touching code — consistent with the repo's long-term-quality objective (`CONTEXT.md`).
- **Route by task shape:** ambiguous / needs judgement → `deep-reasoner` (Opus); decided & mechanical → `fast-worker` (Sonnet); "is this actually correct?" / want a contrarian view → **Codex**.
- **Conserve Fable:** don't grind through bulk edits or long build/test loops on the main model — hand them to `fast-worker`. Reserve Fable for orchestration and final judgement.
- **Use Codex as a real peer, not a rubber stamp:** prefer its *challenge* / *review* modes on high-stakes changes (algorithm, scoring, or routing correctness) to catch what a single model family misses.
- **Verify before accepting:** every delegated change must build warning-clean (`-Wall -Wextra -pedantic`) and pass the relevant `test/run_*.sh` before it counts as done.

Invocation:

- **Subagents** — launch the `deep-reasoner` / `fast-worker` agents (Agent/Task tooling); each runs on its bound model automatically.
- **Codex** — the `gstack-codex` skill (`codex review` / `challenge` / `consult`) or `codex exec "<task>"` for a one-shot second opinion. Already installed (v0.141.0) and logged in via ChatGPT on both WSL and Windows.
