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
#include "../src/eval.hpp"
#include "../src/io.hpp"
#include "../src/model.hpp"
#include "../src/placement_engine.hpp"
#include "../src/route_cost.hpp"
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

// ---- B5b: abutment-credit cheap-score upgrade (.superpowers/sdd/b5-design.md §3) -----------------
//
// Two 10x10 blocks, A at the origin and B offset horizontally by `gap`, sharing a y-overlap (so they
// can abut on a vertical edge, design §3.2's g=dx_gap branch) -- a fixed two-block fixture the design
// doc's own §3.3 monotonicity argument (credited_WL(g) = M(g)*(g/d_thr)*nets, strictly increasing in
// g) is derived against verbatim. d_thr = kAbutRampFrac*0.5*(10+10) = 10.0 for this fixture. nets is
// chosen large enough (1000) that routeAwareProxyPenalty's congestion term is also exercised
// (capacity = min(a.h,b.h)*25 = 250 < 1000), so both credited terms (WL, congestion) are visible.
// Placement.outline_w/h are pinned well above MAX so cheapScore never calls usedOutline -- outline_area
// stays fixed regardless of `gap`, isolating the credit's own effect on score.value.
Problem makeAbutmentPairProblem(int nets) {
    Problem problem;
    // MAX outline (the containment bound cheapScore's legality penalty checks) -- separate from
    // Placement.outline_w/h (the declared/stamped outline used only for the outline_area term).
    // Must be large enough to hold every `gap` this fixture's tests sweep.
    problem.outline_w = 1000.0;
    problem.outline_h = 1000.0;
    Block a;
    a.name = "A";
    a.width = 10.0;
    a.height = 10.0;
    a.area = 100.0;
    problem.blocks.push_back(a);
    Block b;
    b.name = "B";
    b.width = 10.0;
    b.height = 10.0;
    b.area = 100.0;
    problem.blocks.push_back(b);
    problem.bundles.push_back(Bundle{0, 1, nets});
    return problem;
}

Placement makeAbutmentPairPlacement(double gap) {
    Placement placement;
    placement.rects.push_back(Rect{0.0, 0.0, 10.0, 10.0});
    placement.rects.push_back(Rect{10.0 + gap, 0.0, 10.0, 10.0});
    placement.outline_w = 1000.0;
    placement.outline_h = 1000.0;
    return placement;
}

// (a) abutting pair (positive overlap segment -- here a full shared vertical edge -- gap 0) -> full
// credit -> BOTH the WL proxy and the congestion proxy contribute ZERO for that bundle. With only
// one bundle and no other blocks, score.penalty is entirely that bundle's (now-zeroed) congestion
// term, so asserting it is exactly 0.0 pins the congestion side too, not just wirelength.
void testAbutmentCreditFullAtExactTouch() {
    Problem problem = makeAbutmentPairProblem(1000);
    problem.abutment_credit = true;
    Placement placement = makeAbutmentPairPlacement(0.0);  // gap == 0: exact positiveTouch-style abut
    Score score = cheapScore(problem, placement);
    assert(score.wirelength == 0.0);
    assert(score.penalty == 0.0);
    std::cout << "testAbutmentCreditFullAtExactTouch ok\n";
}

// (b) gap == d_thr -> credit == 0 -> byte-SAME as credit-off for that bundle. credit = clamp(1 -
// g/d_thr, 0, 1) = clamp(1-1, 0, 1) = 0.0 exactly (g/d_thr == 1.0 exactly, no rounding, since both
// are the same double literal 10.0), so (1-credit) == 1.0 exactly and multiplying by it changes
// nothing bit-for-bit (IEEE754 x*1.0 == x) -- the two Scores must compare EQUAL in every field.
void testAbutmentCreditZeroAtRampThreshold() {
    Problem problem_on = makeAbutmentPairProblem(1000);
    problem_on.abutment_credit = true;
    Problem problem_off = problem_on;
    problem_off.abutment_credit = false;
    Placement placement = makeAbutmentPairPlacement(10.0);  // gap == d_thr exactly
    Score on = cheapScore(problem_on, placement);
    Score off = cheapScore(problem_off, placement);
    assert(on.wirelength == off.wirelength);
    assert(on.penalty == off.penalty);
    assert(on.outline_area == off.outline_area);
    assert(on.value == off.value);
    std::cout << "testAbutmentCreditZeroAtRampThreshold ok\n";
}

