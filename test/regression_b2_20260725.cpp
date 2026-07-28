// B2 (seed-sweep budget phase, anytime output, submission hardening, .superpowers/sdd/b2-brief.md):
// regression coverage for PortfolioFold::admit's value-gated streaming fold + anytime-write hook
// (items 1-2) and shouldRunPhase2Leg's per-leg skip arithmetic (item 1), plus real-case coverage on
// the small/fast official case0 (test/2026 ICCAD_case.case0.csv, 7 blocks -- the same fixture
// test/regression_b6_20260724.cpp already validated as suite-speed-appropriate for a full
// runPricingPortfolio call) confirming: budget-off stays phase-1-only (legs_run==8,
// budget_phase2==false), budget-on fires the seed sweep and never regresses the budget-off value
// (monotone by construction), and the anytime-written file on disk after a budgeted run byte-
// matches a fresh writeCfg of the actually-returned final best (the brief's own "verify the final
// file equals the final best" regression -- kill-safety itself is not portably testable in a
// suite). The pre-B2-vs-post-B2 5-case byte-identity hard gate (md5 vs pre-B2 outputs) is a manual
// battery, reported in b2-report.md, not reproduced here -- b3/b4/b6's own regression files document
// the identical choice for their own analogous byte-identity requirements.
#include <cassert>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "../src/io.hpp"
#include "../src/model.hpp"
#include "../src/search.hpp"

namespace {
using namespace efgr;

constexpr double kTol = 1e-6;

std::string readFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    std::ostringstream buf;
    buf << in.rdbuf();
    return buf.str();
}

// Minimal legal-shaped SearchResult for PortfolioFold::admit's fast synthetic tests -- admit() only
// reads best_routing.{open_routes,feedthrough_mismatch}, best_score.value, iterations, and
// exact_evaluations, and passes best_placement/best_routing through to on_improved untouched, so a
// default-constructed Placement (no rects) is sufficient; nothing here decodes or validates it.
SearchResult makeLegResult(int open_routes, double value, double mismatch, int iterations = 10,
                           int exact_evals = 5) {
    SearchResult r;
    r.best_routing.open_routes = open_routes;
    r.best_routing.feedthrough_mismatch = mismatch;
    r.best_score.value = value;
    r.iterations = iterations;
    r.exact_evaluations = exact_evals;
    return r;
}

// ---- shouldRunPhase2Leg (item 1) ---------------------------------------------------------------

void testShouldRunPhase2LegMarginBoundary() {
    // remaining < 1.5*leg_time -> skip; remaining >= 1.5*leg_time -> run (brief's own "<" wording,
    // search.hpp's doc comment on shouldRunPhase2Leg records the equality-runs choice explicitly).
    assert(!shouldRunPhase2Leg(/*remaining=*/7.4, /*leg_time=*/5.0));   // 7.4 < 7.5 -> skip
    assert(shouldRunPhase2Leg(/*remaining=*/7.5, /*leg_time=*/5.0));    // exactly at margin -> run
    assert(shouldRunPhase2Leg(/*remaining=*/7.6, /*leg_time=*/5.0));    // clears margin -> run
    assert(!shouldRunPhase2Leg(/*remaining=*/-1.0, /*leg_time=*/0.1));  // negative remaining -> skip
    assert(shouldRunPhase2Leg(/*remaining=*/100.0, /*leg_time=*/0.0));  // zero-time leg always fits
    std::cout << "testShouldRunPhase2LegMarginBoundary ok\n";
}

// ---- PortfolioFold::admit (items 1-2) ----------------------------------------------------------

void testFirstLegAlwaysImprovesEvenIfIllegal() {
    Problem problem;
    PortfolioFold fold;
    SearchVariant v0 = defaultVariants()[0];
    // Deliberately illegal (open_routes>0): the empty fold has no incumbent to lose to, so the
    // FIRST admit always becomes the running best regardless of legality -- the existing (pre-B2)
    // paydownBetter precedent this fold reuses only ever compares against an ALREADY-admitted
    // incumbent. Combined with the brief's own "first write after the first leg result" rule, this
    // is why phase 1's leg 0 always triggers the first anytime write with no special-casing.
    bool improved = fold.admit(problem, makeLegResult(/*open_routes=*/3, 1e9, 0.0), v0, /*leg_seed=*/1, nullptr);
    assert(improved);
    assert(fold.legs_run == 1);
    assert(fold.picked_seed == 1);
    std::cout << "testFirstLegAlwaysImprovesEvenIfIllegal ok\n";
}

void testSecondLegWorseDoesNotImproveWriteCountMatchesImprovements() {
    Problem problem;
    PortfolioFold fold;
    std::vector<SearchVariant> variants = defaultVariants();
    int write_count = 0;
    AnytimeWriteFn counter = [&](const Problem&, const Placement&, const RoutingResult&) { ++write_count; };

    bool improved1 = fold.admit(problem, makeLegResult(0, 100.0, 0.0), variants[0], 1, counter);
    bool improved2 = fold.admit(problem, makeLegResult(0, 200.0, 0.0), variants[1], 1, counter);  // worse (higher value)

    assert(improved1 && !improved2);
    assert(write_count == 1);  // brief: "write count on a 2-leg synthetic = number of fold improvements"
    assert(std::abs(fold.result.best_score.value - 100.0) < kTol);
    assert(fold.legs_run == 2);
    std::cout << "testSecondLegWorseDoesNotImproveWriteCountMatchesImprovements ok (writes=" << write_count << ")\n";
}

