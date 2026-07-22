# Beta Official-Cost Alignment Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Before the beta deadline (2026-07-24), align the solver's exact scoring with the official ICCAD-2026-E checker cost (validated byte-exactly against the alpha-test feedback package), emit integer-only output, and pick the beta default search budget by measured sweep — so the SA optimizes the real objective and the no-flag binary is the best configuration we can prove.

**Architecture:** Four thrusts. (1) A **golden rescorer**: a `.cfg` re-reader + official-cost evaluator that shares the exact route-accounting helpers with `routing.cpp`, locked to the 5 official `report.json` files as a permanent regression. (2) **Restore the stashed C1** eval mapping (`kBetaLin·A·R + kBetaTail·A·max(0,R−θ)`) and reconcile it with whatever tail form the rescorer proves. (3) **Integer quantization at decode** so validation/routing/output all see the same integer geometry (QA A43). (4) A **`--time-budget` stop condition + sweep** over engine × budget × iters on the 20260712 testcases, dual-cost reported, to choose the hardcoded beta default.

**Tech Stack:** C++17 (`g++ -std=c++17 -O2 -Wall -Wextra -pedantic`, warning-clean), bash test runners under `test/`, no external libraries. All commands run from a WSL shell at `/home/yyds/Early-Floorplanning-with-Global-Route`.

## Global Constraints

- Official checker cost (validated 2026-07-22 against the alpha package): `Cost = OutlineArea + α·WL + β_lin·OutlineArea·R + β_tail·OutlineArea·max(0, R − θ) + β_ft·ft_area_excess`, with `R = overflow_total / total_capacity` (both in nets), `overflow_total = Σ_ch [max(0, demand_h − 25·h) + max(0, demand_v − 25·w)]`, `total_capacity = Σ_ch [25·h + 25·w]`.
- Constants: `β_lin = 20.0`, `β_tail = 1.5`, `θ = 0.05`, `β_ft = 3.0`, channel capacity `25 nets/um` per direction (H capped by channel height, V by width).
- **Known residual:** the formula above reproduces `report.json` `cost` byte-exactly on cases 0/2/3/4 but is **0.31% off on case1** (the only case with R > θ). Task 2 must falsify tail hypotheses against case1's per-channel data; until resolved, case1 asserts at ≤0.5% and the delta is printed, and `kBetaTail`-form is whatever the falsification proves.
- α policy (team decision 2026-07-22, recorded in CONTEXT.md): **the solver optimizes the CSV α**. The scoreboard's per-case α (case0→0.3, case1→0.15, case2/3/4→0.1) and the runtime penalty `0.5 · runtime_sec · √OutlineArea` are **benchmark-report-only** — never read by solver code.
- Open routes remain a hard reject (`1e15` per open route) — feasibility before cost.
- Output `.cfg` must contain **integers only** (QA A43); soft block integer W×H ≤ AREA when equality is impossible (A43.3); FT-inflated soft blocks take the minimal integer W×H ≥ required area.
- The official harness runs `./EarlyFloorplanning_with_GlobalRoute <case.csv>` with **no flags** — every improvement must be default behavior. Binary name is fixed (Makefile already copies `bin/efgr` → `bin/EarlyFloorplanning_with_GlobalRoute`).
- Official runtime budget: all 8 cases (5 public + 3 unreleased) within 2 hours total (QA A42). The chosen beta default must project to ≤ 30 minutes for 8 cases (≥ 4× safety margin).
- Every task ends with: `make` warning-clean + the task's regression(s) green. Suites that must stay green throughout: `test/run_case00.sh`, `test/run_placement_engine_regressions.sh`, `test/run_20260618_regressions.sh`, `test/run_20260608_regressions.sh`, `test/run_p0_regressions.sh`, `test/run_p1_regressions.sh`, `test/run_p2_regressions.sh`, `test/run_p3_regressions.sh`.
- **Do not add a second `main()` under `src/`** — the Makefile links every `src/*.cpp` into `bin/efgr`. CLI tools go under `tools/` with explicit compile lines in their runner scripts.
- `git stash stash@{0}` ("WIP on fable5: 6d2de22") contains the C1 implementation (`src/eval.cpp`, `src/model.hpp`, `src/route_cost.hpp`, `src/routing.cpp`). It was made on the same HEAD we are on. **Only Task 3 may pop it**; before popping, re-verify with `git stash list` that it still exists and `git status` shows no `src/` modifications (another session may be active — if the stash is gone or `src/` is dirty, STOP and ask the user).
- Golden data lives in `docs/official/alpha_test/cade0002/` (5 × `output.cfg` + `report.json`); the 20260712 testcases are the authoritative inputs. Commits use Conventional Commits; never commit `bin/`, `.o`, or generated `.cfg`.
- Determinism: all regressions pass explicit `--seed`/`--iters`; the time-budget stop (Task 5) must default OFF in every test invocation.

**Risk fuse (user-approved):** if Task 3's verification shows a regression that cannot be resolved within the timeline, beta ships with C1 **reverted to benchmark-only** (rescorer + sweep still land; `eval.cpp` keeps the 1e6 mapping) — record the decision in `.superpowers/sdd/progress.md` and move on to Task 5.

**Alpha-test facts the plan encodes (for context):** all 5 cases PASS; overflow penalty totals 34.1M of 228.9M scoreboard cost (14.9%); case1's R = 0.0538 breaches θ = 0.05; our v3-qa0618 internal overflow estimate was 3–5× off vs the checker on cases 2–4; the solver never used feedthrough on any case; official runtimes 0.14–5.4 s at iters=1000.

---

### Task 0: Testcase refresh + diagonal-entry guard

