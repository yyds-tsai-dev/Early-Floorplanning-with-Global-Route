# V1 B*-Tree CA-SA Periodic Router Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a CPU-only v1.0 solver that parses the contest CSV, searches placements with B*-tree SA, periodically runs bundled channel-first routing, validates the result, and writes `.cfg`.

**Architecture:** The solver is a compact C++17 command-line program. Modules are split by contest responsibility: parsing/model, B*-tree floorplanning, evaluation/routing, validation, output, and CLI orchestration. The first implementation favors debuggable correctness and a working case00 path over advanced negotiated reroute.

**Tech Stack:** C++17, standard library only, `make`, shell-based smoke tests.

---

## File Structure

- Create `Makefile`: builds `bin/efgr`.
- Create `src/model.hpp`: contest data structures and geometry helpers.
- Create `src/io.hpp`, `src/io.cpp`: CSV parser and `.cfg` writer.
- Create `src/floorplan.hpp`, `src/floorplan.cpp`: seed placement, B*-tree state, contour decode, SA moves.
- Create `src/routing.hpp`, `src/routing.cpp`: channel extraction, rectangle graph, bundled shortest-path routing, wirelength/capacity accounting.
- Create `src/eval.hpp`, `src/eval.cpp`: cheap proxy, exact evaluation, score composition.
- Create `src/validate.hpp`, `src/validate.cpp`: placement/route/output sanity checks.
- Create `src/main.cpp`: CLI, SA loop orchestration, diagnostics.
- Create `test/run_case00.sh`: build and smoke-run public case00.

## Task 1: Build Skeleton And Shared Model

**Files:**
- Create: `Makefile`
- Create: `src/model.hpp`
- Create: `src/main.cpp`

- [ ] **Step 1: Add build skeleton**

Create `Makefile` with:

```make
CXX ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -pedantic
SRC := $(wildcard src/*.cpp)
OBJ := $(SRC:.cpp=.o)
BIN := bin/efgr

.PHONY: all clean

all: $(BIN)

$(BIN): $(OBJ)
	mkdir -p bin
	$(CXX) $(CXXFLAGS) -o $@ $(OBJ)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c -o $@ $<

clean:
	rm -f src/*.o $(BIN)
```

- [ ] **Step 2: Add model types**

Create `src/model.hpp` with enums for block types, rectangles, blocks, problems, placements, channels, route steps, route bundles, and scores. Include helpers for rectangle overlap, centers, and edge names.

- [ ] **Step 3: Add temporary CLI**

Create `src/main.cpp` with a CLI that accepts `input.csv output.cfg [--seed N] [--iters N] [--alpha X]` and prints a clear "not implemented" message until later tasks fill it.

- [ ] **Step 4: Build**

Run: `make clean && make`

Expected: `bin/efgr` builds without warnings that indicate missing symbols.

## Task 2: Parser And CFG Writer

**Files:**
- Create: `src/io.hpp`
- Create: `src/io.cpp`
- Modify: `src/main.cpp`

- [ ] **Step 1: Implement parser**

Implement `parseProblem(path)` that reads the official CSV layout used by `test/E_testcase_20260414.xlsx - case00.csv`: block table, outline max width/height, alpha, and connection matrix. Parse quoted multi-line headers by using a small CSV tokenizer that respects quotes.

- [ ] **Step 2: Implement writer**

Implement `writeCfg(path, problem, placement, channels, routes)` with sections `OUTLINE`, `BLOCK`, `CHANNEL`, and `PATH`, each ending in `END`. Edge ids are numeric: left 1, top 2, right 3, bottom 4.

- [ ] **Step 3: Wire parser into CLI**

Update `main.cpp` to parse input and print block count, bundle count, outline, and alpha when no solver is ready.

- [ ] **Step 4: Build and smoke parse**

Run: `make clean && make && bin/efgr "test/E_testcase_20260414.xlsx - case00.csv" /tmp/case00.cfg --iters 1`

Expected: prints 7 blocks, outline 2916 x 2220, alpha 0.2.

## Task 3: Seed Placement And B*-Tree SA

**Files:**
- Create: `src/floorplan.hpp`
- Create: `src/floorplan.cpp`
- Modify: `src/main.cpp`

- [ ] **Step 1: Implement dimensions**

Implement hard/edge fixed dimensions and soft block shape assignment. Soft blocks start near square and clamp aspect ratio within `[min,max]`.

- [ ] **Step 2: Implement deterministic seed**