// (c) mid-ramp monotonicity: shrinking the gap strictly reduces the credited cheap value (both the
// WL term alone and the full score.value) on this fixed two-block fixture -- the design §3.3 claim,
// checked numerically at 3 points strictly inside (0, d_thr).
void testAbutmentCreditMidRampMonotonicity() {
    Problem problem = makeAbutmentPairProblem(1000);
    problem.abutment_credit = true;
    Score wide = cheapScore(problem, makeAbutmentPairPlacement(8.0));
    Score mid = cheapScore(problem, makeAbutmentPairPlacement(5.0));
    Score narrow = cheapScore(problem, makeAbutmentPairPlacement(2.0));
    assert(wide.wirelength > mid.wirelength);
    assert(mid.wirelength > narrow.wirelength);
    assert(wide.value > mid.value);
    assert(mid.value > narrow.value);
    std::cout << "testAbutmentCreditMidRampMonotonicity ok (wl@8=" << wide.wirelength
              << " wl@5=" << mid.wirelength << " wl@2=" << narrow.wirelength << ")\n";
}

// (d) flag-off byte-identity on a nontrivial (3-block, 2-bundle) fixture: cheapScore with
// abutment_credit=false must reproduce the pre-B5b arithmetic EXACTLY, independently recomputed here
// from the same public helpers (manhattan, kBetaLin) cheapScore/routeAwareProxyPenalty themselves
// use -- not merely "close". Block A-B sit at EXACT geometric abutment (gap 0, the case that would
// be fully credited if the flag were on); flag-off must still charge the FULL uncredited manhattan
// distance/congestion for that pair, proving the credit is skipped via an `if` gate, never merely
// multiplied by 1.0.
void testAbutmentCreditFlagOffByteIdenticalOnNontrivialFixture() {
    Problem problem;
    problem.alpha = 1.0;
    problem.abutment_credit = false;
    problem.outline_w = 500.0;
    problem.outline_h = 500.0;
    Block a;
    a.width = 10.0;
    a.height = 10.0;
    a.area = 100.0;
    problem.blocks.push_back(a);
    Block b;
    b.width = 8.0;
    b.height = 6.0;
    b.area = 48.0;
    problem.blocks.push_back(b);
    Block c;
    c.width = 12.0;
    c.height = 12.0;
    c.area = 144.0;
    problem.blocks.push_back(c);
    problem.bundles.push_back(Bundle{0, 1, 200});  // A-B: exact abutment (gap 0)
    problem.bundles.push_back(Bundle{1, 2, 50});   // B-C: separated, both under capacity

    Placement placement;
    placement.rects.push_back(Rect{0.0, 0.0, 10.0, 10.0});   // A
    placement.rects.push_back(Rect{10.0, 2.0, 8.0, 6.0});    // B -- abuts A's right edge exactly
    placement.rects.push_back(Rect{25.0, 0.0, 12.0, 12.0});  // C -- separated from B
    placement.outline_w = 500.0;
    placement.outline_h = 500.0;

    Score score = cheapScore(problem, placement);

    const Rect& ra = placement.rects[0];
    const Rect& rb = placement.rects[1];
    const Rect& rc = placement.rects[2];

    double expected_wire = manhattan(ra.cx(), ra.cy(), rb.cx(), rb.cy()) * 200.0 +
                          manhattan(rb.cx(), rb.cy(), rc.cx(), rc.cy()) * 50.0;
    assert(score.wirelength == expected_wire);
    assert(expected_wire > 0.0);  // sharp check: A-B's uncredited WL is NOT zeroed despite gap==0

    double expected_outline_area = 500.0 * 500.0;
    assert(score.outline_area == expected_outline_area);

    // Independently recompute the congestion (routeAwareProxyPenalty) term the same way
    // eval.cpp does, using the same public kBetaLin constant (route_cost.hpp) -- neither bundle's
    // bbox intersects the third (non-source/target) block here, so no FT-proxy term applies and
    // congestion is the WHOLE penalty.
    auto congestionFor = [](const Rect& x, const Rect& y, int nets) {
        double dx = std::abs(x.cx() - y.cx());
        double dy = std::abs(x.cy() - y.cy());
        double narrow = dx >= dy ? std::max(1.0, std::min(x.h, y.h)) : std::max(1.0, std::min(x.w, y.w));
        double overflow = std::max(0.0, static_cast<double>(nets) - narrow * 25.0);
        return overflow * kBetaLin;
    };
    double expected_penalty = congestionFor(ra, rb, 200) + congestionFor(rb, rc, 50);
    assert(score.penalty == expected_penalty);

    double expected_value = expected_outline_area + problem.alpha * expected_wire + expected_penalty;
    assert(score.value == expected_value);
    std::cout << "testAbutmentCreditFlagOffByteIdenticalOnNontrivialFixture ok (wirelength="
              << score.wirelength << ", penalty=" << score.penalty << ")\n";
}

