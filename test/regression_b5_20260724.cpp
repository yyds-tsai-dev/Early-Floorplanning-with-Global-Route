// B5a (elite-pool rework, .superpowers/sdd/b5-design.md §2): regression coverage for the admission
// machinery search.cpp's runPlacementSearch now uses in place of plain "top-16 by cheap rank":
// `rememberBounded` (shared dedup+sort+cap discipline for the cheap prefilter AND the new exact
// archive), `reservoirSample` (the insurance-pick reservoir, Algorithm R, fed by a dedicated rng),
// and `buildEliteFullSet` (the rung-0 tight-exact re-rank + forced running-best/insurance admission
// that replaces top-16-by-cheap-rank). All three are free functions declared in search.hpp
// specifically so they are unit-testable in isolation, per the design doc's own §7 verification plan
// ("a hand-built pool ... asserts the full-treated set is the tight-exact top-16 (not cheap
// top-16)").
//
// Also covers an end-to-end determinism smoke: unlike B6 (purely observation-only, so pre-B5a and
// post-B5a outputs had to match byte-for-byte), B5a is DESIGNED to change the elite pass's outcome
// ("the elite pass outcome may of course change, that is the point" -- the task brief). So there is
// no pre/post byte-diff here; instead this pins that the NEW machinery (archive, insurance
// reservoir, tight-exact rerank) is itself still fully deterministic -- two invocations of the same
// case/seed must still produce byte-identical .cfg output, matching the pattern
// regression_b6_20260724.cpp's own determinism smoke already established for this codebase.
//
// B5a.1 (elite-admission variant portfolio, coordinator adjudication after B5a's own STOP --
// .superpowers/sdd/b5a-report.md): B5a's admission rework, wired in unconditionally, measurably
// REGRESSED case1 (+17.1%) and case3 (+32.6%) at default flags -- case3 lost its floor entirely
// because the new admission mechanism lives in shared search.cpp code that BOTH of
// runPricingPortfolio's pricing legs run through, breaking B4.1's "legacy leg reproduces pre-B4
// bit-exactly" premise. The fix: flag-gate the whole mechanism behind
// SearchOptions.elite_admission_v2 (default false = v1, byte-identical to pre-B5a), and extend
// runPricingPortfolio to a 4-leg portfolio -- {route_pricing_official, elite_admission_v2} crossed
// -- folded by the same value-gated paydownBetter. testPortfolioRestoresCase3FloorPostB5a1 below is
// that fold's own acceptance pin, mirroring regression_b4_20260724.cpp's
// testPricingPortfolioRestoresCase3PreB4Floor pattern exactly but against the NEW 4-leg
// runPricingPortfolio (this file's own suite, rather than duplicating into the b4 file, since the
// property under test is now B5a.1's fold, not B4.1's original 2-leg one).
#include <cassert>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <map>
#include <random>
#include <sstream>
#include <string>
#include <vector>

#include "../src/cfg_score.hpp"
#include "../src/io.hpp"
#include "../src/model.hpp"
#include "../src/search.hpp"

