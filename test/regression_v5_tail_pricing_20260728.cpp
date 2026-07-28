// V5 tail-aware marginal overflow pricing (Codex V5-port review finding #2; docs/research/
// 2026-07-28-alpha-v5-cost-recalculation-analysis.md section 6): the official V5 overflow penalty
// is piecewise -- linear up to kOverflowRateThreshold, then a sqrt tail -- but the router's
// per-overflow-net price p_ovf (routing.cpp) and respace's capacity-gap profit key (respace.cpp)
// historically priced marginal overflow at the FLAT linear derivative kBetaLin*A/C regardless of
// the current global R. This suite pins the corrected pricing:
//   - roundOverflowPrice (route_cost.hpp): three regimes. (O+1)/C <= theta -> bit-identical to
//     the historical kBetaLin*A/C expression. Theta straddle -> the exact one-net secant of
//     officialOverflowPenalty (the entry cliff, finite at theta+ where the analytic sqrt
//     derivative diverges). Past theta -> the exact AVERAGE officialOverflowPenalty(R)/O, NOT
//     the endpoint one-net secant: each negotiation round rebuilds its overflow from ZERO, and
//     on a concave penalty the endpoint marginal undercharges a from-zero rebuild
//     (O*P'(O) << P(O)) -- measured on the p2 ChannelVsFt fixture, endpoint pricing steered the
//     router to a strictly worse official outcome (all-channel 3.02M vs 2.36M); the average is
//     the unique flat per-net price whose round total equals the true penalty.
//   - routeProblem negotiation rounds re-price p_ovf from the PREVIOUS round's global overflow
//     (round 0 keeps the pristine linear expression -- byte-identical to the pre-refinement
//     router whenever every round stays in the linear regime, which is where every current
//     benchmark case sits, R <= 0.00906 << theta).
//   - capacityGapCandidates (respace.cpp): rate <= theta keeps the historical linear-coefficient
//     profit VERBATIM (bit-identical); rate > theta prices each candidate by the exact piecewise
//     penalty delta (handles the downward theta-straddle cliff a linear coefficient cannot see).
#include <cassert>
#include <cmath>
#include <iostream>
#include <vector>

#include "../src/model.hpp"
#include "../src/respace.hpp"
#include "../src/route_cost.hpp"
#include "../src/routing.hpp"

