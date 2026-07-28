// Golden regression for the V5 .cfg re-scorer (Cost Function V5; see docs/research/
// 2026-07-28-alpha-v5-cost-recalculation-analysis.md). report.json is the OLD (pre-V5) checker's
// output and is NO LONGER the oracle for wirelength / overflow / penalty / checker_cost -- those
// are hardcoded V5 golden values below, derived from tools/verify_v5_alpha.py applied to each
// case's FULL path set and cross-validated EXACT against the official "PATH Detail Cost
// Calculation" page (docs/official/alpha_test/cade0002_calculation.html, Cost Function V5).
// report.json is still read for the two things the V5 port left unchanged: channel_total_capacity
// (the 25 nets/um capacity rule didn't change) for all 5 cases, and case1's channel_overflow_rate +
// overflow_ft_penalty, used only as the historical R>threshold datapoint for the standalone
// overflow-tail-formula identity test (the tail formula itself is unchanged by V5; only the WL and
// demand accounting changed).
#include <cassert>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <utility>

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
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

static double rel(double a, double b) { return std::abs(a - b) / std::max(1.0, std::abs(b)); }

// Finds the path matching an official example declaration by nets + first/last object (see
// kAnchors below). Asserts the match is unique -- true for all 5 cases (verified separately).
static const CfgSolution::Path* findPath(const CfgSolution& cfg, double nets, const std::string& first,
                                          const std::string& last) {
    const CfgSolution::Path* found = nullptr;
    for (const auto& p : cfg.paths) {
        if (std::abs(p.nets - nets) > 1e-9 || p.steps.empty()) continue;
        if (p.steps.front().first != first || p.steps.back().first != last) continue;
        assert(found == nullptr);  // must be the unique match
        found = &p;
    }
    return found;
}

struct CaseGolden {
    double wirelength;
    double overflow;
    double penalty;
    double checker_cost;
};

// V5 golden values (wirelength / overflow_nets / overflow_penalty / checker_cost), one row per
// docs/official/alpha_test/cade0002/caseN/output.cfg. Derived from tools/verify_v5_alpha.py's V5
// WL-chain + single-charge-per-axis demand + piecewise tail, applied to each case's FULL path set;
// cross-validated EXACT against the official V5 calculation page's per-case overflow/capacity
// totals and the five example-path WL subtotals (kAnchors below). checker_cost uses each case's
// CSV alpha (0.2, identical across all 5 cases -- NOT the official ranking-cost alpha table, which
// is 0.3/0.15/0.1/0.1/0.1 and unrelated to this oracle's contract).
static const CaseGolden kGolden[5] = {
    /* case0 */ {5373447.750000, 469.225, 191857.619370, 7742287.169370},
    /* case1 */ {46726264.090500, 3375.275, 1064192.152785, 18220433.370885},
    /* case2 */ {246461723.828500, 18647.650, 7005239.565245, 98297584.330945},
    /* case3 */ {125992806.142500, 12764.975, 4244404.357742, 52858669.586242},
    /* case4 */ {380230368.589500, 19662.175, 4072088.791921, 117900106.509821},
};

struct WlAnchor {
    double nets;
    const char* first;
    const char* last;
    double subtotal;
};

// Official V5 WL anchors: the ONLY official V5 WL numbers, one example path per case straight off
// the official calculation page. Each matches exactly one path in its case's cfg (by nets + first/
// last object of the PATH declaration).
static const WlAnchor kAnchors[5] = {
    /* case0 */ {500, "BLK02", "BLK05", 1175.56850},
    /* case1 */ {1, "BLK01", "BLK05", 1490.57750},
    /* case2 */ {834, "BLK01", "BLK04", 8606.18400},
    /* case3 */ {25, "BLK01", "BLK04", 4225.04700},
    /* case4 */ {191, "BLK03", "BLK19", 8085.23100},
};