void testSecondLegBetterImprovesWriteCountMatchesImprovements() {
    Problem problem;
    PortfolioFold fold;
    std::vector<SearchVariant> variants = defaultVariants();
    int write_count = 0;
    AnytimeWriteFn counter = [&](const Problem&, const Placement&, const RoutingResult&) { ++write_count; };

    bool improved1 = fold.admit(problem, makeLegResult(0, 200.0, 0.0), variants[0], 1, counter);
    bool improved2 = fold.admit(problem, makeLegResult(0, 100.0, 0.0), variants[1], 2001, counter);  // better

    assert(improved1 && improved2);
    assert(write_count == 2);
    assert(std::abs(fold.result.best_score.value - 100.0) < kTol);
    assert(fold.picked_seed == 2001);  // tracks the seed of the LAST improving leg, not the last admitted one
    std::cout << "testSecondLegBetterImprovesWriteCountMatchesImprovements ok (writes=" << write_count << ")\n";
}

void testLegalityBeatsLowerValue() {
    Problem problem;
    PortfolioFold fold;
    std::vector<SearchVariant> variants = defaultVariants();
    // Leg 0: legal, high value. Leg 1: illegal (open_routes=1), numerically LOWER value -- must NOT
    // win (fewer open_routes wins outright, matching paydownBetter's own legality-first rule).
    fold.admit(problem, makeLegResult(0, 1000.0, 0.0), variants[0], 1, nullptr);
    bool improved2 = fold.admit(problem, makeLegResult(1, 1.0, 0.0), variants[1], 1, nullptr);
    assert(!improved2);
    assert(fold.result.best_routing.open_routes == 0);
    assert(std::abs(fold.result.best_score.value - 1000.0) < kTol);
    std::cout << "testLegalityBeatsLowerValue ok\n";
}

void testAdmitWritesViaRealWriteCfgPath() {
    // Brief: "unit-test the write-on-improvement logic via the EXISTING writeCfg path" -- the
    // on_improved callback here calls the REAL io.cpp writeCfg, not a mock, on a tiny in-memory
    // Problem/Placement/RoutingResult, and asserts the resulting file is well-formed and matches a
    // second, independent writeCfg call of the SAME (problem, placement, routing) triple.
    Problem problem;
    Block b;
    b.name = "BLK1";
    b.width = 10.0;
    b.height = 10.0;
    b.area = 100.0;
    problem.blocks = {b};
    problem.outline_w = 100.0;
    problem.outline_h = 100.0;

    Placement placement;
    placement.rects = {Rect{0.0, 0.0, 10.0, 10.0}};
    placement.aspect = {1.0};
    placement.outline_w = 100.0;
    placement.outline_h = 100.0;

    RoutingResult routing;  // no channels/routes -- a legal, trivially empty routing

    const std::string scratch_path = "test/b2_admit_write_scratch.cfg";
    std::remove(scratch_path.c_str());
    int write_count = 0;
    AnytimeWriteFn writer = [&](const Problem& p, const Placement& pl, const RoutingResult& rr) {
        writeCfg(scratch_path, p, pl, rr);
        ++write_count;
    };

    PortfolioFold fold;
    SearchVariant v0 = defaultVariants()[0];
    SearchResult leg = makeLegResult(0, 42.0, 0.0);
    leg.best_placement = placement;
    leg.best_routing = routing;
    leg.best_routing.open_routes = 0;
    leg.best_score.value = 42.0;
    bool improved = fold.admit(problem, leg, v0, 1, writer);

    assert(improved);
    assert(write_count == 1);
    std::string written = readFile(scratch_path);
    assert(!written.empty());
    assert(written.find("BLK1 0.000 0.000 10.000 10.000") != std::string::npos);

    // Independent second write of the SAME triple must byte-match -- writeCfg is deterministic.
    const std::string independent_path = "test/b2_admit_write_independent.cfg";
    writeCfg(independent_path, problem, placement, routing);
    assert(written == readFile(independent_path));

    std::remove(scratch_path.c_str());
    std::remove(independent_path.c_str());
    std::cout << "testAdmitWritesViaRealWriteCfgPath ok (bytes=" << written.size() << ")\n";
}

// ---- defaultVariants() floor-leg sanity (pruning-policy reference, b2-report.md item 4) --------

