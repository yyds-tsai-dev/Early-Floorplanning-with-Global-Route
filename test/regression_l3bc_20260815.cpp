// L3b/L3c (exact-tier budget governor + deep mode, case5-design.md §4-L3b/§4-L3c,
// .superpowers/sdd/case5-l3bc-brief.md): regression coverage for the deep-mode arithmetic helpers
// (deepModeTrigger/deepLegCount/deepLegSlice -- pure, unit-tested with case5-shaped numbers), the
// deepVariants() registry pin, and the brief's three structural gates on the real case0 fixture
// (same small/fast fixture regression_b2_20260725.cpp established):
//   (i)  budget-off -> sa_wall_time_sec stamped, NO deep mode, full registry;
//   (ii) tiny budget -> structurally valid result, sane phase1_legs/legs_run accounting;
//   (iii) deep-mode firing pinned via the pure trigger helper (deterministic) plus a live
//        tiny-budget run where the trigger provably fires (leg 0's wall >> budget/10) -- deep leg
//        COUNT actually run is machine-speed dependent (slices can legitimately collapse to 0 on a
//        slow machine), so the accounting identity, not a minimum count, is what's asserted.
// The budget-off 8-case byte-identity hard gate is the manual battery (same choice as b2/b3/b4/b6's
// regression files document for their own analogous requirements).
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

// ---- Pure deep-mode arithmetic (case5-design.md §4-L3c steps 3-4) ------------------------------

void testDeepModeTriggerArithmetic() {
    // case5-shaped numbers (design §4-L3c step 3): T0 ~ 80s at 10 registry legs vs a 600s budget
    // -> 800 > 600 -> deep mode. A cheap case (T0 ~ 3s) never triggers at 600s.
    assert(deepModeTrigger(600.0, 10, 80.0));
    assert(!deepModeTrigger(600.0, 10, 50.0));   // 500 < 600 -> registry fits, run the registry
    assert(!deepModeTrigger(600.0, 10, 60.0));   // exactly 600 is NOT > 600 -> registry just fits
    assert(!deepModeTrigger(0.0, 10, 1e9));      // budget off can never trigger (first operand)
    assert(!deepModeTrigger(-1.0, 10, 1e9));
    assert(deepModeTrigger(900.0, 10, 91.0));    // the actual case5 bare-vs-900s shape
    std::cout << "testDeepModeTriggerArithmetic ok\n";
}

void testDeepLegCountArithmetic() {
    // N_deep = clamp(floor((budget - T0) / (1.5 * M0)), 1, 3). case5-shaped: budget 600, T0 80,
    // M0 70 -> floor(520/105) = 4 -> clamped 3.
    assert(deepLegCount(600.0, 80.0, 70.0) == 3);
    // Tight: budget 200, T0 80, M0 70 -> floor(120/105) = 1.
    assert(deepLegCount(200.0, 80.0, 70.0) == 1);
    // Mid: budget 400, T0 80, M0 70 -> floor(320/105) = 3 -> stays 3 (exactly at cap).
    assert(deepLegCount(400.0, 80.0, 70.0) == 3);
    // Overrun (budget already spent by leg 0) -> raw negative -> clamped up to 1 (the per-leg
    // slice check is what then declines to actually launch).
    assert(deepLegCount(60.0, 80.0, 70.0) == 1);
    // Machinery unmeasurably small -> optimistic cap 3.
    assert(deepLegCount(600.0, 80.0, 0.0) == 3);
    assert(deepLegCount(600.0, 80.0, -1.0) == 3);
    std::cout << "testDeepLegCountArithmetic ok\n";
}

void testDeepLegSliceArithmetic() {
    // First leg of 3, case5-shaped: (600 - 80)/3 - 1.5*70 = 173.33 - 105 = 68.33.
    const double s = deepLegSlice(600.0, 80.0, 3, 70.0);
    assert(std::abs(s - (520.0 / 3.0 - 105.0)) < 1e-9);
    // Later leg with the budget nearly gone -> floored at 0, never negative.
    assert(deepLegSlice(600.0, 590.0, 1, 70.0) == 0.0);
    // legs_left is clamped >= 1 defensively.
    assert(deepLegSlice(600.0, 80.0, 1, 0.0) > deepLegSlice(600.0, 80.0, 3, 0.0));
    std::cout << "testDeepLegSliceArithmetic ok\n";
}

void testDeepVariantsRegistryPinned() {
    // The brief's exact 3-leg list, in order: (official,v1,credit-off,moves-off,ftagg-ON),
    // (official,v2,credit-off,moves-off,ftagg-ON), (official,v2,credit-off,moves-on,ftagg-off).
    const std::vector<SearchVariant> dv = deepVariants();
    assert(dv.size() == 3);
    assert(dv[0].pricing_official && !dv[0].elite_admission_v2 && !dv[0].abutment_credit &&
           !dv[0].move_portfolio_v2 && dv[0].ft_pricing_aggressive);
    assert(dv[1].pricing_official && dv[1].elite_admission_v2 && !dv[1].abutment_credit &&
           !dv[1].move_portfolio_v2 && dv[1].ft_pricing_aggressive);
    assert(dv[2].pricing_official && dv[2].elite_admission_v2 && !dv[2].abutment_credit &&
           dv[2].move_portfolio_v2 && !dv[2].ft_pricing_aggressive);
    std::cout << "testDeepVariantsRegistryPinned ok\n";
}