int main() {
    const char* base = "docs/official/alpha_test/cade0002";
    const double kTolRel = 1e-6;  // wirelength / penalty / checker_cost (exact halves; safe)
    const double kTolAbs = 1e-6;  // overflow_nets (absolute)

    for (int i = 0; i < 5; ++i) {
        std::string dir = std::string(base) + "/case" + std::to_string(i);
        Problem problem = parseProblem("test/2026 ICCAD_case.case" + std::to_string(i) + ".csv");
        CfgSolution cfg = parseCfg(dir + "/output.cfg");
        OfficialBreakdown got = officialScoreCfg(problem, cfg);

        std::string rep = slurp(dir + "/report.json");
        double cap = jsonNumber(rep, "\"channel_total_capacity\"");  // capacity rule unchanged by V5

        const CaseGolden& want = kGolden[i];
        std::printf("case%d WL %.6f/%.6f ovf %.6f/%.6f cap %.6f/%.6f pen %.6f/%.6f cost %.6f/%.6f\n",
                    i, got.wirelength, want.wirelength, got.overflow_nets, want.overflow,
                    got.capacity_nets, cap, got.overflow_penalty, want.penalty, got.checker_cost,
                    want.checker_cost);

        // All 5 cases sit in the linear overflow region under V5 -- no special-casing case1.
        assert(rel(got.wirelength, want.wirelength) < kTolRel);
        assert(std::abs(got.overflow_nets - want.overflow) < kTolAbs);
        assert(rel(got.capacity_nets, cap) < kTolRel);
        assert(rel(got.overflow_penalty, want.penalty) < kTolRel);
        assert(rel(got.checker_cost, want.checker_cost) < kTolRel);
        // V5 FT penalty (analysis doc section 1.4): all 5 alpha cases carry no feedthrough, so the
        // aggregate FT_area_sum never exceeds outline_area and ft_penalty is exactly 0 -- documents
        // the zero-FT coverage of this golden set (the goldens above are otherwise unaffected).
        assert(got.ft_penalty == 0.0);

        // Official V5 WL anchor: re-score a copy of cfg containing ONLY the official example path.
        const WlAnchor& anchor = kAnchors[i];
        const CfgSolution::Path* match = findPath(cfg, anchor.nets, anchor.first, anchor.last);
        assert(match != nullptr);
        CfgSolution single = cfg;
        single.paths.assign(1, *match);
        OfficialBreakdown anchor_got = officialScoreCfg(problem, single);
        double want_wl = anchor.nets * anchor.subtotal;
        std::printf("case%d anchor WL %.5f/%.5f (nets=%.0f %s..%s)\n", i, anchor_got.wirelength,
                    want_wl, anchor.nets, anchor.first, anchor.last);
        assert(rel(anchor_got.wirelength, want_wl) < 1e-9);
    }

    // Tail identity: officialOverflowPenalty reproduces the historical R>threshold datapoint --
    // case1's OLD (pre-V5, per-edge-demand) overflow rate and penalty. The tail formula itself is
    // UNCHANGED by V5 (only the WL and demand accounting changed), so it must still reproduce this
    // old datapoint to the last digit (docs/research/2026-07-28-alpha-v5-cost-recalculation-
    // analysis.md, section 1.3).
    {
        std::string rep1 = slurp(std::string(base) + "/case1/report.json");
        double r_old = jsonNumber(rep1, "\"channel_overflow_rate\"");      // 0.05384513657066979
        double want_penalty = jsonNumber(rep1, "\"overflow_ft_penalty\""); // 8537517.705282887
        const double kOutlineAreaCase1 = 7810988.4;  // case1 outline_area (report.json cost_breakdown)
        double got = officialOverflowPenalty(r_old, kOutlineAreaCase1);
        std::printf("tail identity: R=%.11f formula=%.6f official=%.6f\n", r_old, got, want_penalty);
        assert(rel(got, want_penalty) < 1e-9);
    }

    // Fine-grained: case0 per-channel V5 H/V demand (single-charge-per-axis-per-traversal).
    {
        Problem problem = parseProblem("test/2026 ICCAD_case.case0.csv");
        CfgSolution cfg = parseCfg(std::string(base) + "/case0/output.cfg");
        OfficialBreakdown got = officialScoreCfg(problem, cfg);
        const std::pair<const char*, double> demand_h[] = {
            {"CH4", 500.0}, {"CH6", 500.0}, {"CH8", 500.0}, {"CH10", 3600.0}, {"CH11", 2500.0}};
        const std::pair<const char*, double> demand_v[] = {
            {"CH8", 1400.0}, {"CH10", 1400.0}, {"CH11", 200.0}};
        for (auto& [ch, want] : demand_h) assert(std::abs(got.demand_by_channel.at(ch).first - want) < 1e-6);
        for (auto& [ch, want] : demand_v) assert(std::abs(got.demand_by_channel.at(ch).second - want) < 1e-6);
    }

    std::puts("cfg_score golden regressions passed");
    return 0;
}