**Files:**
- Modify: `test/2026 ICCAD_case.case0.csv`, `test/2026 ICCAD_case.case1.csv` (replace with 20260712 versions; case2/3/4 are byte-identical, replace anyway for provenance)
- Test: `test/regression_testdata_20260712.cpp`, `test/run_testdata_regressions.sh`
- Keep: `test/E_testcase_20260712.rar` (source archive, already in repo)

**Interfaces:**
- Consumes: `parseProblem(const std::string& path)` from `src/io.hpp`; `Problem`/`Bundle` from `src/model.hpp`.
- Produces: refreshed CSVs every later task runs against; a regression asserting (a) case0 BLK01 = 420×843 / MAX 2917×2220, (b) case1 BLK11 `port_edge == 3`, (c) **no bundle with `source == target`** on case3 (matrix has BLK09→BLK09=63 and BLK13→BLK13=123 — organizer-confirmed errata, QA A44, still present in 20260712).

- [ ] **Step 1: Extract the rar over `test/`**

```bash
cd /home/yyds/Early-Floorplanning-with-Global-Route
mkdir -p /tmp/tc20260712 && cd /tmp/tc20260712
# bsdtar reads this rar (WSL has no unrar; Windows tar.exe proved it readable)
tar -xf /home/yyds/Early-Floorplanning-with-Global-Route/test/E_testcase_20260712.rar
cp "testcase - upload - 20260712"/*.csv /home/yyds/Early-Floorplanning-with-Global-Route/test/
cd /home/yyds/Early-Floorplanning-with-Global-Route && git diff --stat -- test/
```

Expected: only `case0` and `case1` CSVs show content changes. If WSL `tar` cannot read the rar, run the extraction from the Windows side (`tar -xf` in PowerShell) into a temp dir and copy over.

- [ ] **Step 2: Write the failing regression**

Create `test/regression_testdata_20260712.cpp`:

```cpp
#include <cassert>
#include <cstdio>
#include <cmath>
#include "../src/io.hpp"

using namespace efgr;

int main() {
    // (a) case0: BLK01 fixed dims + MAX outline per 20260712 upload (QA A39)
    Problem p0 = parseProblem("test/2026 ICCAD_case.case0.csv");
    assert(std::abs(p0.outline_w - 2917.0) < 1e-9);
    assert(std::abs(p0.outline_h - 2220.0) < 1e-9);
    const Block& blk01 = p0.blocks.at(p0.block_index.at("BLK01"));
    assert(std::abs(blk01.width - 420.0) < 1e-9);
    assert(std::abs(blk01.height - 843.0) < 1e-9);
    assert(std::abs(blk01.area - 354060.0) < 1e-9);

    // (b) case1: BLK11 PORT EDGE now 3 (QA A36 — blank was erroneous)
    Problem p1 = parseProblem("test/2026 ICCAD_case.case1.csv");
    const Block& blk11 = p1.blocks.at(p1.block_index.at("BLK11"));
    assert(blk11.port_edge == 3 || (!blk11.port_edges.empty() && blk11.port_edges[0] == 3));

    // (c) case3: diagonal matrix entries must NOT become self-bundles (QA A44 errata)
    Problem p3 = parseProblem("test/2026 ICCAD_case.case3.csv");
    for (const Bundle& b : p3.bundles) assert(b.source != b.target);

    std::puts("testdata 20260712 regressions passed");
    return 0;
}
```

Create `test/run_testdata_regressions.sh` (mirror the existing runner pattern):

```bash
#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."

make >/dev/null

g++ -std=c++17 -O2 -Wall -Wextra -pedantic \
    test/regression_testdata_20260712.cpp \
    src/io.cpp \
    -o test/testdata_regression_bin

./test/testdata_regression_bin
echo "testdata regressions passed"
```

- [ ] **Step 3: Run it — assertion (c) is the one that may fail**

Run: `bash test/run_testdata_regressions.sh`
Expected: if `parseProblem` currently creates self-bundles from diagonal entries, assertion (c) aborts. If it already skips them (`r == c` guard in the matrix loop around `src/io.cpp:183`), the test passes — then Step 4 is a no-op; keep the regression either way.

- [ ] **Step 4 (only if Step 3 failed): Skip diagonal entries in the matrix parse**

In `src/io.cpp`, in the connection-matrix loop that creates `Bundle`s (around line 183 where `nets` is read), add the guard before pushing a bundle:

```cpp
if (source_index == target_index) continue;  // QA A44: diagonal entries are data errata
```