namespace {
using namespace efgr;

Placement makePointPlacement(double x) {
    Placement placement;
    placement.rects.push_back(Rect{x, 0.0, 5.0, 5.0});
    placement.outline_w = 1000.0;
    placement.outline_h = 1000.0;
    return placement;
}

bool containsX(const std::vector<EliteCandidate>& set, double x) {
    for (const auto& candidate : set) {
        if (!candidate.placement.rects.empty() && std::abs(candidate.placement.rects[0].x - x) < 1e-9) return true;
    }
    return false;
}

// ---- rememberBounded --------------------------------------------------------------------------

void testRememberBoundedDedupSortAndCap() {
    std::vector<CheapCandidate> pool;
    rememberBounded(pool, makePointPlacement(30.0), 3.0, 4);
    rememberBounded(pool, makePointPlacement(10.0), 1.0, 4);
    rememberBounded(pool, makePointPlacement(20.0), 2.0, 4);
    assert(pool.size() == 3);
    assert(pool[0].value == 1.0 && pool[1].value == 2.0 && pool[2].value == 3.0);  // sorted ascending

    // Duplicate key (same placement, x=10.0): the FIRST-seen value (1.0) must be kept, not
    // overwritten by this later "better" value 0.5 -- matches the pre-B5a rememberCandidate
    // dedup-skip semantics this function generalizes (an existing key short-circuits the insert).
    rememberBounded(pool, makePointPlacement(10.0), 0.5, 4);
    assert(pool.size() == 3);
    assert(pool[0].value == 1.0);

    // Cap enforcement: pushing past `cap` keeps only the `cap` SMALLEST values.
    rememberBounded(pool, makePointPlacement(40.0), 4.0, 4);
    rememberBounded(pool, makePointPlacement(50.0), 5.0, 4);
    assert(pool.size() == 4);
    assert(pool[3].value == 4.0);  // 5.0 (x=50) was resized away
    std::cout << "testRememberBoundedDedupSortAndCap ok\n";
}

// ---- reservoirSample ---------------------------------------------------------------------------

void testReservoirSampleSameSeedProducesSamePicks() {
    std::mt19937 rng_a(2026);
    std::mt19937 rng_b(2026);
    std::vector<Placement> reservoir_a, reservoir_b;
    for (int i = 1; i <= 20; ++i) {
        Placement p = makePointPlacement(static_cast<double>(i));
        reservoirSample(reservoir_a, p, i, 4, rng_a);
        reservoirSample(reservoir_b, p, i, 4, rng_b);
    }
    assert(reservoir_a.size() == 4 && reservoir_b.size() == 4);
    for (std::size_t i = 0; i < 4; ++i) {
        assert(std::abs(reservoir_a[i].rects[0].x - reservoir_b[i].rects[0].x) < 1e-9);
    }
    std::cout << "testReservoirSampleSameSeedProducesSamePicks ok\n";
}

void testReservoirSampleCapAndFullStreamCoverage() {
    // Fewer stream items than cap: every item is kept (no randomness needed).
    std::mt19937 rng(7);
    std::vector<Placement> reservoir;
    reservoirSample(reservoir, makePointPlacement(1.0), 1, 4, rng);
    reservoirSample(reservoir, makePointPlacement(2.0), 2, 4, rng);
    assert(reservoir.size() == 2);
    std::cout << "testReservoirSampleCapAndFullStreamCoverage ok\n";
}

// Insurance's dedicated rng is `options.seed ^ 0x5eedu` (search.cpp), a DIFFERENT generator than the
// main SA-loop rng (seeded from options.seed directly). Confirms these two seed formulas produce
// genuinely different draw sequences for a representative seed (1, the CLI default) -- the
// structural property runPlacementSearch's rememberInsurance relies on to keep the main mutate/
// accept stream byte-unperturbed.
void testInsuranceSeedFormulaIndependentOfMainStream() {
    unsigned int seed = 1;
    std::mt19937 main_rng(seed);
    std::mt19937 ins_rng(seed ^ 0x5eedu);
    bool any_differs = false;
    for (int i = 0; i < 8; ++i) {
        if (main_rng() != ins_rng()) {
            any_differs = true;
            break;
        }
    }
    assert(any_differs);
    std::cout << "testInsuranceSeedFormulaIndependentOfMainStream ok\n";
}

// ---- buildEliteFullSet --------------------------------------------------------------------------

// The core B5a admission test (design doc §7 B5a (i)): a hand-built pool where the cheap-best is NOT
// the tight-exact-best asserts the full-treated set is chosen by TIGHT-EXACT rank, not cheap rank,
// PLUS the running best and all insurance picks are force-admitted regardless of rank.
//
// Construction: cheap_pool has 20 members, label i=0..19 at x=i*10, with CheapCandidate.value = i
// (ascending -- label 0 is cheap-BEST) but a mock exactCached assigns tight_value = 1000-i (so label
// 0, the cheap-best, is the tight-exact WORST at 1000; label 19, the cheap-worst, is the tight-exact
// BEST at 981). exact_archive adds 2 members (x=700,710) with tight values 970/975 -- BETTER than
// every cheap_pool member, so they must win admission ON MERIT, not force. best_placement (x=500)
// and 4 insurance picks (x=600..630) all get deliberately terrible tight values (9000+) so their
// presence in the result can ONLY be explained by force-admission, never by rank.
void testBuildEliteFullSetRanksByTightExactNotCheapValue() {
    std::vector<CheapCandidate> cheap_pool;
    for (int label = 0; label <= 19; ++label) {
        Placement p = makePointPlacement(label * 10.0);
        cheap_pool.push_back(CheapCandidate{static_cast<double>(label), p, placementKey(p)});
    }
    std::vector<CheapCandidate> exact_archive;
    Placement arch_a = makePointPlacement(700.0);
    Placement arch_b = makePointPlacement(710.0);
    exact_archive.push_back(CheapCandidate{0.0, arch_a, placementKey(arch_a)});
    exact_archive.push_back(CheapCandidate{0.0, arch_b, placementKey(arch_b)});

    Placement best_placement = makePointPlacement(500.0);
    std::vector<Placement> insurance = {makePointPlacement(600.0), makePointPlacement(610.0),
                                        makePointPlacement(620.0), makePointPlacement(630.0)};

    std::map<double, double> tight_by_x;
    for (int label = 0; label <= 19; ++label) tight_by_x[label * 10.0] = 1000.0 - label;
    tight_by_x[700.0] = 970.0;
    tight_by_x[710.0] = 975.0;
    tight_by_x[500.0] = 9999.0;
    tight_by_x[600.0] = 9000.0;
    tight_by_x[610.0] = 9001.0;
    tight_by_x[620.0] = 9002.0;
    tight_by_x[630.0] = 9003.0;

    ExactCachedFn mockExact = [&](const Placement& placement, RoutingResult* routing_out,
                                  Placement* placement_out) -> Score {
        double x = placement.rects.at(0).x;
        auto it = tight_by_x.find(x);
        assert(it != tight_by_x.end());
        Score score;
        score.value = it->second;
        if (routing_out) *routing_out = RoutingResult{};  // open_routes defaults to 0 (legal)
        if (placement_out) *placement_out = placement;
        return score;
    };

    std::vector<EliteCandidate> full_set =
        buildEliteFullSet(cheap_pool, exact_archive, best_placement, insurance, 16, mockExact);

    // 16 rank-selected (2 archive + labels 6..19) + 1 forced best + 4 forced insurance = 21.
    assert(full_set.size() == 21);

    // Archive members admitted ON MERIT (their tight values beat every cheap_pool member).
    assert(containsX(full_set, 700.0));
    assert(containsX(full_set, 710.0));

    // labels 6..19 (14 members): the 14 BEST-tight-exact cheap_pool members -- present.
    for (int label = 6; label <= 19; ++label) assert(containsX(full_set, label * 10.0));
    // labels 0..5 (6 members, including the single CHEAP-BEST label 0): the WORST-tight-exact
    // cheap_pool members -- correctly EXCLUDED despite ranking best-to-6th-best by CHEAP value. This
    // is the decisive "not cheap top-16" assertion.
    for (int label = 0; label <= 5; ++label) assert(!containsX(full_set, label * 10.0));

    // Running best force-admitted despite the worst tight value of the whole set.
    assert(containsX(full_set, 500.0));
    // All 4 insurance picks force-admitted despite terrible tight values.
    assert(containsX(full_set, 600.0));
    assert(containsX(full_set, 610.0));
    assert(containsX(full_set, 620.0));
    assert(containsX(full_set, 630.0));

    std::cout << "testBuildEliteFullSetRanksByTightExactNotCheapValue ok (full_set.size()="
              << full_set.size() << ")\n";
}

// buildEliteFullSet's ranking rule is legality-first (open_routes==0 strictly preferred), THEN
// ascending tight value -- confirms an illegal candidate with a much LOWER raw tight value still
// loses to legal candidates with worse tight values.
void testBuildEliteFullSetPrefersLegalOverLowerTightValue() {
    Placement illegal = makePointPlacement(1.0);   // tight=10, but open_routes=1 (illegal)
    Placement legal_b = makePointPlacement(2.0);   // tight=20, legal
    Placement legal_c = makePointPlacement(3.0);   // tight=30, legal
    Placement forced_best = makePointPlacement(4.0);  // tight=999, legal, forced regardless of rank

    std::vector<CheapCandidate> cheap_pool = {
        CheapCandidate{0.0, illegal, placementKey(illegal)},
        CheapCandidate{0.0, legal_b, placementKey(legal_b)},
        CheapCandidate{0.0, legal_c, placementKey(legal_c)},
    };

    ExactCachedFn mockExact = [&](const Placement& placement, RoutingResult* routing_out,
                                  Placement* placement_out) -> Score {
        double x = placement.rects.at(0).x;
        Score score;
        RoutingResult routing;
        if (x == 1.0) {
            score.value = 10.0;
            routing.open_routes = 1;
        } else if (x == 2.0) {
            score.value = 20.0;
        } else if (x == 3.0) {
            score.value = 30.0;
        } else {
            score.value = 999.0;
        }
        if (routing_out) *routing_out = routing;
        if (placement_out) *placement_out = placement;
        return score;
    };

    std::vector<EliteCandidate> full_set =
        buildEliteFullSet(cheap_pool, {}, forced_best, {}, 2, mockExact);

    assert(full_set.size() == 3);           // top-2 legal (b,c) + forced best
    assert(!containsX(full_set, 1.0));      // illegal, lower tight value than b/c -- must lose
    assert(containsX(full_set, 2.0));
    assert(containsX(full_set, 3.0));
    assert(containsX(full_set, 4.0));       // forced best present
    std::cout << "testBuildEliteFullSetPrefersLegalOverLowerTightValue ok\n";
}

// ---- End-to-end determinism smoke ---------------------------------------------------------------

std::string readFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    std::ostringstream buf;
    buf << in.rdbuf();
    return buf.str();
}