// ---- Real-case structural gates (case0, same fixture as the b2 suite) --------------------------

void testT_iBudgetOffNoDeepModeFullRegistry() {
    // Brief gate (i): budget-off -> sa_wall_time_sec stamped, no deep mode, full registry. The SA
    // loop runs >= 500 iterations per leg here, so its summed wall is strictly positive on any
    // real clock.
    Problem problem = parseProblem("test/2026 ICCAD_case.case0.csv");
    SearchOptions options;  // defaults: iters=1000, seed=1, budget off
    SearchResult result = runPricingPortfolio(problem, "hybrid", options);
    assert(!result.deep_mode);
    assert(result.deep_legs_run == 0);
    assert(result.phase1_legs_run == static_cast<int>(defaultVariants().size()));
    assert(result.legs_run == static_cast<int>(defaultVariants().size()));
    assert(result.sa_wall_time_sec > 0.0);
    std::cout << "testT_iBudgetOffNoDeepModeFullRegistry ok (sa_wall="
              << result.sa_wall_time_sec << "s)\n";
}

void testT_iiTinyBudgetStructurallyValid() {
    // Brief gate (ii): tiny budget -> deep mode may or may not trigger (leg 0's wall vs 0.05s is
    // machine-dependent), but the result must be structurally valid and the leg accounting exact:
    // with phase 2 skipped (budget_phase2==false), every executed leg is a phase-1 or deep leg.
    Problem problem = parseProblem("test/2026 ICCAD_case.case0.csv");
    SearchOptions options;
    options.time_budget_sec = 0.05;
    SearchResult result = runPricingPortfolio(problem, "hybrid", options);
    assert(result.phase1_legs_run >= 1);
    assert(result.phase1_legs_run <= static_cast<int>(defaultVariants().size()));
    assert(result.deep_legs_run >= 0 && result.deep_legs_run <= 3);
    if (!result.deep_mode) assert(result.deep_legs_run == 0);
    if (result.deep_mode) assert(result.phase1_legs_run == 1);
    assert(!result.budget_phase2 || result.deep_legs_run == 0);  // deep mode and phase 2 exclusive
    if (!result.budget_phase2) {
        assert(result.legs_run == result.phase1_legs_run + result.deep_legs_run);
    }
    assert(!result.best_placement.rects.empty());
    std::cout << "testT_iiTinyBudgetStructurallyValid ok (deep=" << result.deep_mode
              << ", deep_legs=" << result.deep_legs_run
              << ", phase1_legs=" << result.phase1_legs_run
              << ", legs_run=" << result.legs_run << ")\n";
}

void testT_iiiDeepModeFiresWhenRegistryCannotFit() {
    // Brief gate (iii): a budget the registry provably cannot fit. iters=20000 makes leg 0's SA
    // loop alone consume real wall (case0's 10-leg registry at these iters costs many seconds), so
    // 10 * T0 > 0.5s holds on any machine and the trigger MUST fire: exactly 1 phase-1 leg, no
    // phase 2, and legs_run accounting exact. How many deep legs actually launch is machine-speed
    // dependent (deepLegSlice may legitimately collapse to 0 when leg 0 overran the whole budget),
    // so the count itself is printed, not asserted.
    Problem problem = parseProblem("test/2026 ICCAD_case.case0.csv");
    SearchOptions options;
    options.iters = 20000;
    options.time_budget_sec = 0.5;
    SearchResult result = runPricingPortfolio(problem, "hybrid", options);
    assert(result.deep_mode);
    assert(result.phase1_legs_run == 1);
    assert(!result.budget_phase2);
    assert(result.deep_legs_run >= 0 && result.deep_legs_run <= 3);
    assert(result.legs_run == 1 + result.deep_legs_run);
    assert(result.sa_wall_time_sec > 0.0);
    assert(!result.best_placement.rects.empty());
    std::cout << "testT_iiiDeepModeFiresWhenRegistryCannotFit ok (deep_legs="
              << result.deep_legs_run << ", legs_run=" << result.legs_run << ")\n";
}

}  // namespace

int main() {
    testDeepModeTriggerArithmetic();
    testDeepLegCountArithmetic();
    testDeepLegSliceArithmetic();
    testDeepVariantsRegistryPinned();
    testT_iBudgetOffNoDeepModeFullRegistry();
    testT_iiTinyBudgetStructurallyValid();
    testT_iiiDeepModeFiresWhenRegistryCannotFit();
    std::cout << "l3bc (deep mode + budget governor) regressions passed\n";
    return 0;
}
