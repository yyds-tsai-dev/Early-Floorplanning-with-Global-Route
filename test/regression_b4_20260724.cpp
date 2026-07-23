// B4.1 (route-pricing portfolio, coordinator adjudication 2026-07-24, .superpowers/sdd/b4-report.md):
// regression coverage for runPricingPortfolio (search.hpp/.cpp). The load-bearing correctness
// property -- route_pricing_official=false reproduces pre-B4 (HEAD 207d08c) routing behavior
// BYTE-IDENTICALLY -- was verified manually (scratch tool + diff against the pre-B4 build's 5
// default-flag .cfg outputs, all byte-identical; see b4-report.md) and is not re-encoded here as an
// automated test (byte-identity of a whole .cfg write is exactly what run_selfconsistency_
// regressions.sh / run_cfg_score_regressions.sh already exercise the machinery for, and pinning a
// full .cfg's bytes as a literal constant would be an unmaintainable, brittle duplicate). What IS
// pinned here is runPricingPortfolio's own end-to-end CONTRACT: the fold restores the per-case
// <=-pre-B4 Pareto floor (case3, where the legacy leg wins) and correctly picks the strictly better
// leg when one dominates (case1, where the official leg wins by a wide margin) -- the two concrete
// fold outcomes the coordinator's brief names.
#include <cassert>
#include <cstdio>
#include <iostream>
#include <string>

#include "../src/cfg_score.hpp"
#include "../src/io.hpp"
#include "../src/model.hpp"
#include "../src/search.hpp"

namespace {
using namespace efgr;

double rescoreDefaultRun(const Problem& problem, const SearchResult& result, const std::string& tmp_path) {
    writeCfg(tmp_path, problem, result.best_placement, result.best_routing);
    CfgSolution cfg = parseCfg(tmp_path);
    OfficialBreakdown breakdown = officialScoreCfg(problem, cfg);
    std::remove(tmp_path.c_str());
    return breakdown.checker_cost;
}

// Pre-B4 (HEAD 207d08c) default-flag golden-rescored checker_cost, case3 -- matches b31-report.md's
// own row exactly (B4 did not touch this baseline; independently re-measured in b4-report.md via
// git-stash/rebuild before B4 landed). This is the value case3's B4-ALONE run regressed past
// (+7.46%, a proven search-trajectory effect -- see b4-report.md's replay diagnostic); the whole
// point of B4.1 is that the PORTFOLIO must never exceed it, because its legacy leg reproduces this
// exact pre-B4 trajectory bit-for-bit.
constexpr double kPreB4Case3DefaultCheckerCost = 36615637.236;

// The B4-alone (official-pricing-only) checker_cost this SAME case regressed to, named here purely
// so the test's own comment is self-documenting about what the portfolio is recovering from -- not
// asserted directly (asserting an upper bound near a KNOWN-bad number would be a weak test; the
// meaningful assertion is against the pre-B4 floor above).
constexpr double kB4AloneCase3DefaultCheckerCost = 39346073.615;

void testPricingPortfolioRestoresCase3PreB4Floor() {
    Problem problem = parseProblem("test/2026 ICCAD_case.case3.csv");
    SearchOptions options;  // defaults: iters=1000, seed=1, exact_interval=50, budget off
    SearchResult result = runPricingPortfolio(problem, "hybrid", options);
    assert(result.best_routing.open_routes == 0);

    double checker_cost = rescoreDefaultRun(problem, result, "test/b4_case3_tmp.cfg");

    if (checker_cost > kPreB4Case3DefaultCheckerCost + 1.0) {
        std::cerr << "testPricingPortfolioRestoresCase3PreB4Floor: checker_cost=" << checker_cost
                  << " exceeds pre-B4 floor " << kPreB4Case3DefaultCheckerCost
                  << " (B4-alone had regressed to " << kB4AloneCase3DefaultCheckerCost << ")\n";
    }
    assert(checker_cost <= kPreB4Case3DefaultCheckerCost + 1.0);
    std::cout << "testPricingPortfolioRestoresCase3PreB4Floor ok (checker_cost=" << checker_cost
              << " <= pre-B4 floor " << kPreB4Case3DefaultCheckerCost
              << ", pricing_official=" << result.pricing_official << ")\n";
}

// case1's B4-alone (official) improvement over pre-B4 (legacy) is decisive (-31.30%, 13,246,805.65
// -> 9,100,697.98 per b4-report.md's sweep) -- the fold MUST pick the official leg here, not merely
// tie or pick arbitrarily. Asserts both halves of the contract: the telemetry field says official
// won, AND the actual golden-rescored result matches the (much better) official/B4-alone number,
// not the (much worse) legacy/pre-B4 one -- ruling out a bug where the telemetry is set but the
// wrong RoutingResult/Placement/Score triple gets returned.
constexpr double kB4AloneCase1DefaultCheckerCost = 9100697.98;
constexpr double kPreB4Case1DefaultCheckerCost = 13246805.65;

void testPricingPortfolioPicksOfficialLegOnCase1() {
    Problem problem = parseProblem("test/2026 ICCAD_case.case1.csv");
    SearchOptions options;  // defaults: iters=1000, seed=1, exact_interval=50, budget off
    SearchResult result = runPricingPortfolio(problem, "hybrid", options);
    assert(result.best_routing.open_routes == 0);
    assert(result.pricing_official);  // the official leg must win this fold

    double checker_cost = rescoreDefaultRun(problem, result, "test/b4_case1_tmp.cfg");

    // Must land at (or below) the official/B4-alone number, and strictly below the midpoint between
    // official and legacy -- a weak/buggy fold that accidentally returned the legacy leg's own
    // result while merely mislabeling pricing_official would fail this numeric check even if the
    // boolean assertion above were (wrongly) satisfied by chance.
    double midpoint = 0.5 * (kB4AloneCase1DefaultCheckerCost + kPreB4Case1DefaultCheckerCost);
    if (checker_cost > kB4AloneCase1DefaultCheckerCost + 1.0 || checker_cost >= midpoint) {
        std::cerr << "testPricingPortfolioPicksOfficialLegOnCase1: checker_cost=" << checker_cost
                  << " official=" << kB4AloneCase1DefaultCheckerCost
                  << " legacy=" << kPreB4Case1DefaultCheckerCost << "\n";
    }
    assert(checker_cost <= kB4AloneCase1DefaultCheckerCost + 1.0);
    assert(checker_cost < midpoint);
    std::cout << "testPricingPortfolioPicksOfficialLegOnCase1 ok (checker_cost=" << checker_cost
              << ", pricing_official=" << result.pricing_official << ")\n";
}

}  // namespace

int main() {
    testPricingPortfolioRestoresCase3PreB4Floor();
    testPricingPortfolioPicksOfficialLegOnCase1();
    std::cout << "b4.1 regressions passed\n";
    return 0;
}