// Registry shape (b5-design.md §4, coordinator adjudication applying B5a's flag-gated-variant
// lesson): defaultVariants() must hold exactly 8 legs -- the pre-existing 6 (B5a.1/B5a.2/B5b,
// UNCHANGED) first, then B5c's 2 new legs (both official-pricing, credit-on, moves-on: admission-v2
// then admission-v1) -- in this exact order, since runPricingPortfolio's fold/telemetry stamping and
// the budget-mode phase split both depend on iterating this registry, not a hardcoded leg count.
void testDefaultVariantsRegistryHasEightLegsInOrder() {
    std::vector<SearchVariant> variants = defaultVariants();
    assert(variants.size() == 8);
    assert(variants[0].pricing_official == true && variants[0].elite_admission_v2 == false &&
          variants[0].abutment_credit == false && variants[0].move_portfolio_v2 == false);
    assert(variants[1].pricing_official == false && variants[1].elite_admission_v2 == false &&
          variants[1].abutment_credit == false && variants[1].move_portfolio_v2 == false);
    assert(variants[2].pricing_official == true && variants[2].elite_admission_v2 == true &&
          variants[2].abutment_credit == false && variants[2].move_portfolio_v2 == false);
    assert(variants[3].pricing_official == false && variants[3].elite_admission_v2 == true &&
          variants[3].abutment_credit == false && variants[3].move_portfolio_v2 == false);
    assert(variants[4].pricing_official == true && variants[4].elite_admission_v2 == true &&
          variants[4].abutment_credit == true && variants[4].move_portfolio_v2 == false);
    assert(variants[5].pricing_official == true && variants[5].elite_admission_v2 == false &&
          variants[5].abutment_credit == true && variants[5].move_portfolio_v2 == false);
    assert(variants[6].pricing_official == true && variants[6].elite_admission_v2 == true &&
          variants[6].abutment_credit == true && variants[6].move_portfolio_v2 == true);
    assert(variants[7].pricing_official == true && variants[7].elite_admission_v2 == false &&
          variants[7].abutment_credit == true && variants[7].move_portfolio_v2 == true);
    std::cout << "testDefaultVariantsRegistryHasEightLegsInOrder ok\n";
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

// ---- B5c: move-portfolio upgrade (.superpowers/sdd/b5-design.md §4) -----------------------------
//
// Flag-gated exactly like B5a/B5b before it: SearchOptions.move_portfolio_v2 is threaded via
// makePlacementEngine's second argument (placement_engine.hpp's own doc comments record the
// "engine construction, not mutate's signature" threading decision and its documented caveat).

Problem makeTwoBlockAbutmentProblem(int x_port_edge, int p_port_edge, int nets) {
    Problem problem;
    problem.outline_w = 1000.0;
    problem.outline_h = 1000.0;
    Block x_block;
    x_block.width = 10.0;
    x_block.height = 10.0;
    x_block.area = 100.0;
    x_block.type = BlockType::Macro;
    x_block.port_edge = x_port_edge;
    Block p_block;
    p_block.width = 10.0;
    p_block.height = 10.0;
    p_block.area = 100.0;
    p_block.type = BlockType::Macro;
    p_block.port_edge = p_port_edge;
    problem.blocks.push_back(x_block);
    problem.blocks.push_back(p_block);
    problem.bundles.push_back(Bundle{0, 1, nets});
    return problem;
}

// ---- (b) wireMedianTarget: weighted-median correctness + even-count tie rule --------------------

void testWireMedianTargetEvenCountTieUsesLowerMedian() {
    Problem problem;
    problem.outline_w = 1000.0;
    problem.outline_h = 1000.0;
    for (int i = 0; i < 5; ++i) {
        Block b;
        b.width = 1.0;
        b.height = 1.0;
        b.area = 1.0;
        b.type = BlockType::Macro;
        problem.blocks.push_back(b);
    }
    problem.bundles.push_back(Bundle{0, 1, 1});
    problem.bundles.push_back(Bundle{0, 2, 1});
    problem.bundles.push_back(Bundle{0, 3, 1});
    problem.bundles.push_back(Bundle{0, 4, 1});

    Placement placement;
    placement.rects.push_back(Rect{500.0, 500.0, 1.0, 1.0});  // block 0 = X, own position irrelevant
    placement.rects.push_back(Rect{0.0, 0.0, 0.0, 0.0});      // partner @ x=0
    placement.rects.push_back(Rect{10.0, 0.0, 0.0, 0.0});     // partner @ x=10
    placement.rects.push_back(Rect{20.0, 0.0, 0.0, 0.0});     // partner @ x=20
    placement.rects.push_back(Rect{30.0, 0.0, 0.0, 0.0});     // partner @ x=30

    auto target = wireMedianTarget(problem, placement, 0);
    // 4 equal-weight (nets=1) partners: total weight 4, half=2.0. Cumulative weight first reaches
    // >=2.0 at the SECOND sorted point (x=10) -- the LOWER weighted median (the design doc's own
    // wireMedian formula does not pin an alternate even-count convention; this is the standard one,
    // documented in wireMedianTarget's own header comment).
    assert(std::abs(target.first - 10.0) < 1e-9);
    assert(std::abs(target.second - 0.0) < 1e-9);
    std::cout << "testWireMedianTargetEvenCountTieUsesLowerMedian ok (target=(" << target.first << ","
              << target.second << "))\n";
}

void testWireMedianTargetWeightsByNetCount() {
    Problem problem;
    problem.outline_w = 1000.0;
    problem.outline_h = 1000.0;
    for (int i = 0; i < 3; ++i) {
        Block b;
        b.width = 1.0;
        b.height = 1.0;
        b.area = 1.0;
        b.type = BlockType::Macro;
        problem.blocks.push_back(b);
    }
    problem.bundles.push_back(Bundle{0, 1, 10});  // heavy partner @ x=0
    problem.bundles.push_back(Bundle{0, 2, 1});   // light partner @ x=100

    Placement placement;
    placement.rects.push_back(Rect{500.0, 500.0, 1.0, 1.0});
    placement.rects.push_back(Rect{0.0, 0.0, 0.0, 0.0});
    placement.rects.push_back(Rect{100.0, 0.0, 0.0, 0.0});

    auto target = wireMedianTarget(problem, placement, 0);
    // total weight 11, half=5.5; cumulative after the heavy (x=0, weight 10) partner is 10 >= 5.5,
    // so the median sits AT the heavy partner, not at the midpoint (50) or the light partner (100).
    assert(std::abs(target.first - 0.0) < 1e-9);
    std::cout << "testWireMedianTargetWeightsByNetCount ok (target=(" << target.first << ","
              << target.second << "))\n";
}

void testWireMedianTargetNoBundlesReturnsOwnCenter() {
    Problem problem;
    problem.outline_w = 1000.0;
    problem.outline_h = 1000.0;
    Block b;
    b.width = 10.0;
    b.height = 10.0;
    b.area = 100.0;
    b.type = BlockType::Macro;
    problem.blocks.push_back(b);
    Placement placement;
    placement.rects.push_back(Rect{40.0, 60.0, 10.0, 10.0});
    auto target = wireMedianTarget(problem, placement, 0);
    assert(std::abs(target.first - 45.0) < 1e-9);
    assert(std::abs(target.second - 65.0) < 1e-9);
    std::cout << "testWireMedianTargetNoBundlesReturnsOwnCenter ok\n";
}

// ---- (c) abutment-targeted move respects PORT EDGE legality --------------------------------------

void testAbutmentSideRealizesPortLegalTouch(const std::string& engine_name) {
    // X only allows edge 3 (right), P only allows edge 1 (left) -- per the design's own §4.4 worked
    // example, this makes "Left" (X.right touches P.x, edges {X:3,P:1}) the ONLY port-legal side;
    // Right/Below/Above each require an edge neither block allows.
    Problem problem = makeTwoBlockAbutmentProblem(/*x_port_edge=*/3, /*p_port_edge=*/1, /*nets=*/500);

    // mutateAbutment's own port-legality pre-check (private; reimplemented here since routing.cpp's
    // portAllows has internal linkage -- see b5c-report.md's scope note), confirming this fixture's
    // premise: exactly one of the 4 sides is legal, and it is Left.
    struct SidePorts {
        AdjacentSide side;
        int edge_x;
        int edge_p;
    };
    static constexpr SidePorts kOrder[] = {
        {AdjacentSide::Left, 3, 1},
        {AdjacentSide::Right, 1, 3},
        {AdjacentSide::Below, 2, 4},
        {AdjacentSide::Above, 4, 2},
    };
    auto portOk = [](const Block& blk, int edge) { return blk.port_edge <= 0 || blk.port_edge == edge; };
    int legal_count = 0;
    AdjacentSide chosen = AdjacentSide::Left;
    for (const auto& sp : kOrder) {
        if (portOk(problem.blocks[0], sp.edge_x) && portOk(problem.blocks[1], sp.edge_p)) {
            ++legal_count;
            chosen = sp.side;
        }
    }
    assert(legal_count == 1);
    assert(chosen == AdjacentSide::Left);

    std::unique_ptr<PlacementEngine> engine = makePlacementEngine(engine_name);
    std::mt19937 rng(99);
    PlacementState state = engine->initialState(problem, rng);
    if (engine_name == "sp") {
        reinsertSPAdjacent(state, 0, 1, chosen);
    } else {
        bool ok = reinsertBStarChild(state, 0, 1, chosen, rng, /*strict=*/true);
        assert(ok);
    }
    assert(engine->isValidState(problem, state));
    Placement place = engine->decode(problem, state);
    const Rect& rx = place.rects[0];
    const Rect& rp = place.rects[1];
    // "Left" realized: X.right touches P.x, with a positive y-overlap (positiveTouch-consistent).
    assert(std::abs(rx.right() - rp.x) < 1e-6);
    assert(std::min(rx.top(), rp.top()) - std::max(rx.y, rp.y) > 1e-6);
    std::cout << "testAbutmentSideRealizesPortLegalTouch(" << engine_name << ") ok\n";
}

// ---- (d) greedy-reposition's worstBlock metric ---------------------------------------------------

void testWorstBlockPicksHighestBadnessBlock() {
    Problem problem;
    problem.outline_w = 2000.0;
    problem.outline_h = 2000.0;
    problem.alpha = 1.0;
    for (int i = 0; i < 4; ++i) {
        Block b;
        b.width = 10.0;
        b.height = 10.0;
        b.area = 100.0;
        b.type = BlockType::Macro;
        problem.blocks.push_back(b);
    }
    // block3 is the shared bundle-partner/anchor for block0 and block1 -- marked Edge so worstBlock's
    // movable-only candidate set (movableBlocks) excludes it as a CANDIDATE (an edge block cannot be
    // repositioned by these moves anyway); it still works fine as a bundle partner (worstBlock's
    // inner per-bundle loop does not filter by the PARTNER's type, only the candidate's). Without
    // this, block3 itself would win (it accumulates badness from BOTH bundles below and edges out
    // block0's own single-bundle badness by a small margin) -- not a worstBlock bug, just the wrong
    // fixture shape for isolating "does the single worst CANDIDATE win."
    problem.blocks[3].type = BlockType::Edge;
    problem.bundles.push_back(Bundle{0, 3, 1000});  // block0: heavy nets, far from the shared anchor
    problem.bundles.push_back(Bundle{1, 3, 1});     // block1: light nets, near the shared anchor
    // block2 has no bundles at all -- badness 0, must never win over block0 or block1.

    Placement placement;
    placement.rects.push_back(Rect{0.0, 0.0, 10.0, 10.0});
    placement.rects.push_back(Rect{500.0, 500.0, 10.0, 10.0});
    placement.rects.push_back(Rect{1000.0, 1000.0, 10.0, 10.0});
    placement.rects.push_back(Rect{510.0, 500.0, 10.0, 10.0});  // shared anchor (block 3)

    int worst = worstBlock(problem, placement);
    assert(worst == 0);
    std::cout << "testWorstBlockPicksHighestBadnessBlock ok (worst=" << worst << ")\n";
}

// ---- (a) flag-off byte-identity: independently-reimplemented reference vs the real engine --------
//
// Verbatim copy of BStarEngine::moveLeaf (private) so the reference below is independently derived,
// not merely "calls the same private function twice."
void referenceMoveLeafCopy(PlacementState& state, std::mt19937& rng) {
    std::vector<int> leaves;
    int n = static_cast<int>(state.nodes.size());
    for (int i = 0; i < n; ++i) {
        if (i != state.root && state.left[i] < 0 && state.right[i] < 0) leaves.push_back(i);
    }
    if (leaves.empty()) return;
    std::uniform_int_distribution<int> pick_leaf(0, static_cast<int>(leaves.size()) - 1);
    int leaf = leaves[pick_leaf(rng)];
    int old_parent = state.parent[leaf];
    if (old_parent >= 0) {
        if (state.left[old_parent] == leaf) state.left[old_parent] = -1;
        if (state.right[old_parent] == leaf) state.right[old_parent] = -1;
    }
    state.parent[leaf] = -1;
    std::vector<int> targets;
    for (int i = 0; i < n; ++i) {
        if (i == leaf) continue;
        if (state.left[i] < 0 || state.right[i] < 0) targets.push_back(i);
    }
    if (targets.empty()) {
        state.parent[leaf] = old_parent;
        if (old_parent >= 0) state.left[old_parent] = leaf;
        return;
    }
    std::uniform_int_distribution<int> pick_target(0, static_cast<int>(targets.size()) - 1);
    int target = targets[pick_target(rng)];
    bool use_left = state.left[target] < 0;
    if (state.left[target] < 0 && state.right[target] < 0) {
        std::uniform_int_distribution<int> side(0, 1);
        use_left = side(rng) == 0;
    }
    if (use_left) {
        state.left[target] = leaf;
    } else {
        state.right[target] = leaf;
    }
    state.parent[leaf] = target;
}

Problem makeFiveMovableBlockProblem() {
    Problem problem;
    problem.outline_w = 2000.0;
    problem.outline_h = 2000.0;
    for (int i = 0; i < 5; ++i) {
        Block b;
        b.width = 10.0 + i;
        b.height = 10.0 + i;
        b.area = b.width * b.height;
        // All macro (no soft, no edge) -- moves 3/4 (soft-aspect/edge-choice) are guaranteed no-ops
        // in BOTH the real engine and the reimplemented reference below, isolating the structural-
        // swap/moveLeaf dispatch this test actually verifies.
        b.type = BlockType::Macro;
        problem.blocks.push_back(b);
    }
    problem.bundles.push_back(Bundle{0, 1, 100});
    problem.bundles.push_back(Bundle{1, 2, 50});
    problem.bundles.push_back(Bundle{2, 3, 30});
    return problem;
}

void testSPFlagOffByteIdenticalToReimplementedReference() {
    Problem problem = makeFiveMovableBlockProblem();
    std::unique_ptr<PlacementEngine> engine = makePlacementEngine("sp");  // move_portfolio_v2 default false
    std::mt19937 seed_rng(4242);
    PlacementState initial = engine->initialState(problem, seed_rng);
    PlacementState actual = initial;
    PlacementState reference = initial;

    std::mt19937 actual_rng(777);
    std::mt19937 reference_rng(777);
    for (int i = 0; i < 1000; ++i) {
        engine->mutate(problem, actual, actual_rng);

        // Independently reimplemented SequencePairEngine::mutate flag-off branch (verbatim shape,
        // not a call into the engine).
        std::uniform_int_distribution<int> move_dist(0, 4);
        int move = move_dist(reference_rng);
        if (move <= 2 && reference.sequence_pos.size() >= 2) {
            std::uniform_int_distribution<int> pick(0, static_cast<int>(reference.sequence_pos.size()) - 1);
            int a = pick(reference_rng);
            int b = pick(reference_rng);
            if (move == 0 || move == 2) std::swap(reference.sequence_pos[a], reference.sequence_pos[b]);
            if (move == 1 || move == 2) std::swap(reference.sequence_neg[a], reference.sequence_neg[b]);
        }

        assert(actual.sequence_pos == reference.sequence_pos);
        assert(actual.sequence_neg == reference.sequence_neg);
    }
    std::cout << "testSPFlagOffByteIdenticalToReimplementedReference ok (1000 mutations)\n";
}

void testBStarFlagOffByteIdenticalToReimplementedReference() {
    Problem problem = makeFiveMovableBlockProblem();
    std::unique_ptr<PlacementEngine> engine = makePlacementEngine("bstar");  // move_portfolio_v2 default false
    std::mt19937 seed_rng(4242);
    PlacementState initial = engine->initialState(problem, seed_rng);
    PlacementState actual = initial;
    PlacementState reference = initial;

    std::mt19937 actual_rng(777);
    std::mt19937 reference_rng(777);
    for (int i = 0; i < 1000; ++i) {
        engine->mutate(problem, actual, actual_rng);

        // Independently reimplemented BStarEngine::mutate flag-off branch (verbatim shape).
        std::uniform_int_distribution<int> move_dist(0, 3);
        int move = move_dist(reference_rng);
        int n = static_cast<int>(reference.nodes.size());
        if (move == 0 && n >= 2) {
            std::uniform_int_distribution<int> pick(0, n - 1);
            int a = pick(reference_rng);
            int b = pick(reference_rng);
            std::swap(reference.nodes[a], reference.nodes[b]);
        } else if (move == 1 && n >= 2) {
            referenceMoveLeafCopy(reference, reference_rng);
        }

        assert(actual.nodes == reference.nodes);
        assert(actual.parent == reference.parent);
        assert(actual.left == reference.left);
        assert(actual.right == reference.right);
        assert(actual.root == reference.root);
    }
    std::cout << "testBStarFlagOffByteIdenticalToReimplementedReference ok (1000 mutations)\n";
}

// ---- (e) infeasible-fallback contract: flag-on mutate always yields a valid, decodable state ------

void testFlagOnMutateAlwaysValidFuzz(const std::string& engine_name) {
    Problem problem = parseProblem("test/2026 ICCAD_case.case0.csv");
    std::unique_ptr<PlacementEngine> engine = makePlacementEngine(engine_name, /*move_portfolio_v2=*/true);
    std::mt19937 rng(31415);
    PlacementState state = engine->initialState(problem, rng);
    for (int i = 0; i < 1000; ++i) {
        engine->mutate(problem, state, rng);
        assert(engine->isValidState(problem, state));
        Placement place = engine->decode(problem, state);
        assert(place.rects.size() == problem.blocks.size());
    }
    std::cout << "testFlagOnMutateAlwaysValidFuzz(" << engine_name << ") ok (1000 mutations)\n";
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
    testAbutmentCreditFullAtExactTouch();
    testAbutmentCreditZeroAtRampThreshold();
    testAbutmentCreditMidRampMonotonicity();
    testAbutmentCreditFlagOffByteIdenticalOnNontrivialFixture();
    testDefaultVariantsRegistryHasEightLegsInOrder();
    // B5c (move-portfolio upgrade, .superpowers/sdd/b5-design.md §4)
    testWireMedianTargetEvenCountTieUsesLowerMedian();
    testWireMedianTargetWeightsByNetCount();
    testWireMedianTargetNoBundlesReturnsOwnCenter();
    testAbutmentSideRealizesPortLegalTouch("sp");
    testAbutmentSideRealizesPortLegalTouch("bstar");
    testWorstBlockPicksHighestBadnessBlock();
    testSPFlagOffByteIdenticalToReimplementedReference();
    testBStarFlagOffByteIdenticalToReimplementedReference();
    testFlagOnMutateAlwaysValidFuzz("sp");
    testFlagOnMutateAlwaysValidFuzz("bstar");
    std::cout << "b5a regressions passed\n";
    return 0;
}