void testDefaultVariantsFloorLegsUnchanged() {
    std::vector<SearchVariant> variants = defaultVariants();
    // F-FT (2026-07-28) appended 2 ft_pricing_aggressive legs: 8 -> 10 (same bump as B5b/B5c).
    assert(variants.size() == 10);
    // The two "floor" legs the b2-brief.md pruning policy names as never-droppable: v1-admission
    // crossed with {official,legacy} pricing, both credit-off/moves-off -- indices 0 and 1.
    assert(variants[0].pricing_official && !variants[0].elite_admission_v2 && !variants[0].abutment_credit &&
          !variants[0].move_portfolio_v2);
    assert(!variants[1].pricing_official && !variants[1].elite_admission_v2 && !variants[1].abutment_credit &&
          !variants[1].move_portfolio_v2);
    std::cout << "testDefaultVariantsFloorLegsUnchanged ok\n";
}

// ---- Real-case coverage (case0, 7 blocks -- same fast fixture b6 already uses) ------------------

void testBudgetOffIsPhase1Only() {
    Problem problem = parseProblem("test/2026 ICCAD_case.case0.csv");
    SearchOptions options;  // defaults: iters=1000, seed=1, budget off
    SearchResult result = runPricingPortfolio(problem, "hybrid", options);
    // F-FT (2026-07-28): registry is 10 legs now (2 ft_pricing_aggressive legs appended).
    assert(result.legs_run == 10);
    assert(!result.budget_phase2);
    assert(result.picked_seed == 1);
    std::cout << "testBudgetOffIsPhase1Only ok (value=" << result.best_score.value << ")\n";
}

void testBudgetOnNeverRegressesBudgetOffAndFiresSeedSweep() {
    Problem problem = parseProblem("test/2026 ICCAD_case.case0.csv");
    SearchOptions base_options;  // budget off
    SearchResult base = runPricingPortfolio(problem, "hybrid", base_options);

    SearchOptions budgeted_options;
    budgeted_options.time_budget_sec = 20.0;  // ample for case0's own cheap ~5s phase-1 cost
    SearchResult budgeted = runPricingPortfolio(problem, "hybrid", budgeted_options);

    assert(budgeted.budget_phase2);
    assert(budgeted.legs_run > 8);
    // Monotone by construction: phase 1's own legs are always in budgeted's folded set too.
    assert(budgeted.best_routing.open_routes <= base.best_routing.open_routes);
    if (budgeted.best_routing.open_routes == base.best_routing.open_routes) {
        assert(budgeted.best_score.value <= base.best_score.value + kTol);
    }
    // picked_seed is either the phase-1 seed or a phase-2 sweep seed -- never options.seed+1 (that
    // is hybrid's OWN internal bstar-leg offset, not a phase-2 sweep seed -- see search.hpp).
    assert(budgeted.picked_seed == 1 || (budgeted.picked_seed - 1) % 1000 == 0);
    assert(budgeted.picked_seed != 2);
    std::cout << "testBudgetOnNeverRegressesBudgetOffAndFiresSeedSweep ok (base=" << base.best_score.value
              << ", budgeted=" << budgeted.best_score.value << ", legs_run=" << budgeted.legs_run
              << ", picked_seed=" << budgeted.picked_seed << ")\n";
}

void testAnytimeWriteFinalFileMatchesFinalBestOnRealCase() {
    // Brief: "verify the final file equals the final best (byte compare) on one real case."
    Problem problem = parseProblem("test/2026 ICCAD_case.case0.csv");
    SearchOptions options;
    options.time_budget_sec = 15.0;  // fires phase 1 + likely some phase-2 rounds

    const std::string anytime_path = "test/b2_anytime_realcase.cfg";
    std::remove(anytime_path.c_str());
    int write_count = 0;
    AnytimeWriteFn writer = [&](const Problem& p, const Placement& pl, const RoutingResult& rr) {
        writeCfg(anytime_path, p, pl, rr);
        ++write_count;
    };

    SearchResult result = runPricingPortfolio(problem, "hybrid", options, writer);
    assert(write_count >= 1);  // at minimum, phase 1's leg 0 always triggers the first write

    const std::string final_path = "test/b2_final_realcase.cfg";
    writeCfg(final_path, problem, result.best_placement, result.best_routing);

    std::string anytime_bytes = readFile(anytime_path);
    std::string final_bytes = readFile(final_path);
    assert(!anytime_bytes.empty());
    assert(anytime_bytes == final_bytes);

    std::remove(anytime_path.c_str());
    std::remove(final_path.c_str());
    std::cout << "testAnytimeWriteFinalFileMatchesFinalBestOnRealCase ok (writes=" << write_count
              << ", bytes=" << final_bytes.size() << ")\n";
}

}  // namespace

int main() {
    testShouldRunPhase2LegMarginBoundary();
    testFirstLegAlwaysImprovesEvenIfIllegal();
    testSecondLegWorseDoesNotImproveWriteCountMatchesImprovements();
    testSecondLegBetterImprovesWriteCountMatchesImprovements();
    testLegalityBeatsLowerValue();
    testAdmitWritesViaRealWriteCfgPath();
    testDefaultVariantsFloorLegsUnchanged();
    testBudgetOffIsPhase1Only();
    testBudgetOnNeverRegressesBudgetOffAndFiresSeedSweep();
    testAnytimeWriteFinalFileMatchesFinalBestOnRealCase();
    std::cout << "b2 regressions passed\n";
    return 0;
}
