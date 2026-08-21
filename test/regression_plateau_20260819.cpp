// P1 stage 1 (plateau-stop, .superpowers/sdd/p1-plateau-stop-design-20260819.md): regression
// coverage for runtimeSecondPrice (§5.3), plateauStopDecision (§5.2), and PlateauTracker (§10.1) --
// the pure-function machinery the phase-2 leg-launch decision point (search.cpp's runPricingPortfolio)
// uses -- plus real-case coverage on case0 (test/2026 ICCAD_case.case0.csv, 7 blocks, the same fast
// fixture test/regression_b2_20260725.cpp already validated) confirming stage 1's own observe-only
// contract: options.plateau_stop=false never breaks phase 2 regardless of the predicate firing,
// --plateau-margin 0 disables the lever outright even with plateau_stop=true, and a stop (when the
// flag IS set) never fires before the design's own §5.2 S2/S3 evidence floor (>=1 full phase-2
// round of legal samples).
//
// Note on §11.2 item 3's illustrative numbers: the design doc's own worked "0.5*sqrt(3858480) ~=
// 982.36" is off by about 0.2 (the true value is ~982.15) -- testPatienceArithmetic below derives S
// from the ACTUAL runtimeSecondPrice(3858480) instead of the design's hand-computed price, which
// keeps the "S/price == 200 exactly" test intent intact without baking in that rounding slip.
#include <cassert>
#include <cmath>
#include <iostream>
#include <string>
#include <vector>

#include "../src/io.hpp"
#include "../src/model.hpp"
#include "../src/search.hpp"