(Adapt the variable names to the loop's actual row/column index names.)

- [ ] **Step 5: Re-run + full suites**

Run: `bash test/run_testdata_regressions.sh && bash test/run_case00.sh && bash test/run_placement_engine_regressions.sh`
Expected: all pass. (case00 smoke uses its own bundled CSV; unaffected by the refresh.)

- [ ] **Step 6: Commit**

```bash
git add "test/2026 ICCAD_case.case0.csv" "test/2026 ICCAD_case.case1.csv" \
        "test/2026 ICCAD_case.case2.csv" "test/2026 ICCAD_case.case3.csv" \
        "test/2026 ICCAD_case.case4.csv" \
        test/regression_testdata_20260712.cpp test/run_testdata_regressions.sh src/io.cpp
git commit -m "chore(test): refresh testcases to 20260712 upload + diagonal-entry guard"
```

---

### Task 1: Extract shared route-accounting helpers (byte-identical refactor)

**Files:**
- Create: `src/route_geometry.hpp`
- Modify: `src/routing.cpp` (move code out; include the new header)
- Test: byte-identity harness below (no new .cpp — uses the solver binary)

**Interfaces:**
- Consumes: `Rect`, `Channel`, `kEps`, `manhattan` from `src/model.hpp`.
- Produces (exact signatures Task 2 builds on):

```cpp
namespace efgr {
// Edge numbering (QA A22): 1 = left, 2 = top, 3 = right, 4 = bottom (clockwise from left).
int edgeFromTo(const Rect& a, const Rect& b);              // moved verbatim from routing.cpp:24
std::pair<double, double> guidePoint(const Rect& a, const Rect& b);  // moved verbatim from routing.cpp:31

// One routed rect-sequence's contribution to wirelength and per-channel directional usage.
// `rect_of(i)` returns the rect of the i-th node in the path; `channel_of(i)` returns the
// Channel* if node i is a channel else nullptr. Implements exactly the accumulation loop
// currently inlined in routing.cpp:316-345: guides = midpoints of consecutive shared borders
// (QA A41), per-segment dx>kEps charges horizontal_usage, dy>kEps charges vertical_usage
// on the channel that owns the segment, and WL = nets * Σ manhattan(consecutive guides).
double accumulatePathUsage(const std::vector<Rect>& node_rects,
                           const std::vector<Channel*>& node_channels,
                           double nets);
}
```

- [ ] **Step 1: Capture the pre-refactor baseline**

```bash
cd /home/yyds/Early-Floorplanning-with-Global-Route && make >/dev/null
for i in 0 1 2 3 4; do
  bin/efgr "test/2026 ICCAD_case.case$i.csv" /tmp/base_case$i.cfg --seed 1 --iters 200 2>/dev/null
done
md5sum /tmp/base_case*.cfg > /tmp/baseline.md5
```

- [ ] **Step 2: Create `src/route_geometry.hpp` and move the helpers**

Move `edgeFromTo` (routing.cpp:24) and `guidePoint` (routing.cpp:31) into the header as `inline` functions, unchanged. Extract the accumulation loop at routing.cpp:316-345 into `accumulatePathUsage` with the signature above; `routing.cpp` calls it wherever the loop lived, passing the same rect/channel sequence it already builds (`guides.push_back(guidePoint(...))` at :316, the `dx/dy` usage stamps at :331-333, steps emission at :342-343 stays in routing.cpp — only WL+usage accumulation moves). The function must be a **verbatim transplant**: same iteration order, same `kEps` comparisons, same `ch.usage = std::max(h,v)` update.

- [ ] **Step 3: Verify byte-identity**

```bash
make >/dev/null
for i in 0 1 2 3 4; do
  bin/efgr "test/2026 ICCAD_case.case$i.csv" /tmp/post_case$i.cfg --seed 1 --iters 200 2>/dev/null
done
sed 's|/tmp/base_|/tmp/post_|' /tmp/baseline.md5 | md5sum -c -
```

Expected: five lines of `OK`. Any mismatch = the transplant changed behavior — fix before proceeding.

- [ ] **Step 4: Full suites**

Run: `for s in run_case00 run_placement_engine_regressions run_20260618_regressions run_20260608_regressions run_p0_regressions run_p1_regressions run_p2_regressions run_p3_regressions; do bash test/$s.sh || exit 1; done`
Expected: all green.

- [ ] **Step 5: Commit**

```bash
git add src/route_geometry.hpp src/routing.cpp
git commit -m "refactor(routing): extract shared route-geometry accounting helpers"
```

---

### Task 2: Golden rescorer — `.cfg` reader + official cost, locked to the alpha package

**Files:**
- Create: `src/cfg_score.hpp`, `src/cfg_score.cpp` (library, **no main**)
- Create: `tools/rescore.cpp` (CLI), `scripts/benchmark.sh`
- Test: `test/regression_cfg_score.cpp`, `test/run_cfg_score_regressions.sh`

**Interfaces:**
- Consumes: `parseProblem` (io.hpp); `edgeFromTo` / `guidePoint` / `accumulatePathUsage` (route_geometry.hpp, Task 1); `Problem`, `Rect`, `Channel` (model.hpp).
- Produces:

```cpp
namespace efgr {
struct CfgSolution {
    double outline_w = 0.0, outline_h = 0.0;
    std::map<std::string, Rect> blocks;      // BLOCK section, by name
    std::vector<Channel> channels;           // CHANNEL section (name + rect)
    struct Path { double nets; std::vector<std::pair<std::string,int>> steps; };  // (object, edge)
    std::vector<Path> paths;                 // PATH section
};
CfgSolution parseCfg(const std::string& path);

struct OfficialBreakdown {
    double outline_area = 0.0, wirelength = 0.0;
    double overflow_nets = 0.0, capacity_nets = 0.0, rate = 0.0;
    double overflow_penalty = 0.0, ft_penalty = 0.0;
    double checker_cost = 0.0;               // uses problem.alpha (CSV α)
    std::map<std::string, std::pair<double,double>> demand_by_channel;  // name -> (H, V)
};
OfficialBreakdown officialScoreCfg(const Problem& problem, const CfgSolution& cfg);
}
```

**Format facts for `parseCfg`:** sections are `OUTLINE w h` … `END`, `BLOCK` name x y w h lines … `END`, `CHANNEL` name x y w h … `END`, `PATH` lines `PATH <nets> <obj> <edge> <obj> <edge> …` … `END`. In a PATH line each intermediate channel appears **twice** (entry edge, exit edge) — e.g. `PATH 700 BLK06 1 CH8 3 CH8 1 CH6 3 CH6 1 BLK02 3`; the node sequence for scoring is the deduplicated object sequence `BLK06, CH8, CH6, BLK02`. A direct-abutment path is `PATH n BLKa e BLKb e` (zero WL, no channel). Files may carry `\r\n` — `trim` every token.

**Scoring rule:** rebuild `node_rects` from cfg blocks/channels by name; guides via `guidePoint` chain; WL + per-channel H/V demand via `accumulatePathUsage`; then `overflow_nets = Σ_ch [max(0, H_ch − 25·h_ch) + max(0, V_ch − 25·w_ch)]`, `capacity_nets = Σ_ch [25·h_ch + 25·w_ch]`, `R = overflow/capacity`, `overflow_penalty = 20·A·R + 1.5·A·max(0, R − 0.05)`, `checker_cost = A + α·WL + overflow_penalty` (A = `outline_w·outline_h` from the cfg OUTLINE line; ft_penalty = 0 for the alpha package — no FT was used).

- [ ] **Step 1: Write the failing golden regression**

Create `test/regression_cfg_score.cpp`. Golden values come from the official `report.json`s; the test reads them at runtime with a minimal key-scanner (the files are fixed assets — full JSON parsing is not needed):

```cpp
#include <cassert>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include "../src/cfg_score.hpp"
#include "../src/io.hpp"

using namespace efgr;

static double jsonNumber(const std::string& text, const std::string& key) {
    auto pos = text.find(key);
    assert(pos != std::string::npos);
    pos = text.find(':', pos);
    return std::stod(text.substr(pos + 1));
}

static std::string slurp(const std::string& p) {
    std::ifstream f(p);
    assert(f);
    std::stringstream ss; ss << f.rdbuf(); return ss.str();
}

int main() {
    const char* base = "docs/official/alpha_test/cade0002";
    const double kTolExact = 1e-6;   // relative
    for (int i = 0; i < 5; ++i) {
        std::string dir = std::string(base) + "/case" + std::to_string(i);
        Problem problem = parseProblem("test/2026 ICCAD_case.case" + std::to_string(i) + ".csv");
        CfgSolution cfg = parseCfg(dir + "/output.cfg");
        OfficialBreakdown got = officialScoreCfg(problem, cfg);

        std::string rep = slurp(dir + "/report.json");
        double wl   = jsonNumber(rep, "\"total_wirelength\"");
        double ovf  = jsonNumber(rep, "\"overflow_total\"");
        double cap  = jsonNumber(rep, "\"channel_total_capacity\"");
        double cost = jsonNumber(rep, "\"cost\"");

        auto rel = [](double a, double b) { return std::abs(a - b) / std::max(1.0, std::abs(b)); };
        std::printf("case%d WL %.3f/%.3f ovf %.3f/%.3f cap %.3f/%.3f cost %.2f/%.2f\n",
                    i, got.wirelength, wl, got.overflow_nets, ovf, got.capacity_nets, cap,
                    got.checker_cost, cost);
        assert(rel(got.wirelength, wl) < kTolExact);
        assert(rel(got.overflow_nets, ovf) < kTolExact);
        assert(rel(got.capacity_nets, cap) < kTolExact);
        if (i == 1) {
            // Known tail residual (Global Constraints): 0.5% gate until the tail form is proven.
            std::printf("case1 tail delta = %.6f\n", got.checker_cost - cost);
            assert(rel(got.checker_cost, cost) < 5e-3);
        } else {
            assert(rel(got.checker_cost, cost) < kTolExact);
        }
    }
    // Fine-grained: case0 per-channel H demand (report.json channel.demand_h_by_channel).
    {
        Problem problem = parseProblem("test/2026 ICCAD_case.case0.csv");
        CfgSolution cfg = parseCfg(std::string(base) + "/case0/output.cfg");
        OfficialBreakdown got = officialScoreCfg(problem, cfg);
        const std::pair<const char*, double> demand_h[] = {
            {"CH4", 1000.0}, {"CH6", 1000.0}, {"CH8", 500.0}, {"CH10", 5800.0}, {"CH11", 4800.0}};
        const std::pair<const char*, double> demand_v[] = {
            {"CH8", 2300.0}, {"CH10", 1400.0}, {"CH11", 200.0}};
        for (auto& [ch, want] : demand_h) assert(std::abs(got.demand_by_channel.at(ch).first - want) < 1e-6);
        for (auto& [ch, want] : demand_v) assert(std::abs(got.demand_by_channel.at(ch).second - want) < 1e-6);
    }
    std::puts("cfg_score golden regressions passed");
    return 0;
}
```

Create `test/run_cfg_score_regressions.sh`:

```bash
#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."

make >/dev/null

g++ -std=c++17 -O2 -Wall -Wextra -pedantic \
    test/regression_cfg_score.cpp \
    src/cfg_score.cpp src/io.cpp \
    -o test/cfg_score_regression_bin

./test/cfg_score_regression_bin
echo "cfg_score regressions passed"
```

- [ ] **Step 2: Run to verify it fails** — `bash test/run_cfg_score_regressions.sh` → compile error (`cfg_score.hpp` missing).

- [ ] **Step 3: Implement `src/cfg_score.{hpp,cpp}`** per the interfaces and scoring rule above. Implementation notes: dedupe consecutive identical objects when building the node sequence; look rects up by name (blocks first, then channels); a `Channel*` vector parallel to the rect vector feeds `accumulatePathUsage`; accumulate `demand_by_channel` from each channel's `horizontal_usage`/`vertical_usage` after all paths are applied.

- [ ] **Step 4: Iterate until green.** The per-channel case0 assertions localize any accounting mismatch to a specific channel and direction. If totals disagree, print per-path WL vs a hand-walk of `report.json`'s `routing_details.path_details` for the failing case.

- [ ] **Step 5: Tail falsification (case1).** Add a temporary `#ifdef TAIL_PROBE` block (or a small scratch main under `/tmp`) that, using case1's computed per-channel demands, evaluates each hypothesis and prints which one reproduces the official `overflow_ft_penalty = 8537517.705282887` (target: match to 1e-6 rel):
  - H1 (current): `20·A·R + 1.5·A·(R−0.05)` — known 0.31% short (~+80.8k missing).
  - H2 per-channel tail gated on global R>θ: `20·A·R + 1.5·A·Σ_ch max(0, R_ch − θ)·(cap_ch/capacity_nets)` where `R_ch = overflow_ch/cap_ch`.
  - H3 direction-split R: compute `R_h = overflow_h_total/capacity_h_total`, `R_v` likewise; penalty `= Σ_d [20·A·R_d + 1.5·A·max(0, R_d − θ)]` (case0 must still yield 396,298.58).
  - H4 used-channels denominator: R with `capacity_nets` summed only over channels with nonzero demand.
  - H5 tail measured in excess nets: `20·A·R + 1.5·A·(overflow_total − θ·capacity_nets)/capacity_nets · k` for k ∈ {1, β_lin/θ·…} — try only if H2–H4 all miss, and document.
  Whichever hypothesis matches: implement it in `officialScoreCfg`, tighten the case1 assertion to `kTolExact`, and record the proven form in `CONTEXT.md` (**Official Overflow Penalty** entry) and the `official-cost-function` memory. If none matches exactly, keep H1 + the 0.5% gate, document the residual in the plan-progress notes, and proceed (0.31% does not change any decision the sweep will make).

- [ ] **Step 6: Build the CLI + benchmark script.**

`tools/rescore.cpp`:

```cpp
#include <cstdio>
#include "../src/cfg_score.hpp"
#include "../src/io.hpp"

int main(int argc, char** argv) {
    if (argc < 3) { std::fprintf(stderr, "usage: rescore input.csv output.cfg\n"); return 2; }
    efgr::Problem problem = efgr::parseProblem(argv[1]);
    efgr::CfgSolution cfg = efgr::parseCfg(argv[2]);
    efgr::OfficialBreakdown b = efgr::officialScoreCfg(problem, cfg);
    // Machine-readable single line; the benchmark script derives ranking cost from these fields.
    std::printf("area=%.3f wl=%.3f overflow=%.3f capacity=%.3f rate=%.9f "
                "overflow_penalty=%.3f checker_cost=%.3f alpha=%.3f\n",
                b.outline_area, b.wirelength, b.overflow_nets, b.capacity_nets, b.rate,
                b.overflow_penalty, b.checker_cost, problem.alpha);
    return 0;
}
```

`scripts/benchmark.sh` (dual-cost matrix; ranking α table and δ=0.5 runtime penalty live ONLY here):

```bash
#!/usr/bin/env bash
# usage: scripts/benchmark.sh [extra solver args...]   e.g. scripts/benchmark.sh --iters 2000
set -euo pipefail
cd "$(dirname "$0")/.."
make >/dev/null
g++ -std=c++17 -O2 -Wall -Wextra -pedantic tools/rescore.cpp \
    src/cfg_score.cpp src/io.cpp -o bin/efgr_rescore

RANK_ALPHA=(0.3 0.15 0.1 0.1 0.1)   # alpha-test scoreboard values (benchmark-only)
printf "%-6s %-8s %12s %12s %14s %14s %8s\n" case engine checker_cost rank_cost overflow wl runtime
for engine in bstar hybrid; do
  total_checker=0; total_rank=0
  for i in 0 1 2 3 4; do
    csv="test/2026 ICCAD_case.case$i.csv"; out="/tmp/bench_${engine}_case$i.cfg"
    start=$(date +%s.%N)
    bin/efgr "$csv" "$out" --engine "$engine" "$@" 2>/dev/null
    rt=$(echo "$(date +%s.%N) - $start" | bc)
    line=$(bin/efgr_rescore "$csv" "$out")
    eval "$(echo "$line" | tr ' ' '\n' | sed 's/^/B_/')"   # B_area= B_wl= ...
    rank=$(echo "$B_area + ${RANK_ALPHA[$i]} * $B_wl + $B_overflow_penalty + 0.5 * $rt * sqrt($B_area)" | bc -l)
    printf "%-6s %-8s %12.0f %12.0f %14.3f %14.0f %7.2fs\n" \
           "case$i" "$engine" "$B_checker_cost" "$rank" "$B_overflow" "$B_wl" "$rt"
    total_checker=$(echo "$total_checker + $B_checker_cost" | bc -l)
    total_rank=$(echo "$total_rank + $rank" | bc -l)
  done
  printf "%-6s %-8s %12.0f %12.0f\n" TOTAL "$engine" "$total_checker" "$total_rank"
done
```

- [ ] **Step 7: Run everything**

Run: `bash test/run_cfg_score_regressions.sh && bash scripts/benchmark.sh --iters 200`
Expected: golden regression green; benchmark prints a 5×2 dual-cost matrix (numbers are baseline — no assertion).

- [ ] **Step 8: Commit**

```bash
git add src/cfg_score.hpp src/cfg_score.cpp tools/rescore.cpp scripts/benchmark.sh \
        test/regression_cfg_score.cpp test/run_cfg_score_regressions.sh
git commit -m "feat(eval): golden cfg rescorer locked to alpha-test official reports"
```

---

### Task 3: Restore C1 (official cost in `exactScore`) and prove no regression

**Files:**
- Modify (via `git stash pop`): `src/eval.cpp`, `src/model.hpp`, `src/route_cost.hpp`, `src/routing.cpp`
- Possibly modify: tail form per Task 2 Step 5's finding

**Interfaces:**
- Consumes: `channel_overflow_nets` / `channel_capacity_total` (added to `RoutingResult` by the stash), `kBetaLin`/`kBetaTail`/`kOverflowRateThreshold`/`kBetaFt` (added to `model.hpp` by the stash), the proven tail form from Task 2.
- Produces: `exactScore` whose penalty is `kBetaLin·A·R + kBetaTail·A·max(0,R−θ) + kBetaFt·feedthrough_mismatch + 1e15·open_routes` — the same formula `officialScoreCfg` computes, so solver-reported score components and the rescorer agree on identical solutions.

- [ ] **Step 1: Baseline benchmark BEFORE popping**

Run: `bash scripts/benchmark.sh --iters 1000 | tee /tmp/bench_pre_c1.txt`

- [ ] **Step 2: Pop the stash (guarded)**

```bash
git stash list   # must still show stash@{0}: WIP on fable5: 6d2de22
git status --short -- src/   # must be empty
git stash pop
git diff --stat  # expect: src/eval.cpp src/model.hpp src/route_cost.hpp src/routing.cpp
```

If the stash is missing or `src/` is dirty: STOP, report to the user (another session owns the state).

- [ ] **Step 3: Reconcile with Task 1/2.** The stash predates Task 1's extraction — expect a conflict in `routing.cpp` (stash adds `channel_capacity_total` accumulation near the overflow loop at :408-425). Resolve keeping BOTH: the extraction structure and the stash's nets-denominated `channel_overflow_nets`/`channel_capacity_total` fields. If Task 2 proved a tail form ≠ H1, port it into `routeAndScoreAt` now. Reviewer focus: `route_cost.hpp`'s marginal-cost weights must price in the SAME currency the new `exactScore` charges (the stash edits both — verify the router's `channelMarginalCost`/`softFtMarginalCost` callers still compare like units; this is the P2 §7 router-scorer consistency invariant).