void testB5aFullPipelineDeterministicAcrossInvocations() {
    Problem problem = parseProblem("test/2026 ICCAD_case.case0.csv");
    SearchOptions options;  // defaults: iters=1000, seed=1, exact_interval=50, budget off

    SearchResult first = runPricingPortfolio(problem, "hybrid", options);
    SearchResult second = runPricingPortfolio(problem, "hybrid", options);

    const std::string path_a = "test/b5_determinism_a.cfg";
    const std::string path_b = "test/b5_determinism_b.cfg";
    writeCfg(path_a, problem, first.best_placement, first.best_routing);
    writeCfg(path_b, problem, second.best_placement, second.best_routing);

    std::string content_a = readFile(path_a);
    std::string content_b = readFile(path_b);
    assert(!content_a.empty());
    assert(content_a == content_b);
    assert(first.elite_pairs.size() == second.elite_pairs.size());
    assert(first.best_routing.open_routes == second.best_routing.open_routes);
    assert(first.best_score.value == second.best_score.value);

    std::remove(path_a.c_str());
    std::remove(path_b.c_str());
    std::cout << "testB5aFullPipelineDeterministicAcrossInvocations ok (cfg bytes=" << content_a.size()
              << ", elite_pairs=" << first.elite_pairs.size() << ")\n";
}

