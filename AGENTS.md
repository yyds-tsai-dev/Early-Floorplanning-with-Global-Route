# Repository Guidelines

## Project Structure & Module Organization

This repository contains a C++17 solver for early floorplanning with global routing. Source files live in `src/`, with paired `.cpp` and `.hpp` modules such as `floorplan`, `routing`, `eval`, `io`, and `validate`. Shared data models are in `src/model.hpp` under the `efgr` namespace. Test inputs and smoke-test scripts live in `test/`. Official problem PDFs are kept in `docs/official/`, research notes in `docs/research/`, and helper scripts in `scripts/`.

## Build, Test, and Development Commands

- `make`: builds the solver at `bin/efgr` using `g++`, C++17, `-O2`, `-Wall`, `-Wextra`, and `-pedantic`.
- `make clean`: removes `src/*.o` and `bin/efgr`.
- `bash test/run_case00.sh`: rebuilds the project, runs the sample case, writes `/tmp/case00.cfg`, and checks for required `OUTLINE`, `BLOCK`, `CHANNEL`, and `PATH` sections.
- `bin/efgr input.csv output.cfg --iters 500 --seed 1 --exact-interval 25`: runs the solver manually with deterministic annealing settings.

When working from Windows, prefer WSL-native commands from `/home/yyds/Early-Floorplanning-with-Global-Route` to avoid UNC Git ownership issues.

## Coding Style & Naming Conventions

Use 4-space indentation and keep code compatible with C++17. Place public declarations in `.hpp` files and implementations in matching `.cpp` files. Keep project symbols inside `namespace efgr`. Current naming patterns use `PascalCase` for structs and result types (`RoutingResult`, `BTreeState`), `lowerCamelCase` for functions and variables (`parseProblem`, `exact_interval` is an existing exception), and `k`-prefixed constants such as `kEps`. Prefer small, deterministic helpers and avoid broad refactors in unrelated modules.

## Testing Guidelines

There is no full unit-test framework yet; use shell-based regression tests. Add new executable test scripts under `test/` and keep them deterministic by passing explicit seeds. Each test should build or assume a fresh `make`, run `bin/efgr` on a committed input, verify the output file exists, and assert key CFG sections or expected counts.

## Commit & Pull Request Guidelines

Follow the existing lightweight Conventional Commit style: `feat:`, `fix:`, `docs:`, `docs(scope):`, `chore:`, or `add:`. Keep commits focused and mention solver behavior changes clearly. Pull requests should include a short summary, changed modules, commands run, any output-score impact, and links to relevant problem documents or issues. Include generated CFG snippets only when they clarify behavior; do not commit `bin/efgr`, object files, or temporary outputs.

## Agent-Specific Instructions

Prefer codebase-memory MCP graph tools for code discovery when an index is available; fall back to local file search only when the graph has no project index or insufficient results. Preserve user changes and avoid destructive Git commands unless explicitly requested.