- [ ] **Step 4: Build + full suites**

Run: `make` then all 8 suite scripts (Global Constraints list), plus `bash test/run_testdata_regressions.sh && bash test/run_cfg_score_regressions.sh`
Expected: warning-clean build; all green. Suites that assert absolute internal score values may need updating — legitimate (the objective changed); each such edit must be listed in the commit message.

- [ ] **Step 5: Cross-check solver vs rescorer on live output**

```bash
bin/efgr "test/2026 ICCAD_case.case0.csv" /tmp/c1_case0.cfg --seed 1 --iters 1000 2>/tmp/c1_case0.log
bin/efgr_rescore "test/2026 ICCAD_case.case0.csv" /tmp/c1_case0.cfg
grep -o "score=[0-9.e+]*" /tmp/c1_case0.log
```

Expected: solver `score` ≈ rescorer `checker_cost` (small drift from FT/edge terms is explainable; overflow penalty component must match to float noise — print both if in doubt).

- [ ] **Step 6: AFTER benchmark + regression gate**

Run: `bash scripts/benchmark.sh --iters 1000 | tee /tmp/bench_post_c1.txt` then compare:
- `checker_cost` TOTAL (both engines): post ≤ pre required. Expect a **large** drop — pre-C1 the SA sacrificed area/WL to phantom overflow (the alpha data shows the old internal penalty overweighted overflow ~2000×).
- open_routes must stay 0 on all 10 rows (the solver exit code enforces it — any nonzero exits 1 and the script aborts).
- If TOTAL regresses: investigate (most likely `route_cost.hpp` currency mismatch). If unresolvable within the day → **risk fuse**: `git stash` the C1 changes again, record in progress.md, continue to Task 5 with the 1e6 mapping.