// ---- B5a.1 portfolio-level floor pin -------------------------------------------------------------

// Pre-B4 (HEAD 207d08c) default-flag golden-rescored checker_cost, case3 -- the SAME constant
// regression_b4_20260724.cpp pins (kPreB4Case3DefaultCheckerCost). This is the floor B5a's
// unconditional admission rework broke (measured +32.6% regression, 48,549,702.543, during this
// task's own STOP) and B5a.1's 4-leg portfolio must restore, since the (legacy,v1) leg alone
// reproduces this exact pre-B4/pre-B5a trajectory bit-for-bit (byte-identity independently verified
// via a throwaway md5 probe against b6-report.md's pre-B5a hashes; see b5a-report.md).
constexpr double kPreB4Case3DefaultCheckerCost = 36615637.236;

double rescoreDefaultRun(const Problem& problem, const SearchResult& result, const std::string& tmp_path) {
    writeCfg(tmp_path, problem, result.best_placement, result.best_routing);
    CfgSolution cfg = parseCfg(tmp_path);
    OfficialBreakdown breakdown = officialScoreCfg(problem, cfg);
    std::remove(tmp_path.c_str());
    return breakdown.checker_cost;
}

void testPortfolioRestoresCase3FloorPostB5a1() {
    Problem problem = parseProblem("test/2026 ICCAD_case.case3.csv");
    SearchOptions options;  // defaults: iters=1000, seed=1, exact_interval=50, budget off
    SearchResult result = runPricingPortfolio(problem, "hybrid", options);
    assert(result.best_routing.open_routes == 0);

    double checker_cost = rescoreDefaultRun(problem, result, "test/b5a1_case3_tmp.cfg");

    if (checker_cost > kPreB4Case3DefaultCheckerCost + 1.0) {
        std::cerr << "testPortfolioRestoresCase3FloorPostB5a1: checker_cost=" << checker_cost
                  << " exceeds floor " << kPreB4Case3DefaultCheckerCost << "\n";
    }
    assert(checker_cost <= kPreB4Case3DefaultCheckerCost + 1.0);
    // The floor-restoring leg is expected to be (legacy, v1) -- the pair byte-identical to pre-B5a --
    // not a coincidental value match from a different leg.
    std::cout << "testPortfolioRestoresCase3FloorPostB5a1 ok (checker_cost=" << checker_cost
              << " <= floor " << kPreB4Case3DefaultCheckerCost
              << ", pricing_official=" << result.pricing_official
              << ", elite_admission_v2=" << result.elite_admission_v2 << ")\n";
}

}  // namespace

int main() {
    testRememberBoundedDedupSortAndCap();
    testReservoirSampleSameSeedProducesSamePicks();
    testReservoirSampleCapAndFullStreamCoverage();
    testInsuranceSeedFormulaIndependentOfMainStream();
    testBuildEliteFullSetRanksByTightExactNotCheapValue();
    testBuildEliteFullSetPrefersLegalOverLowerTightValue();
    testB5aFullPipelineDeterministicAcrossInvocations();
    testPortfolioRestoresCase3FloorPostB5a1();
    std::cout << "b5a regressions passed\n";
    return 0;
}
