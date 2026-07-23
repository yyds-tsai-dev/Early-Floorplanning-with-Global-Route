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