- [ ] **Step 7: Codex adversarial review (high-stakes scoring change)**

Dispatch per CLAUDE.md: `node "$CLAUDE_PLUGIN_ROOT/scripts/codex-companion.mjs" adversarial-review --background` from the WSL shell, scoped to the C1 diff + `cfg_score.cpp`. Address Critical/Important findings before commit.

- [ ] **Step 8: Commit**

```bash
git add src/eval.cpp src/model.hpp src/route_cost.hpp src/routing.cpp
git commit -m "feat(eval): official checker cost mapping in exactScore (C1)"
```

Also: update the `official-cost-function` memory (C1 is now COMMITTED, not stashed; record the proven tail form) — the orchestrator does this, not a subagent.

---

### Task 4: Integer decode (QA A43) — may run in parallel with Task 3 (file-disjoint except tests)

**Files:**
- Modify: `src/placement_engine.cpp` (`dimensionFor` at :10-14, edge anchoring in `placeEdge`/`legalizeEdgeBlocks`, `shrinkWrapOutline`/`anchorAtOutline` outline stamping)
- Test: `test/regression_integer_output.cpp`, `test/run_integer_output_regressions.sh`

**Interfaces:**
- Consumes: `Block` (area, ar_min/ar_max, ft_base_area), `Rect`, `Placement`.
- Produces: every decoded `Rect` has integer `x, y, w, h`; every stamped `outline_w/h` integer; `writeCfg` output therefore all-integer with no code change there (`setprecision(3)` prints `640.000` — acceptable; optionally switch to integer printing, see Step 5).

