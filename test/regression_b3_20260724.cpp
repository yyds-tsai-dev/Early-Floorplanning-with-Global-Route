// B3 (research doc 2026-07-23 §3a-b, capacity-denominator engineering): rung-generator + mechanism
// tests for outlineSweep's new integer-µm capacity-denominator micro-pad rungs (W-only/H-only/both,
// added to the tight frontier, alongside the pre-existing margin ladder). See
// .superpowers/sdd/b3-brief.md and .superpowers/sdd/b3-report.md for the full narrative.
// B3.1 (.superpowers/sdd/b31-report.md): also pins the post-finalize sweep pass's end-to-end effect
// on case2 (golden-rescored, the same oracle bin/efgr_rescore/run_cfg_score_regressions.sh use).
#include <cassert>
#include <cmath>
#include <cstdio>
#include <iostream>
#include <memory>
#include <random>
#include <string>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

#include "../src/cfg_score.hpp"
#include "../src/eval.hpp"
#include "../src/io.hpp"
#include "../src/model.hpp"
#include "../src/placement_engine.hpp"
#include "../src/search.hpp"

namespace {
using namespace efgr;

// Rung-generator unit test (brief item 1): probes every (W,H) target outlineSweep actually tries
// (post-dedupe) via a spy ExactCachedFn -- same technique as
// test/regression_p1_20260706.cpp's testOutlineSweepProbesDistinctOutlinesCase0, generalized to
// record every distinct target tried instead of just the distinct areas. A cheap bstar decode (no
// SA loop) on case4 gives a real placement with genuine tight-to-MAX slack on both axes, so pad
// targets are neither degenerate (clamped to a pre-existing rung) nor trivially absent.
void testOutlineSweepPadRungsGenerated() {
    Problem problem = parseProblem("test/2026 ICCAD_case.case4.csv");
    std::unique_ptr<PlacementEngine> engine = makePlacementEngine("bstar");
    std::mt19937 rng(1);
    PlacementState state = engine->initialState(problem, rng);
    Placement placement = engine->decode(problem, state);

    std::vector<std::pair<double, double>> probed;
    ExactCachedFn probe = [&](const Placement& p, RoutingResult* routing_out, Placement* placement_out) {
        probed.emplace_back(p.outline_w, p.outline_h);
        if (placement_out) *placement_out = p;  // probe never grows: effective == input
        RoutingResult routing;
        routing.open_routes = 0;
        Score score;
        score.outline_area = p.outline_w * p.outline_h;
        score.value = score.outline_area;  // smallest-area-wins is irrelevant here; only the probed
                                            // target set (not the winner) is under test
        if (routing_out) *routing_out = routing;
        return score;
    };

    OutlineSweepResult result = outlineSweep(problem, placement, probe);
    (void)result;

    // Independently recompute tight_w/h -- the same call outlineSweep itself makes internally --
    // to derive the expected margin-ladder/pad targets without depending on outlineSweep's own
    // internals for the expectation, only for the (already-captured) probed set.
    Placement tight = placement;
    tight.outline_w = 0.0;
    tight.outline_h = 0.0;
    OutlineFit tight_fit = shrinkWrapOutline(problem, tight);
    assert(tight_fit.legal);
    double tight_w = tight_fit.w;
    double tight_h = tight_fit.h;
    // Sanity: case4 actually has slack on both axes at this decode, otherwise pad targets would
    // degenerate to MAX and this test would not distinguish "pads present" from "pads clamped away".
    assert(tight_w + 64.0 < problem.outline_w - kEps);
    assert(tight_h + 64.0 < problem.outline_h - kEps);

    // 1. Every probed target is integral (A43 integer geometry).
    for (const auto& t : probed) {
        assert(std::abs(t.first - std::round(t.first)) < kEps);
        assert(std::abs(t.second - std::round(t.second)) < kEps);
    }

    // 2. Every probed target is <= MAX per axis.
    for (const auto& t : probed) {
        assert(t.first <= problem.outline_w + kEps);
        assert(t.second <= problem.outline_h + kEps);
    }

    // 3. No duplicate (W,H) targets (dedupe holds across the whole call, not just consecutively).
    for (std::size_t i = 0; i < probed.size(); ++i) {
        for (std::size_t j = i + 1; j < probed.size(); ++j) {
            bool same = std::abs(probed[i].first - probed[j].first) < kEps &&
                        std::abs(probed[i].second - probed[j].second) < kEps;
            assert(!same);
        }
    }

    auto contains = [&](double w, double h) {
        for (const auto& t : probed) {
            if (std::abs(t.first - w) < kEps && std::abs(t.second - h) < kEps) return true;
        }
        return false;
    };

    // 4. Existing margin-ladder targets unchanged: recompute the ORIGINAL formula independently
    // (byte-identical to search.cpp's) and assert every margin>0 rung is still probed exactly.
    // margin=0.0 (tight) is checked separately below: its own tryTarget branch re-invokes
    // shrinkWrapOutline (a re-bisecting minimizer, unlike anchorAtOutline's exact no-bisection
    // anchor that every other rung uses) on a ceiling of (tight_w,tight_h) -- pre-existing behavior
    // this task did not touch (the m=0.0 branch is byte-identical to the pre-B3 code, just moved
    // into the shared tryTarget lambda) -- which can legitimately re-converge to an EVEN TIGHTER
    // frontier than this tight_w/tight_h estimate (a smaller bisection search range gives the
    // tolerance-bounded bisection more relative precision; observed on case4 while writing this
    // test: outer tight=(5366,5065) but the m=0.0 rung's own reshrink actually lands at
    // (5366,5050) -- see b3-report.md). So the tight rung's actual probed target is only asserted
    // to be tight-or-tighter below, not byte-equal to this external single-pass estimate.
    static constexpr double kNonTightMargins[] = {0.02, 0.05, 0.10, 0.20, 0.50, 1.0};
    for (double m : kNonTightMargins) {
        double tw = std::min(problem.outline_w, std::ceil(tight_w + m * (problem.outline_w - tight_w) - kEps));
        double th = std::min(problem.outline_h, std::ceil(tight_h + m * (problem.outline_h - tight_h) - kEps));
        assert(contains(tw, th));
    }
    // The tight (m=0.0) rung is always probed first (kMargins starts at 0.0, tried before any
    // margin>0 rung or any pad) and, per the reshrink note above, only ever tight-or-tighter than
    // this single-pass estimate -- never looser (shrinkWrapOutline is a minimizer in both calls).
    assert(!probed.empty());
    assert(probed.front().first <= tight_w + kEps);
    assert(probed.front().second <= tight_h + kEps);

    // 5. Pads present: every one of the 7*3=21 pad-derived targets was actually probed (case4's
    // slack, asserted above, guarantees none of them clamp down to an already-present rung target).
    static constexpr double kPads[] = {1.0, 2.0, 4.0, 8.0, 16.0, 32.0, 64.0};
    int pad_targets_seen = 0;
    for (double p : kPads) {
        double pw = tight_w + p;
        double ph = tight_h + p;
        assert(contains(pw, tight_h));
        assert(contains(tight_w, ph));
        assert(contains(pw, ph));
        pad_targets_seen += 3;
    }
    assert(pad_targets_seen == 21);
    std::cout << "testOutlineSweepPadRungsGenerated ok (" << probed.size() << " distinct targets probed, "
               << pad_targets_seen << " of them pad-derived)\n";
}

// Mechanism test (brief item 2): case2 has nonzero overflow at default settings (R~=0.0072 per the
// research doc), so its post-search placement is exactly the kind of geometry the capacity-
// denominator lever targets. Runs the SAME default search bin/efgr's default flags use (hybrid via
// runHybridPlacementSearch, SearchOptions{} defaults: iters=1000, seed=1), then drives outlineSweep
// directly on the resulting best_placement with pads on vs off (the test-only `include_pads` escape
// hatch search.hpp documents for exactly this) and asserts the pads-on value is never worse --
// Pareto-safety by construction (pads-on tries a strict superset of pads-off's candidates). Measured
// while writing this test (see b3-report.md): pads-on lands at value=59,445,119 vs pads-off's
// 63,253,695 on this exact case/seed -- a real, non-marginal improvement, not just a technical tie.
void testOutlineSweepPadRungsNeverWorseCase2Default() {
    Problem problem = parseProblem("test/2026 ICCAD_case.case2.csv");
    SearchOptions options;  // defaults: iters=1000, seed=1, exact_interval=50, budget off
    SearchResult result = runHybridPlacementSearch(problem, options);
    assert(result.best_routing.open_routes == 0);

    std::unordered_map<std::string, std::tuple<Score, RoutingResult, Placement>> exact_cache;
    ExactCachedFn exactCached = [&](const Placement& p, RoutingResult* routing_out, Placement* placement_out) {
        std::string key = placementKey(p);
        auto it = exact_cache.find(key);
        if (it != exact_cache.end()) {
            if (routing_out) *routing_out = std::get<1>(it->second);
            if (placement_out) *placement_out = std::get<2>(it->second);
            return std::get<0>(it->second);
        }
        RoutingResult routing;
        Placement effective;
        Score score = exactScore(problem, p, &routing, &effective);
        exact_cache.emplace(key, std::make_tuple(score, routing, effective));
        if (routing_out) *routing_out = routing;
        if (placement_out) *placement_out = effective;
        return score;
    };

    OutlineSweepResult with_pads = outlineSweep(problem, result.best_placement, exactCached, true);
    OutlineSweepResult without_pads = outlineSweep(problem, result.best_placement, exactCached, false);

    assert(with_pads.routing.open_routes == 0);
    if (with_pads.score.value > without_pads.score.value + kEps) {
        std::cerr << "testOutlineSweepPadRungsNeverWorseCase2Default: with_pads=" << with_pads.score.value
                  << " without_pads=" << without_pads.score.value << "\n";
    }
    assert(with_pads.score.value <= without_pads.score.value + kEps);
    std::cout << "testOutlineSweepPadRungsNeverWorseCase2Default ok (with_pads=" << with_pads.score.value
               << " without_pads=" << without_pads.score.value << ")\n";
}

// B3.1 (.superpowers/sdd/b31-report.md): end-to-end pin. Runs the SAME default search bin/efgr's
// default flags use on case2 (hybrid, iters=1000, seed=1), writes the resulting cfg, and re-scores
// it through the golden rescorer (cfg_score.hpp -- the SAME oracle bin/efgr_rescore and
// run_cfg_score_regressions.sh use) -- asserting the OFFICIAL checker_cost never exceeds the
// pre-B3.1 baseline. Constant measured at HEAD 8e82204 (post-B1.1, pre-B3.1, i.e. before
// runPlacementSearch's post-finalize sweep pass existed): 65,625,655.415, identical to
// b3-report.md's own case2 "after" row and b1-report.md's sweep-matrix hybrid@0 case2 row. Small
// epsilon headroom (1.0 absolute, ~1.5e-8 relative at this magnitude) for 3-decimal .cfg rounding
// noise on the round-trip -- the same rounding-awareness run_selfconsistency_regressions.sh already
// documents, and far tighter than any real regression this pin exists to catch (measured deltas on
// this lever are in the hundreds-of-thousands to millions).
constexpr double kPreB31Case2DefaultCheckerCost = 65625655.42;

void testPostFinalizeSweepNeverExceedsPreB31Case2Default() {
    Problem problem = parseProblem("test/2026 ICCAD_case.case2.csv");
    SearchOptions options;  // defaults: iters=1000, seed=1, exact_interval=50, budget off
    SearchResult result = runHybridPlacementSearch(problem, options);
    assert(result.best_routing.open_routes == 0);

    const std::string tmp = "test/b31_case2_tmp.cfg";
    writeCfg(tmp, problem, result.best_placement, result.best_routing);
    CfgSolution cfg = parseCfg(tmp);
    OfficialBreakdown breakdown = officialScoreCfg(problem, cfg);
    std::remove(tmp.c_str());

    if (breakdown.checker_cost > kPreB31Case2DefaultCheckerCost + 1.0) {
        std::cerr << "testPostFinalizeSweepNeverExceedsPreB31Case2Default: checker_cost="
                  << breakdown.checker_cost << " exceeds pre-B3.1 baseline "
                  << kPreB31Case2DefaultCheckerCost << "\n";
    }
    assert(breakdown.checker_cost <= kPreB31Case2DefaultCheckerCost + 1.0);
    std::cout << "testPostFinalizeSweepNeverExceedsPreB31Case2Default ok (checker_cost="
               << breakdown.checker_cost << " <= " << kPreB31Case2DefaultCheckerCost << ")\n";
}

}  // namespace

int main() {
    testOutlineSweepPadRungsGenerated();
    testOutlineSweepPadRungsNeverWorseCase2Default();
    testPostFinalizeSweepNeverExceedsPreB31Case2Default();
    std::cout << "b3 regressions passed\n";
    return 0;
}