Place edge blocks on legal boundary regions, then pack non-edge blocks left-to-right/top-to-bottom within outline. If seed overflows, keep coordinates and let validation/evaluation penalize it.

- [ ] **Step 3: Implement B*-tree state**

Represent each node with `block`, `parent`, `left`, `right`; provide initialization from seed order and contour decode for non-edge blocks. Preserve edge block anchors after decode.

- [ ] **Step 4: Implement SA moves**

Implement swap block labels, detach/reattach leaf or subtree, soft aspect-ratio perturbation, and edge legal-location move. Reject structurally invalid trees.

- [ ] **Step 5: Wire minimal SA loop**

Run cheap evaluation using area and overlap penalties; Task 5 will expand the score with routing-aware terms.

- [ ] **Step 6: Build**

Run: `make clean && make`

Expected: solver runs for at least 100 iterations without crashing.

## Task 4: Routing And Channel Extraction

**Files:**
- Create: `src/routing.hpp`
- Create: `src/routing.cpp`

- [ ] **Step 1: Extract vertical-strip channels**

Collect `0`, outline width, and every block `x`/`x+w`; for each adjacent x pair, subtract covered y-intervals from placed blocks and emit uncovered rectangles as channels.

- [ ] **Step 2: Build adjacency graph**

Create graph nodes for blocks and channels. Add an edge when two rectangles touch and their shared edge segment has positive length.

- [ ] **Step 3: Route bundles**

For each nonzero connection matrix entry, run Dijkstra over channel nodes plus source/target. If channel-only fails, rerun allowing soft blocks as transit with feedthrough cost.

- [ ] **Step 4: Compute wirelength**

For each adjacent pair in a route path, compute the midpoint of their overlap segment as a guiding point and sum Manhattan distances times net count.

- [ ] **Step 5: Track capacities**

For each channel used by a bundle, add bundle net count to channel usage. Capacity is `25 * min(width,height)` as a conservative first proxy.

## Task 5: Evaluation And Validation

**Files:**
- Create: `src/eval.hpp`
- Create: `src/eval.cpp`
- Create: `src/validate.hpp`
- Create: `src/validate.cpp`
- Modify: `src/main.cpp`

- [ ] **Step 1: Implement cheap score**

Compute outline area, overlap penalty, outline penalty, edge-location penalty, weighted center distance, and coarse congestion proxy.

- [ ] **Step 2: Implement exact score**

Call channel extraction and bundled routing. Add route-open penalty, `alpha * wirelength`, channel overflow penalty, feedthrough mismatch penalty, and parser-safety penalty.

- [ ] **Step 3: Implement validator**

Check no overlap, all blocks in outline, edge blocks touch one legal side, every route starts and ends at expected blocks, all channel references exist, and every path step edge id is in 1..4.

- [ ] **Step 4: Select best exact candidate**

Keep an elite archive. Exact-evaluate every fixed interval and every new cheap-score best. Emit the lowest exact score candidate.

## Task 6: CLI Completion And Smoke Test

**Files:**
- Modify: `src/main.cpp`
- Create: `test/run_case00.sh`

- [ ] **Step 1: Complete CLI**

`bin/efgr input.csv output.cfg --iters N --seed S --alpha X --exact-interval K` runs SA, writes cfg, and prints final diagnostics: score, wirelength, channels, routes, overflow, validation status.

- [ ] **Step 2: Add smoke script**

Create `test/run_case00.sh`:

```bash
#!/usr/bin/env bash
set -euo pipefail
make clean
make
bin/efgr "test/E_testcase_20260414.xlsx - case00.csv" /tmp/case00.cfg --iters 500 --seed 1 --exact-interval 25
test -s /tmp/case00.cfg
grep -q '^OUTLINE' /tmp/case00.cfg
grep -q '^BLOCK' /tmp/case00.cfg
grep -q '^CHANNEL' /tmp/case00.cfg
grep -q '^PATH' /tmp/case00.cfg
```

- [ ] **Step 3: Run smoke test**

Run: `bash test/run_case00.sh`

Expected: command exits 0 and `/tmp/case00.cfg` contains outline, block, channel, and path sections.

## Self-Review

- Spec coverage: parser, B*-tree CA-SA, boundary edge moves, cheap proxy, periodic exact routing, channel-first fallback, cfg writer, validation, and case00 smoke test are covered.
- Scope control: full negotiated rip-up/reroute, SCB-tree, visualizer, and parallel multi-seed are explicitly left for later.
- Placeholder scan: no implementation step depends on unspecified external libraries or future solver integration.