**Integerization rules (contest-derived):**
1. Soft block, base sizing: for clamped target aspect `ar`, find integer `(w, h)` maximizing `w·h ≤ area` with `ar_min ≤ w/h ≤ ar_max`. Search: for `h` in `[max(1, floor(h0)−3), ceil(h0)+3]` where `h0 = sqrt(area/ar)`, take `w = floor(area/h)` clamped so `w/h` stays in `[ar_min, ar_max]`; keep the best `w·h`, tie-break on `|w/h − ar|`. (A43.3: undersize when equality impossible; the SA's aspect variable keeps exploring shapes, so a local integer optimum per aspect is sufficient.)
2. Soft block, FT-inflated re-decode: minimal integer `w·h ≥ required_area` within the AR range (same window search with `w = ceil(required_area/h)`), so no feedthrough-mismatch is charged for rounding.
3. Macro/Edge: dims are already integers in the 20260712 CSVs — assert, don't round.
4. Positions: decode produces `x, y` as sums of integer dims (contour/longest-path) — integer by construction once dims are integers; edge-block anchors computed from outline fractions (thirds of 2917 are fractional) must be `std::llround`ed then clamped so the block stays inside `[0, outline]` and keeps its required LOCATION overlap (1D check, QA A20 — a 1um shift cannot break an overlap that was ≥ 1um; add the assert).
5. Outline stamps: `shrinkWrapOutline`/`anchorAtOutline` results `ceil`ed to integers, clamped to MAX (MAX is integer).

- [ ] **Step 1: Write the failing regression**

`test/regression_integer_output.cpp`:

```cpp
#include <cassert>
#include <cmath>
#include <cstdio>
#include <memory>
#include "../src/io.hpp"
#include "../src/placement_engine.hpp"
#include "../src/search.hpp"

using namespace efgr;

static bool isInt(double v) { return std::abs(v - std::llround(v)) < 1e-9; }

int main() {
    for (int c = 0; c < 5; ++c) {
        Problem problem = parseProblem("test/2026 ICCAD_case.case" + std::to_string(c) + ".csv");
        SearchOptions opt; opt.iters = 200; opt.seed = 1;
        std::unique_ptr<PlacementEngine> engine = makePlacementEngine("bstar");
        SearchResult r = runPlacementSearch(problem, *engine, opt);
        assert(r.best_routing.open_routes == 0);
        for (const Rect& rect : r.best_placement.rects) {
            assert(isInt(rect.x) && isInt(rect.y) && isInt(rect.w) && isInt(rect.h));
        }
        if (r.best_placement.outline_w > 0.0) {
            assert(isInt(r.best_placement.outline_w) && isInt(r.best_placement.outline_h));
        }
        // Soft blocks must not exceed their base area unless FT-inflated (rule 1 vs rule 2).
        for (std::size_t i = 0; i < problem.blocks.size(); ++i) {
            const Block& b = problem.blocks[i];
            if (!isSoft(b.type)) continue;
            double placed = r.best_placement.rects[i].w * r.best_placement.rects[i].h;
            double ft_nets = i < r.best_routing.soft_feedthrough_nets.size()
                                 ? r.best_routing.soft_feedthrough_nets[i] : 0.0;
            if (ft_nets <= 0.0) assert(placed <= b.area + 1e-6);
        }
        std::printf("case%d integer decode ok\n", c);
    }
    std::puts("integer output regressions passed");
    return 0;
}
```

Runner `test/run_integer_output_regressions.sh` — same pattern as `run_p1_regressions.sh` (link `src/routing.cpp src/eval.cpp src/io.cpp src/validate.cpp src/placement_engine.cpp src/search.cpp src/floorplan.cpp src/respace.cpp src/ft.cpp` — copy the exact list from `test/run_p1_regressions.sh` plus any files it names).

- [ ] **Step 2: Run to verify it fails** — expect the `isInt` assertions to abort on soft-block dims (e.g. case0 BLK02 is currently 640.3007×640.3007).

- [ ] **Step 3: Implement rules 1–5 in `placement_engine.cpp`.** The single choke point for dims is `dimensionFor(const Block&, double aspect)` (:10) — implement the integer window search there (rule 1) and add a required-area variant for the FT re-decode path (rule 2; find its caller by grepping `ftRequiredArea` in placement_engine.cpp/search.cpp). Anchors and outline stamps per rules 4–5.

- [ ] **Step 4: Run regression + full suites + benchmark delta**

Run: `bash test/run_integer_output_regressions.sh`, the 8 suites, then `bash scripts/benchmark.sh --iters 1000`.
Expected: all green. Benchmark `checker_cost` moves only marginally (integer quantization loses < 0.1% area utilization); open_routes stays 0. Verify the emitted cfg has no fractional values: `grep -E "\.[0-9]*[1-9]" /tmp/bench_bstar_case0.cfg | grep -v "^PATH"` → empty.

- [ ] **Step 5 (cosmetic, optional): integer printing.** In `writeCfg` (io.cpp:201), values are now integral; `%.3f` prints `640.000`. The alpha checker accepted decimals, so leaving it is safe. If changing: print with `std::llround` for OUTLINE/BLOCK/CHANNEL lines only — PATH lines contain names/edges, untouched. Re-run the cfg_score golden regression afterward (the reader must still parse both styles — it must, for the alpha-package files).

- [ ] **Step 6: Commit**

```bash
git add src/placement_engine.cpp test/regression_integer_output.cpp test/run_integer_output_regressions.sh src/io.cpp
git commit -m "feat(placement): integer-quantized decode per QA A43"
```

---

### Task 5: `--time-budget` + default-selection sweep

**Files:**
- Modify: `src/search.hpp` (SearchOptions), `src/search.cpp` (SA loop stop condition, hybrid split), `src/main.cpp` (flag + chosen defaults)
- Create: `scripts/sweep.sh`
- Test: extend none — determinism suites keep running with explicit `--iters` (budget defaults OFF in tests)

**Interfaces:**
- Consumes: `SearchOptions`, `runPlacementSearch`, `runHybridPlacementSearch`.
- Produces: `SearchOptions.time_budget_sec` (double, 0 = disabled) honored by both search entry points; `--time-budget S` CLI flag; final hardcoded beta defaults in `main.cpp` chosen by the sweep.

- [ ] **Step 1: Implement the stop condition.**

`search.hpp`: add to `SearchOptions`:

```cpp
    double time_budget_sec = 0.0;  // 0 = disabled; wall-clock stop, checked every 64 iters
```

`search.cpp`: in the SA loop of `runPlacementSearch`, capture `auto t0 = std::chrono::steady_clock::now();` before the loop; inside, every 64 iterations:

```cpp
        if (options.time_budget_sec > 0.0 && (iter & 63) == 0) {
            double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
            if (elapsed >= options.time_budget_sec) break;
        }
```

When the budget is set, the iteration bound becomes `max(options.iters, 2'000'000)` so time, not the iter default, is the binding stop. `runHybridPlacementSearch`: split the budget 50/50 between the sp and bstar legs (each leg gets `time_budget_sec * 0.5` in its own SearchOptions copy). `main.cpp`: `options.time_budget_sec = argDouble(argc, argv, "--time-budget", 0.0);` and extend the usage string.

- [ ] **Step 2: Sanity run**

Run: `time bin/efgr "test/2026 ICCAD_case.case4.csv" /tmp/tb.cfg --time-budget 10 2>&1 | tail -1`
Expected: wall time ≈ 10–11 s (one in-flight exact eval may overshoot slightly); summary line shows `iters` ≫ 1000; `validation=ok`.

- [ ] **Step 3: Determinism guard** — run `bash test/run_placement_engine_regressions.sh` (explicit `--iters`, budget off) → green, byte-identical behavior.

- [ ] **Step 4: Sweep.** `scripts/sweep.sh` loops `engine ∈ {bstar, hybrid}` × `budget ∈ {0(=iters1000), 30, 60, 90, 120}` × `seed ∈ {1, 2, 3}` over the 5 cases, reusing `benchmark.sh`'s rescore+dual-cost machinery (factor the per-run scoring into a function both scripts source, or just extend benchmark.sh with `--budget`/`--seed` env knobs — implementer's choice, keep it ≤ 80 lines). Output: one CSV row per (engine, budget, seed, case) → `/tmp/sweep_results.csv`, plus an aggregate table of `Σ checker_cost` per (engine, budget) averaged over seeds.

