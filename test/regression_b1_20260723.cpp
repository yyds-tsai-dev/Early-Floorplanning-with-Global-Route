// B1 (research doc 2026-07-23 §2 + controller amendment 2026-07-24): comparator truth table for
// paydownBetter/acceptFeedthroughPaydown's value-gated rule, plus end-to-end pins for the three-leg
// hybrid amendment's guarantee (a case's default-flag value must never exceed its documented pre-B1
// baseline). See .superpowers/sdd/b1-brief.md, .superpowers/sdd/b1-brief-amended.md, and
// .superpowers/sdd/b1-report.md for the full narrative (the byte-identity premise was falsified for
// cases 2/3/4, which is why the three-leg design and this file's upper-bound test exist).
#include <cassert>
#include <algorithm>
#include <iostream>
#include <memory>
#include <string>

#include "../src/io.hpp"
#include "../src/model.hpp"
#include "../src/placement_engine.hpp"
#include "../src/search.hpp"
#include "../src/validate.hpp"

namespace {
using namespace efgr;

// Value dominates mismatch (both functions): a candidate/leg with worse (nonzero) mismatch but a
// better value wins outright -- the core property B1 establishes.
void testValueDominatesMismatchPaydownBetter() {
    RoutingResult a;
    a.open_routes = 0;
    a.feedthrough_mismatch = 5000.0;
    RoutingResult b;
    b.open_routes = 0;
    b.feedthrough_mismatch = 0.0;
    assert(paydownBetter(a, 100.0, b, 200.0));
    assert(!paydownBetter(b, 200.0, a, 100.0));
    std::cout << "testValueDominatesMismatchPaydownBetter ok\n";
}

void testValueDominatesMismatchAcceptFeedthroughPaydown() {
    RoutingResult candidate;
    candidate.open_routes = 0;
    candidate.feedthrough_mismatch = 5000.0;
    RoutingResult best;
    best.open_routes = 0;
    best.feedthrough_mismatch = 0.0;
    assert(acceptFeedthroughPaydown(candidate, 100.0, best, 200.0));
    std::cout << "testValueDominatesMismatchAcceptFeedthroughPaydown ok\n";
}

// open_routes is a hard legality gate that still dominates value/mismatch in both functions --
// unchanged by B1.
void testOpenRoutesDominatesValuePaydownBetter() {
    RoutingResult a;
    a.open_routes = 1;
    a.feedthrough_mismatch = 0.0;
    RoutingResult b;
    b.open_routes = 0;
    b.feedthrough_mismatch = 1.0e9;
    assert(!paydownBetter(a, 1.0, b, 1.0e15));
    assert(paydownBetter(b, 1.0e15, a, 1.0));
    std::cout << "testOpenRoutesDominatesValuePaydownBetter ok\n";
}

void testOpenRoutesRejectsAcceptFeedthroughPaydown() {
    RoutingResult candidate;
    candidate.open_routes = 1;
    candidate.feedthrough_mismatch = 0.0;
    RoutingResult best;
    best.open_routes = 0;
    best.feedthrough_mismatch = 1.0e9;
    assert(!acceptFeedthroughPaydown(candidate, 1.0, best, 1.0e15));
    std::cout << "testOpenRoutesRejectsAcceptFeedthroughPaydown ok\n";
}

// Exact-value tie: lower mismatch wins as a tiebreak only; equal mismatch + equal value -> neither
// side is "better"/"accepted" (no incumbent churn on a true tie).
void testExactValueTieLowerMismatchWinsPaydownBetter() {
    RoutingResult a;
    a.open_routes = 0;
    a.feedthrough_mismatch = 100.0;
    RoutingResult b;
    b.open_routes = 0;
    b.feedthrough_mismatch = 200.0;
    assert(paydownBetter(a, 500.0, b, 500.0));
    assert(!paydownBetter(b, 500.0, a, 500.0));

    RoutingResult c;
    c.open_routes = 0;
    c.feedthrough_mismatch = 100.0;
    RoutingResult d;
    d.open_routes = 0;
    d.feedthrough_mismatch = 100.0;
    assert(!paydownBetter(c, 500.0, d, 500.0));
    assert(!paydownBetter(d, 500.0, c, 500.0));
    std::cout << "testExactValueTieLowerMismatchWinsPaydownBetter ok\n";
}

void testExactValueTieLowerMismatchWinsAcceptFeedthroughPaydown() {
    RoutingResult candidate;
    candidate.open_routes = 0;
    candidate.feedthrough_mismatch = 100.0;
    RoutingResult best;
    best.open_routes = 0;
    best.feedthrough_mismatch = 200.0;
    assert(acceptFeedthroughPaydown(candidate, 500.0, best, 500.0));

    RoutingResult worse_candidate;
    worse_candidate.open_routes = 0;
    worse_candidate.feedthrough_mismatch = 200.0;
    RoutingResult lower_best;
    lower_best.open_routes = 0;
    lower_best.feedthrough_mismatch = 100.0;
    assert(!acceptFeedthroughPaydown(worse_candidate, 500.0, lower_best, 500.0));

    RoutingResult tied_candidate;
    tied_candidate.open_routes = 0;
    tied_candidate.feedthrough_mismatch = 100.0;
    RoutingResult tied_best;
    tied_best.open_routes = 0;
    tied_best.feedthrough_mismatch = 100.0;
    assert(!acceptFeedthroughPaydown(tied_candidate, 500.0, tied_best, 500.0));
    std::cout << "testExactValueTieLowerMismatchWinsAcceptFeedthroughPaydown ok\n";
}

// PINNED REGRESSION: worse value + lower mismatch is now REJECTED (the pre-B1 rule's acceptance,
// now refused). Same magnitudes as the rewritten test/regression_p0_20260706.cpp assertions, so the
// two files cross-check each other.
void testWorseValueLowerMismatchRejectedPaydownBetter() {
    RoutingResult a;
    a.open_routes = 0;
    a.feedthrough_mismatch = 0.0;
    RoutingResult b;
    b.open_routes = 0;
    b.feedthrough_mismatch = 5000.0;
    assert(!paydownBetter(a, 2.19e11, b, 6.39e10));
    assert(paydownBetter(b, 6.39e10, a, 2.19e11));
    std::cout << "testWorseValueLowerMismatchRejectedPaydownBetter ok\n";
}

void testWorseValueLowerMismatchRejectedAcceptFeedthroughPaydown() {
    RoutingResult candidate;
    candidate.open_routes = 0;
    candidate.feedthrough_mismatch = 0.0;
    RoutingResult best;
    best.open_routes = 0;
    best.feedthrough_mismatch = 5000.0;
    assert(!acceptFeedthroughPaydown(candidate, 2.19e11, best, 6.39e10));
    std::cout << "testWorseValueLowerMismatchRejectedAcceptFeedthroughPaydown ok\n";
}

// B1 amendment item 5 (hybrid-consistency): hybrid's final result must never be worse (by value)
// than the SP leg it was folded from -- paydownBetter's fold in runHybridPlacementSearch starts from
// sp_result and only replaces it with a strictly-better (or exact-tie) candidate. Full 3-leg
// introspection would require reconstructing bstar_a/bstar_b's internal seed chain
// (sp_result.best_placement / legacy_paydown_placement feeding a freshly-seeded bstar run) outside
// runHybridPlacementSearch, which would duplicate its own internals for marginal extra assurance
// beyond what testHybridDefaultFlagsNeverExceedPreB1Baseline below already covers end-to-end -- so
// this test checks the SP leg only (reproducible from the public API via two derived fields, iters/2
// and the unchanged seed, with zero dependency on hybrid's internal bstar-seeding steps).
void testHybridFinalNeverWorseThanSpLegCase1LowIters() {
    Problem problem = parseProblem("test/2026 ICCAD_case.case1.csv");
    SearchOptions options;
    options.iters = 200;
    options.seed = 1;
    SearchResult hybrid = runHybridPlacementSearch(problem, options);

    SearchOptions sp_options = options;
    sp_options.iters = std::max(1, options.iters / 2);
    sp_options.time_budget_sec = options.time_budget_sec * 0.5;
    std::unique_ptr<PlacementEngine> sp_engine = makePlacementEngine("sp");
    SearchResult sp_leg = runPlacementSearch(problem, *sp_engine, sp_options);

    assert(hybrid.best_routing.open_routes == 0);
    if (hybrid.best_score.value > sp_leg.best_score.value + kEps) {
        std::cerr << "testHybridFinalNeverWorseThanSpLegCase1LowIters: hybrid=" << hybrid.best_score.value
                   << " sp_leg=" << sp_leg.best_score.value << "\n";
    }
    assert(hybrid.best_score.value <= sp_leg.best_score.value + kEps);
    std::cout << "testHybridFinalNeverWorseThanSpLegCase1LowIters ok (hybrid=" << hybrid.best_score.value
               << " sp_leg=" << sp_leg.best_score.value << ")\n";
}

// THE GUARANTEE (B1 amendment item 4): per-case default-flag (hybrid, iters=1000, seed=1, budget
// off) value must never exceed the documented PRE-B1 baseline, measured at HEAD 922a60a (before any
// B1 edit) with the identical invocation -- see b1-report.md for the measurement transcript. This is
// the regression the three-leg amendment exists to guarantee: the legacy paydown walk is demoted to
// a seed generator (never a selection criterion again), so a case whose only route to a good result
// was the old rule's willingness to trade value for mismatch still has that trajectory available
// (via the third, legacy-seeded bstar leg), adjudicated honestly by value.
struct CaseUpperBound {
    const char* csv;
    double pre_b1_value;
};

// Full round-trip-precision doubles (%.17g, exact under IEEE754 double round-trip), measured from a
// throwaway pre-B1 build (git stash push -- src/search.cpp src/search.hpp at HEAD 922a60a, rebuild,
// probe, stash pop; see b1-report.md for the transcript). An earlier version of this file pinned the
// 12-significant-figure values bin/efgr's stderr summary prints, which is NOT enough precision for a
// 1e-6 absolute tolerance at this magnitude (~1e7-1e8) and caused a spurious near-miss on case1.
const CaseUpperBound kPreB1DefaultUpperBounds[5] = {
    {"test/2026 ICCAD_case.case0.csv", 5781379.0},
    {"test/2026 ICCAD_case.case1.csv", 14031473.200829031},
    {"test/2026 ICCAD_case.case2.csv", 74930410.788959861},
    {"test/2026 ICCAD_case.case3.csv", 44704168.445734486},
    {"test/2026 ICCAD_case.case4.csv", 113982466.84331457},
};

void testHybridDefaultFlagsNeverExceedPreB1Baseline() {
    for (const auto& c : kPreB1DefaultUpperBounds) {
        Problem problem = parseProblem(c.csv);
        SearchOptions options;  // defaults: iters=1000, seed=1, exact_interval=50, budget off
        SearchResult result = runHybridPlacementSearch(problem, options);

        assert(result.best_routing.open_routes == 0);
        ValidationResult vr = validateSolution(problem, result.best_placement, result.best_routing);
        if (!vr.ok) {
            for (const auto& m : vr.messages) std::cerr << c.csv << " validate: " << m << "\n";
        }
        assert(vr.ok);
        if (result.best_score.value > c.pre_b1_value + kEps) {
            std::cerr << c.csv << ": value=" << result.best_score.value << " exceeds pre-B1 baseline "
                      << c.pre_b1_value << "\n";
        }
        assert(result.best_score.value <= c.pre_b1_value + kEps);
    }
    std::cout << "testHybridDefaultFlagsNeverExceedPreB1Baseline ok\n";
}

}  // namespace

int main() {
    testValueDominatesMismatchPaydownBetter();
    testValueDominatesMismatchAcceptFeedthroughPaydown();
    testOpenRoutesDominatesValuePaydownBetter();
    testOpenRoutesRejectsAcceptFeedthroughPaydown();
    testExactValueTieLowerMismatchWinsPaydownBetter();
    testExactValueTieLowerMismatchWinsAcceptFeedthroughPaydown();
    testWorseValueLowerMismatchRejectedPaydownBetter();
    testWorseValueLowerMismatchRejectedAcceptFeedthroughPaydown();
    testHybridFinalNeverWorseThanSpLegCase1LowIters();
    testHybridDefaultFlagsNeverExceedPreB1Baseline();
    std::cout << "b1 regressions passed\n";
    return 0;
}