namespace {
using namespace efgr;

constexpr double kTol = 1e-6;

// ---- runtimeSecondPrice (§5.3) -------------------------------------------------------------------

void testRuntimeSecondPrice() {
    const double oa = 3858480.0;
    const double expected = kRuntimeCostDelta * std::sqrt(oa);
    const double got = runtimeSecondPrice(oa);
    assert(std::abs(got - expected) <= 1e-6 * std::max(1.0, std::abs(expected)));
    assert(runtimeSecondPrice(0.0) == 0.0);
    assert(runtimeSecondPrice(-5.0) == 0.0);
    std::cout << "testRuntimeSecondPrice ok (price=" << got << ")\n";
}

// ---- plateauStopDecision (§5.2) ------------------------------------------------------------------

PlateauInputs makeFiringBaseline() {
    PlateauInputs in;
    in.have_best = true;
    in.best_open_routes = 0;
    in.best_outline_area = 3858480.0;
    const double price = runtimeSecondPrice(in.best_outline_area);
    in.leg_value_spread = price * 100.0;  // patience S/price = 100 at margin 1.0
    in.legal_leg_samples = 10;
    in.phase2_legs_launched = 10;
    in.min_legs = 10;
    in.round_wall_sec = 10.0;
    in.margin = 1.0;
    in.drought_sec = 150.0;  // well past both the 100s patience and the 10s t0 floor
    return in;
}

void testPlateauStopDecisionConditions() {
    const PlateauInputs base = makeFiringBaseline();
    assert(plateauStopDecision(base));  // baseline is a known FIRE

    PlateauInputs no_result = base;
    no_result.have_best = false;
    assert(!plateauStopDecision(no_result));

    PlateauInputs illegal = base;
    illegal.best_open_routes = 1;
    assert(!plateauStopDecision(illegal));

    PlateauInputs no_evidence = base;
    no_evidence.phase2_legs_launched = base.min_legs - 1;
    assert(!plateauStopDecision(no_evidence));

    PlateauInputs few_samples = base;
    few_samples.legal_leg_samples = base.min_legs - 1;
    assert(!plateauStopDecision(few_samples));

    PlateauInputs disabled = base;
    disabled.margin = 0.0;
    assert(!plateauStopDecision(disabled));

    PlateauInputs unknown_price = base;
    unknown_price.best_outline_area = 0.0;
    assert(!plateauStopDecision(unknown_price));

    // drought boundary: >= patience fires, < patience does not (matches shouldRunPhase2Leg's own
    // documented equality-runs convention).
    const double price = runtimeSecondPrice(base.best_outline_area);
    const double patience = std::max(base.margin * base.leg_value_spread / price, base.round_wall_sec);
    PlateauInputs at_boundary = base;
    at_boundary.drought_sec = patience;
    assert(plateauStopDecision(at_boundary));
    PlateauInputs below_boundary = base;
    below_boundary.drought_sec = patience - 0.1;
    assert(!plateauStopDecision(below_boundary));

    std::cout << "testPlateauStopDecisionConditions ok\n";
}

// ---- Patience arithmetic (design §11.2 item 3) ----------------------------------------------------

void testPatienceArithmetic() {
    const double oa = 3858480.0;
    const double price = runtimeSecondPrice(oa);
    const double t0 = 10.0;
    const double s200 = price * 200.0;  // S chosen so S/price == 200 exactly (see file header note)

    PlateauInputs in;
    in.have_best = true;
    in.best_open_routes = 0;
    in.best_outline_area = oa;
    in.leg_value_spread = s200;
    in.legal_leg_samples = 10;
    in.phase2_legs_launched = 10;
    in.min_legs = 10;
    in.round_wall_sec = t0;
    in.margin = 1.0;

    in.drought_sec = 200.0;
    assert(plateauStopDecision(in));
    in.drought_sec = 199.9;
    assert(!plateauStopDecision(in));

    in.margin = 2.0;  // P doubles to 400
    in.drought_sec = 400.0;
    assert(plateauStopDecision(in));
    in.drought_sec = 399.9;
    assert(!plateauStopDecision(in));

    in.margin = 1.0;
    in.leg_value_spread = 0.0;  // S=0 -> P floors at t0
    in.drought_sec = t0;
    assert(plateauStopDecision(in));
    in.drought_sec = t0 - 0.1;
    assert(!plateauStopDecision(in));

    std::cout << "testPatienceArithmetic ok (price=" << price << ")\n";
}

// ---- Monotonicity (design §11.2 item 4) -----------------------------------------------------------

void testMonotonicity() {
    PlateauInputs base;
    base.have_best = true;
    base.best_open_routes = 0;
    base.best_outline_area = 3858480.0;
    base.legal_leg_samples = 10;
    base.phase2_legs_launched = 10;
    base.min_legs = 10;
    base.round_wall_sec = 10.0;

    // Non-decreasing in drought_sec: once true for a given (margin, spread), stays true for every
    // larger drought on the same grid.
    for (double margin : {0.5, 1.0, 2.0}) {
        for (double spread : {0.0, 50000.0, 500000.0}) {
            bool seen_true = false;
            for (double drought = 0.0; drought <= 1000.0; drought += 25.0) {
                PlateauInputs in = base;
                in.margin = margin;
                in.leg_value_spread = spread;
                in.drought_sec = drought;
                const bool fired = plateauStopDecision(in);
                if (seen_true) assert(fired);
                if (fired) seen_true = true;
            }
        }
    }

    // Non-increasing in margin: larger margin (more patient) never turns a false into a true on the
    // same (drought, spread) grid point.
    for (double drought : {50.0, 150.0, 500.0}) {
        for (double spread : {50000.0, 300000.0}) {
            bool last_fired = true;  // sentinel: skip the check on the first grid point
            for (double margin : {0.5, 1.0, 2.0, 4.0, 8.0}) {
                PlateauInputs in = base;
                in.margin = margin;
                in.leg_value_spread = spread;
                in.drought_sec = drought;
                const bool fired = plateauStopDecision(in);
                if (fired) assert(last_fired);
                last_fired = fired;
            }
        }
    }

    // Non-increasing in leg_value_spread: same direction as margin (a more dispersed landscape is
    // MORE patient, never less).
    for (double drought : {50.0, 150.0, 500.0}) {
        for (double margin : {0.5, 1.0, 2.0}) {
            bool last_fired = true;
            for (double spread : {0.0, 10000.0, 100000.0, 500000.0, 2000000.0}) {
                PlateauInputs in = base;
                in.margin = margin;
                in.leg_value_spread = spread;
                in.drought_sec = drought;
                const bool fired = plateauStopDecision(in);
                if (fired) assert(last_fired);
                last_fired = fired;
            }
        }
    }

    std::cout << "testMonotonicity ok\n";
}

// ---- PlateauTracker::legValueSpread (design §11.2 item 5) ------------------------------------------

void testLegValueSpreadEmptyAndSingleton() {
    PlateauTracker empty;
    assert(std::abs(empty.legValueSpread() - 0.0) < kTol);

    PlateauTracker singleton;
    singleton.observeLeg(5.0, /*leg_open_routes=*/0, /*improved=*/false, /*elapsed_sec=*/1.0);
    assert(std::abs(singleton.legValueSpread() - 0.0) < kTol);

    std::cout << "testLegValueSpreadEmptyAndSingleton ok\n";
}

void testLegValueSpreadOddEvenUpperMedian() {
    PlateauTracker odd;
    odd.observeLeg(10.0, 0, false, 1.0);
    odd.observeLeg(20.0, 0, false, 2.0);
    odd.observeLeg(30.0, 0, false, 3.0);
    // sorted {10,20,30}: upper median sorted[3/2]=sorted[1]=20; spread = 20-10 = 10.
    assert(std::abs(odd.legValueSpread() - 10.0) < kTol);

    PlateauTracker even;
    even.observeLeg(10.0, 0, false, 1.0);
    even.observeLeg(20.0, 0, false, 2.0);
    even.observeLeg(30.0, 0, false, 3.0);
    even.observeLeg(40.0, 0, false, 4.0);
    // sorted {10,20,30,40}: upper median sorted[4/2]=sorted[2]=30; spread = 30-10 = 20.
    assert(std::abs(even.legValueSpread() - 20.0) < kTol);

    std::cout << "testLegValueSpreadOddEvenUpperMedian ok\n";
}

void testLegValueSpreadExcludesIllegalLegsAndOrderIndependent() {
    PlateauTracker t;
    t.observeLeg(999999.0, /*leg_open_routes=*/3, false, 1.0);  // illegal -- excluded from X
    t.observeLeg(30.0, 0, false, 2.0);
    t.observeLeg(10.0, 0, false, 3.0);
    t.observeLeg(20.0, 0, false, 4.0);
    assert(t.leg_values.size() == 3);
    assert(std::abs(t.legValueSpread() - 10.0) < kTol);

    PlateauTracker reordered;  // same legal multiset, different insertion order
    reordered.observeLeg(10.0, 0, false, 1.0);
    reordered.observeLeg(20.0, 0, false, 2.0);
    reordered.observeLeg(30.0, 0, false, 3.0);
    assert(std::abs(reordered.legValueSpread() - t.legValueSpread()) < kTol);

    std::cout << "testLegValueSpreadExcludesIllegalLegsAndOrderIndependent ok\n";
}

// ---- PlateauTracker clocks (design §11.2 item 6) ---------------------------------------------------

void testPlateauTrackerClocks() {
    PlateauTracker t;
    t.observeLeg(100.0, 0, /*improved=*/true, /*elapsed_sec=*/5.0);
    assert(std::abs(t.last_improve_sec - 5.0) < kTol);

    t.beginPhase2(30.0);
    assert(std::abs(t.phase2_start_sec - 30.0) < kTol);
    assert(std::abs(t.droughtSec(35.0) - 5.0) < kTol);

    t.observeLeg(50.0, 0, /*improved=*/true, /*elapsed_sec=*/40.0);
    assert(std::abs(t.droughtSec(45.0) - 5.0) < kTol);

    std::cout << "testPlateauTrackerClocks ok\n";
}

// ---- End-to-end (case0 CSV, design §11.2 items 7-9) -------------------------------------------------

void testObserveModeNeverBreaksPhase2() {
    Problem problem = parseProblem("test/2026 ICCAD_case.case0.csv");
    SearchOptions options;
    options.time_budget_sec = 20.0;
    options.plateau_stop = false;  // stage 1 observe-only: never breaks regardless of the predicate
    SearchResult r1 = runPricingPortfolio(problem, "hybrid", options);
    SearchResult r2 = runPricingPortfolio(problem, "hybrid", options);
    assert(!r1.plateau_stopped);
    assert(!r2.plateau_stopped);
    assert(r1.legs_run > 0 && r1.best_routing.open_routes == 0);
    assert(r2.legs_run > 0 && r2.best_routing.open_routes == 0);
    std::cout << "testObserveModeNeverBreaksPhase2 ok (legs_run1=" << r1.legs_run
              << ", legs_run2=" << r2.legs_run << ")\n";
}

void testPlateauMarginZeroDisables() {
    Problem problem = parseProblem("test/2026 ICCAD_case.case0.csv");
    SearchOptions options;
    options.time_budget_sec = 20.0;
    options.plateau_stop = true;
    options.plateau_margin = 0.0;  // S4 disable path
    SearchResult result = runPricingPortfolio(problem, "hybrid", options);
    assert(!result.plateau_stopped);
    std::cout << "testPlateauMarginZeroDisables ok (legs_run=" << result.legs_run << ")\n";
}

void testPlateauStopNeverFiresBeforeEvidenceFloor() {
    Problem problem = parseProblem("test/2026 ICCAD_case.case0.csv");
    SearchOptions options;
    options.time_budget_sec = 20.0;
    options.plateau_stop = true;
    options.plateau_margin = 1.0;
    SearchResult result = runPricingPortfolio(problem, "hybrid", options);
    assert(result.best_routing.open_routes == 0);
    if (result.plateau_stopped) {
        assert(result.plateau_fire_sec > 0.0);
        assert(result.legs_run >= 2 * static_cast<int>(defaultVariants().size()));
    }
    std::cout << "testPlateauStopNeverFiresBeforeEvidenceFloor ok (plateau_stopped="
              << result.plateau_stopped << ", legs_run=" << result.legs_run
              << ", plateau_t=" << result.plateau_fire_sec << ")\n";
}

}  // namespace

int main() {
    testRuntimeSecondPrice();
    testPlateauStopDecisionConditions();
    testPatienceArithmetic();
    testMonotonicity();
    testLegValueSpreadEmptyAndSingleton();
    testLegValueSpreadOddEvenUpperMedian();
    testLegValueSpreadExcludesIllegalLegsAndOrderIndependent();
    testPlateauTrackerClocks();
    testObserveModeNeverBreaksPhase2();
    testPlateauMarginZeroDisables();
    testPlateauStopNeverFiresBeforeEvidenceFloor();
    std::cout << "plateau regressions passed\n";
    return 0;
}