namespace {
using namespace efgr;

constexpr double kTol = 1e-6;

Block makeBlock(const std::string& name, double w, double h) {
    Block b;
    b.name = name;
    b.width = w;
    b.height = h;
    b.area = w * h;
    b.type = BlockType::Macro;
    return b;
}

// ---- marginalOverflowPrice: linear regime is bit-identical to the historical rate ---------------

void testMarginalPriceLinearRegimeBitIdentical() {
    // While (O+1)/C stays <= theta the price must be EXACTLY the historical kBetaLin*A/C
    // expression (same operands, same order) -- asserted with ==, not a tolerance, because
    // routing.cpp's linear-regime behavior must stay byte-identical.
    const double A = 160000.0, C = 30250.0;
    assert(roundOverflowPrice(0.0, C, A) == kBetaLin * A / C);
    assert(roundOverflowPrice(1000.0, C, A) == kBetaLin * A / C);
    assert(roundOverflowPrice(1500.0, C, A) == kBetaLin * A / C);  // rate 0.0496, next 0.0496
    // Degenerate capacity: 0, matching routing.cpp's own raw_capacity_total > 0 guard.
    assert(roundOverflowPrice(5.0, 0.0, A) == 0.0);
    std::cout << "testMarginalPriceLinearRegimeBitIdentical ok\n";
}

// ---- marginalOverflowPrice: theta straddle is the exact piecewise delta (and much pricier) ------

void testMarginalPriceStraddlesThetaExactly() {
    const double A = 160000.0, C = 30250.0;
    // O/C = 0.0499901.. <= theta < (O+1)/C = 0.0500232.. -- the one-net secant crosses the kink.
    const double O = 1512.2;
    const double before = O / C, after = (O + 1.0) / C;
    assert(before <= kOverflowRateThreshold && after > kOverflowRateThreshold);
    const double got = roundOverflowPrice(O, C, A);
    const double want = officialOverflowPenalty(after, A) - officialOverflowPenalty(before, A);
    assert(got == want);
    // Crossing theta wakes the sqrt tail: ~11x the flat linear rate here (0.00742*A vs 0.000661*A).
    assert(got > kBetaLin * A / C + kTol);
    std::cout << "testMarginalPriceStraddlesThetaExactly ok\n";
}

void testMarginalPriceDeepTailIsExactAverageBetweenEndpointAndLinear() {
    const double A = 160000.0, C = 30250.0;
    const double O = 55000.0;  // rate 1.818 -- deep in the diminishing sqrt tail
    const double got = roundOverflowPrice(O, C, A);
    // The AVERAGE price P(R)/O (~8.71 here): rebuilding all O nets from zero at this flat rate
    // charges exactly the true piecewise penalty (cost recovery on a concave curve).
    const double want = officialOverflowPenalty(O / C, A) / O;
    assert(got == want);
    assert(got > 0.0);
    // Strict ordering pin: endpoint one-net secant (~2.98) < average (~8.71) < flat linear
    // (~105.8). Above the endpoint marginal -- pricing there undercharges a from-zero rebuild
    // (the p2 ChannelVsFt lesson); far below the linear rate -- deep-tail overflow is genuinely
    // cheaper than the old flat pricing claimed (analysis doc section 1.3).
    const double endpoint_secant =
        officialOverflowPenalty((O + 1.0) / C, A) - officialOverflowPenalty(O / C, A);
    assert(got > endpoint_secant + kTol);
    assert(got < kBetaLin * A / C - kTol);
    std::cout << "testMarginalPriceDeepTailIsExactAverageBetweenEndpointAndLinear ok\n";
}

// ---- routeProblem: negotiation rounds re-price p_ovf from the previous round's global R ---------

// Geometry (400x400 outline, all hard Macro blocks, alpha=0.05):
//
//   F1 = {0,0,50,400}      full-height left column          idx 0
//   F2 = {100,100,60,200}  middle column, y=[100,300]       idx 1
//   A2 = {50,0,50,100}     bottom plug of strip x=[50,100]  idx 2
//   B2 = {50,300,50,100}   top plug of the same strip       idx 3
//
// extractChannels x-strips ->
//   CH_A  = {50,100,50,200}   (between A2/B2; F1 left, F2 right)  Hcap 5000, Vcap 1250
//   CH_RB = {100,0,60,100}    (below F2)                          Hcap 2500
//   CH_RT = {100,300,60,100}  (above F2)                          Hcap 2500
//   CH_E  = {160,0,240,400}   (open east region)                  Hcap 10000, Vcap 6000
//   C_total = 25*[(200+50)+(100+60)+(100+60)+(400+240)] = 30250
//
// bundle1 = F1->F2, 60000 nets: the ONLY path is F1->CH_A->F2 (horizontal axis), so every round
// is forced to commit 60000 against Hcap 5000 -> 55000 overflow nets, R = 55000/30250 = 1.818
// (deep tail). bundle2 = A2->B2, 2000 nets, two paths:
//   short:  A2->CH_A->B2 (vertical axis, center-dist 300/net, guide-WL 200/net) -- 750 V-overflow
//   detour: A2->CH_RB->CH_E->CH_RT->B2 (center-dist 710/net, guide-WL 420/net) -- overflow-free
// Flat linear pricing (p_ovf = 20*A/C = 105.8): short costs 0.05*2000*300 + 750*105.8 = 109,339
// vs detour 0.05*2000*710 = 71,000 -> every round detours; overflow stays exactly 55000. The
// legacy gate pass also detours (10000-weight congestion on the jammed CH_A), and roundCostOfficial
// ties best-vs-legacy at WL 3.84M / overflow 55000 -> the pre-refinement router returns 55000.
// Tail-aware rounds >= 1 re-price p_ovf at R=1.818 -> average price P(R)/O = 8.71: short costs
// 30,000 + 750*8.71 = 36,534 < 71,000 -> bundle2 takes CH_A; overflow 55750, WL 3.4M. That round
// wins roundCostOfficial (0.05*3.4M + P(1.843) = 651,364 < 671,135), so the refined router must
// return overflow EXACTLY 55750 = 55000 (forced H) + 750 (chosen V) with the short-path WL.
void testNegotiationRepricesTailRoundsFromPreviousGlobalR() {
    Problem p;
    p.blocks = {makeBlock("F1", 50, 400), makeBlock("F2", 60, 200), makeBlock("A2", 50, 100),
                makeBlock("B2", 50, 100)};
    p.block_index = {{"F1", 0}, {"F2", 1}, {"A2", 2}, {"B2", 3}};
    p.outline_w = 400;
    p.outline_h = 400;
    p.alpha = 0.05;
    Bundle b1;
    b1.source = 0;
    b1.target = 1;
    b1.nets = 60000;
    Bundle b2;
    b2.source = 2;
    b2.target = 3;
    b2.nets = 2000;
    p.bundles = {b1, b2};

    Placement pl;
    pl.rects = {Rect{0, 0, 50, 400}, Rect{100, 100, 60, 200}, Rect{50, 0, 50, 100},
                Rect{50, 300, 50, 100}};
    pl.aspect = {1.0, 1.0, 1.0, 1.0};
    pl.outline_w = 400;
    pl.outline_h = 400;

    RoutingResult r = routeProblem(p, pl, /*cost_driven=*/true);
    assert(r.open_routes == 0);
    assert(!r.used_fallback);
    assert(std::abs(r.channel_capacity_total - 30250.0) < kTol);
    // 55000 forced (bundle1) + 750 chosen (bundle2 short path) -- flat linear pricing leaves this
    // at exactly 55000 (bundle2 detours every round).
    assert(std::abs(r.channel_overflow_nets - 55750.0) < kTol);
    // Short-path WL (~3.4M), not the detour shape (~3.84M).
    assert(r.total_wirelength > 3000000.0);
    assert(r.total_wirelength < 3600000.0);

    // Determinism: a second identical call reproduces the identical result.
    RoutingResult r2 = routeProblem(p, pl, /*cost_driven=*/true);
    assert(r2.channel_overflow_nets == r.channel_overflow_nets);
    assert(r2.total_wirelength == r.total_wirelength);
    assert(r2.routes.size() == r.routes.size());
    std::cout << "testNegotiationRepricesTailRoundsFromPreviousGlobalR ok (overflow="
              << r.channel_overflow_nets << ", wl=" << r.total_wirelength << ")\n";
}

// ---- capacityGapCandidates: linear regime stays bit-identical to the B7 coefficient key --------

void testCapacityGapProfitsLinearRegimeBitIdentical() {
    // Same shape as regression_b7_20260725.cpp's count fixture: rate = 100/1e5 = 1e-3 (deep
    // linear). Profits must equal the HISTORICAL linear-coefficient formula EXACTLY (==):
    // coeff_c = kBetaLin*A*rate/C, coeff_a = 1 + kBetaLin*rate, profit = coeff_c*delta_c -
    // delta_a*coeff_a with ample slack making delta_a = 0 for every candidate.
    Problem p;
    p.blocks = {makeBlock("L", 50, 300), makeBlock("R", 50, 300), makeBlock("P", 50, 100),
                makeBlock("Q", 50, 100)};
    p.outline_w = 1000;
    p.outline_h = 1000;
    p.alpha = 1.0;
    Placement pl;
    pl.rects = {Rect{0, 0, 50, 300}, Rect{300, 0, 50, 300}, Rect{500, 0, 50, 100},
                Rect{500, 300, 50, 100}};
    pl.aspect = {1.0, 1.0, 1.0, 1.0};
    pl.outline_w = 1000;
    pl.outline_h = 1000;

    RoutingResult routing;
    Channel ch_lo, ch_hi, ch_pq;
    ch_lo.rect = Rect{50, 0, 250, 100};
    ch_hi.rect = Rect{50, 150, 250, 100};
    ch_pq.rect = Rect{500, 100, 50, 200};
    routing.channels = {ch_lo, ch_hi, ch_pq};
    routing.channel_overflow_nets = 100.0;
    routing.channel_capacity_total = 100000.0;

    std::vector<CapacityGapCandidate> cands = capacityGapCandidates(p, pl, routing);
    assert(cands.size() == 6);
    const double A = pl.outline_w * pl.outline_h;
    const double rate = routing.channel_overflow_nets / routing.channel_capacity_total;
    assert(rate <= kOverflowRateThreshold);
    const double coeff_c = kBetaLin * A * rate / routing.channel_capacity_total;
    const double coeff_a = 1.0 + kBetaLin * rate;
    for (const CapacityGapCandidate& c : cands) {
        const double count = (c.from == 0 && c.to == 1) ? 2.0 : 1.0;
        const double delta_c = 25.0 * c.delta * count;
        const double delta_a = 0.0;  // slack (450 / 600) >> every delta
        assert(c.profit == coeff_c * delta_c - delta_a * coeff_a);  // bit-identical, not approximate
    }
    std::cout << "testCapacityGapProfitsLinearRegimeBitIdentical ok\n";
}

// ---- capacityGapCandidates: tail regime prices candidates by the exact piecewise delta ---------

// Single channel resolving to BOTH a (L,R,horizontal) and an (A,B,vertical) pair, b7-style, with
// asymmetric outline slack: slack_w = 0 (horizontal candidates pay delta_a = delta) and
// slack_h = 100 (vertical candidates stay delta_a = 0). A = 200*400 = 80000.
std::vector<CapacityGapCandidate> buildTailFixture(Problem& p, Placement& pl, RoutingResult& routing,
                                                   double overflow_nets, double capacity_total) {
    p.blocks = {makeBlock("A", 100, 100), makeBlock("B", 100, 100), makeBlock("L", 50, 300),
                makeBlock("R", 50, 300)};
    p.outline_w = 200;
    p.outline_h = 400;
    p.alpha = 1.0;
    pl.rects = {Rect{50, 0, 100, 100}, Rect{50, 200, 100, 100}, Rect{0, 0, 50, 300},
                Rect{150, 0, 50, 300}};
    pl.aspect = {1.0, 1.0, 1.0, 1.0};
    pl.outline_w = 200;
    pl.outline_h = 400;

    Channel ch;
    ch.rect = Rect{50, 100, 100, 100};  // left=L(2), right=R(3), below=A(0), above=B(1)
    routing.channels = {ch};
    routing.channel_overflow_nets = overflow_nets;
    routing.channel_capacity_total = capacity_total;
    return capacityGapCandidates(p, pl, routing);
}

void testCapacityGapProfitsDeepTailUseExactDelta() {
    Problem p;
    Placement pl;
    RoutingResult routing;
    // rate = 5000/5000 = 1.0 -- deep tail, where the flat linear coefficient overvalues added
    // capacity ~26x (linear dP/dC slope 20 vs sqrt-tail slope 1.5/(2*sqrt(0.95)) = 0.77).
    std::vector<CapacityGapCandidate> cands = buildTailFixture(p, pl, routing, 5000.0, 5000.0);
    assert(cands.size() == 6);

    const double A = pl.outline_w * pl.outline_h;
    const double O = routing.channel_overflow_nets;
    const double C = routing.channel_capacity_total;
    const double rate = O / C;
    assert(rate > kOverflowRateThreshold);
    const double penalty_before = officialOverflowPenalty(rate, A);
    const double coeff_c_linear = kBetaLin * A * rate / C;
    for (const CapacityGapCandidate& c : cands) {
        const double delta_c = 25.0 * c.delta;  // count == 1 for both pairs here
        const double delta_a = c.horizontal ? c.delta : 0.0;  // slack_w = 0, slack_h = 100
        const double want =
            penalty_before - officialOverflowPenalty(O / (C + delta_c), A + delta_a) - delta_a;
        assert(c.profit == want);  // exact piecewise delta, bit-identical composition
        // The old linear coefficient would have claimed coeff_c*delta_c (>= 6000 here); the true
        // deep-tail value is ~26x smaller.
        assert(c.profit < 0.1 * coeff_c_linear * delta_c);
        assert(c.profit > 0.0);
    }

    // Determinism across invocations.
    Problem p2;
    Placement pl2;
    RoutingResult routing2;
    std::vector<CapacityGapCandidate> again = buildTailFixture(p2, pl2, routing2, 5000.0, 5000.0);
    assert(again.size() == cands.size());
    for (std::size_t i = 0; i < cands.size(); ++i) {
        assert(again[i].from == cands[i].from && again[i].to == cands[i].to);
        assert(again[i].horizontal == cands[i].horizontal);
        assert(again[i].delta == cands[i].delta);
        assert(again[i].profit == cands[i].profit);
    }
    std::cout << "testCapacityGapProfitsDeepTailUseExactDelta ok\n";
}

void testCapacityGapProfitsThetaStraddleSeesTheCliff() {
    Problem p;
    Placement pl;
    RoutingResult routing;
    // rate = 250.5/5000 = 0.0501, just past theta: adding capacity pulls R BELOW theta, killing
    // the whole sqrt tail -- the exact delta (~1439 for delta_c=25) is ~3.6x the old linear
    // estimate (~401), an incentive cliff the flat coefficient cannot see.
    std::vector<CapacityGapCandidate> cands = buildTailFixture(p, pl, routing, 250.5, 5000.0);
    assert(cands.size() == 6);

    const double A = pl.outline_w * pl.outline_h;
    const double O = routing.channel_overflow_nets;
    const double C = routing.channel_capacity_total;
    const double rate = O / C;
    assert(rate > kOverflowRateThreshold);
    const double penalty_before = officialOverflowPenalty(rate, A);
    const double coeff_c_linear = kBetaLin * A * rate / C;
    bool saw_downward_straddle = false;
    for (const CapacityGapCandidate& c : cands) {
        const double delta_c = 25.0 * c.delta;
        const double delta_a = c.horizontal ? c.delta : 0.0;
        const double want =
            penalty_before - officialOverflowPenalty(O / (C + delta_c), A + delta_a) - delta_a;
        assert(c.profit == want);
        const double linear_claim = coeff_c_linear * delta_c - delta_a * (1.0 + kBetaLin * rate);
        if (O / (C + delta_c) <= kOverflowRateThreshold) {
            saw_downward_straddle = true;
            // Killing the sqrt tail (kBetaTail*sqrt(1e-4)*A ~= 1200 here) is a one-time bonus the
            // linear coefficient never sees: every straddling candidate out-earns its linear claim
            // by ~+1000 (the bonus minus the coefficient's own linear-segment overshoot)...
            assert(c.profit > linear_claim + 900.0);
            // ...and for the smallest capacity step the true value is ~3.6x the linear claim.
            if (c.delta == 1.0) assert(c.profit > 3.0 * linear_claim);
        }
    }
    assert(saw_downward_straddle);
    std::cout << "testCapacityGapProfitsThetaStraddleSeesTheCliff ok\n";
}

}  // namespace

int main() {
    testMarginalPriceLinearRegimeBitIdentical();
    testMarginalPriceStraddlesThetaExactly();
    testMarginalPriceDeepTailIsExactAverageBetweenEndpointAndLinear();
    testNegotiationRepricesTailRoundsFromPreviousGlobalR();
    testCapacityGapProfitsLinearRegimeBitIdentical();
    testCapacityGapProfitsDeepTailUseExactDelta();
    testCapacityGapProfitsThetaStraddleSeesTheCliff();
    std::cout << "v5 tail-pricing regressions passed\n";
    return 0;
}