- [ ] **Step 5: Pick and hardcode the default.** Selection rule (user-approved: best measured score): minimize seed-averaged `Σ_case checker_cost`, tie-break lower `Σ rank_cost`, subject to: projected 8-case runtime = `(Σ_5 runtime) × 8/5 × 1.5 (unknown-case factor) ≤ 30 min`, and per-case `open_routes == 0` on every seed. Hardcode the winner in `main.cpp` (`--engine` default string and/or `time_budget_sec` default when no flag is given — keep `--iters`/`--time-budget` overridable for tests). Paste the aggregate table into the commit message.

- [ ] **Step 6: Full suites + commit**

```bash
for s in run_case00 run_placement_engine_regressions run_20260618_regressions run_20260608_regressions \
         run_p0_regressions run_p1_regressions run_p2_regressions run_p3_regressions \
         run_testdata_regressions run_cfg_score_regressions run_integer_output_regressions; do
  bash test/$s.sh || exit 1
done
git add src/search.hpp src/search.cpp src/main.cpp scripts/sweep.sh
git commit -m "feat(search): wall-clock time budget + sweep-selected beta defaults"
```

---

### Task 6: Beta packaging, final review, organizer QA draft

**Files:**
- Create: `docs/official/qa-draft-20260722.md`
- Modify: `.superpowers/sdd/progress.md` (append plan outcome)

- [ ] **Step 1: Whole-branch final review.** Dispatch code review over `6d2de22..HEAD` (all commits from this plan): one `deep-reasoner` pass + one Codex `review` (per CLAUDE.md, run Codex from the WSL native shell). Blocking findings fixed before packaging.

- [ ] **Step 2: Package.**

```bash
make clean && make
for i in 0 1 2 3 4; do
  bin/EarlyFloorplanning_with_GlobalRoute "test/2026 ICCAD_case.case$i.csv" || exit 1
done
ls -la bin/EarlyFloorplanning_with_GlobalRoute
```

Expected: exit 0 on all five (validation ok, no open routes); the binary at the contest-required name. Note: the harness invokes with only the CSV — the default output path (`<input>.cfg`, main.cpp:37-44) is the submission contract; the loop above exercises exactly that call shape. Delete the generated `test/*.cfg` afterward (gitignored, but keep the tree clean): `rm -f "test/2026 ICCAD_case.case"*.cfg`.

- [ ] **Step 3: Organizer QA draft** — write `docs/official/qa-draft-20260722.md` (Traditional Chinese) with three questions, each with the evidence one line each: (1) α 矛盾：A35.5 說以 testcase 檔為準（五案皆 0.2），但 alpha 排行榜 `case_result.json` 使用 per-case α 0.3/0.15/0.1/0.1/0.1 並含 `runtime_penalty`（δ=0.5）—— 正式評分以何者為準？(2) case3 對角自連接（BLK09→BLK09=63、BLK13→BLK13=123）：A44 已承諾修正，但 20260712 測資仍含兩筆 —— 是否會再更新？(3) A38 承諾的 3 筆新測資（ETA 7/15）尚未見公布 —— 預計何時提供？

- [ ] **Step 4: Close out.** Append to `.superpowers/sdd/progress.md`: plan name, per-task commits, sweep-chosen default (engine/budget), before/after benchmark totals, tail-form finding, risk-fuse status. Update the `official-cost-function` memory accordingly. Push via `scripts/update.sh` only if the user asks.

---

## Execution notes

- **Critical path:** T0 → T1 → T2 → T3 → T5 → T6, with T4 parallel to T3 (different files; both touch tests only additively). Day 1 (today): T0–T3. Day 2: T4 finish, T5 sweep (the 90 sweep runs at ≤ 120 s each dominate — start it early), T6.
- **Delegation (CLAUDE.md orchestration):** T0, T1, T4 → `fast-worker` (mechanical, specs above are complete). T2, T3 → `deep-reasoner` implements with the plan + Codex adversarial review on T3. T5 sweep runs → `fast-worker`; default selection → orchestrator. T6 review → deep-reasoner + Codex; QA draft → orchestrator.
- **Parallel-session caution:** the C1 stash and the vanished `test/run_*.sh` modifications from the session-start snapshot suggest another session touched this tree today. Before T3's pop and before any `git add`, re-check `git status` matches expectations; on surprise, stop and surface to the user.
