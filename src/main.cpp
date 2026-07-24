#include <cstdlib>
#include <algorithm>
#include <iomanip>
#include <iostream>
#include <memory>
#include <string>

#include "io.hpp"
#include "placement_engine.hpp"
#include "rank_stats.hpp"
#include "search.hpp"
#include "validate.hpp"

using namespace efgr;

namespace {

int argInt(int argc, char** argv, const std::string& name, int fallback) {
    for (int i = 0; i + 1 < argc; ++i) {
        if (argv[i] == name) return std::atoi(argv[i + 1]);
    }
    return fallback;
}

double argDouble(int argc, char** argv, const std::string& name, double fallback) {
    for (int i = 0; i + 1 < argc; ++i) {
        if (argv[i] == name) return std::atof(argv[i + 1]);
    }
    return fallback;
}

std::string argString(int argc, char** argv, const std::string& name, const std::string& fallback) {
    for (int i = 0; i + 1 < argc; ++i) {
        if (argv[i] == name) return argv[i + 1];
    }
    return fallback;
}

bool hasFlag(int argc, char** argv, const std::string& name) {
    for (int i = 1; i < argc; ++i) {
        if (argv[i] == name) return true;
    }
    return false;
}

std::string defaultOutputPath(const std::string& input_path) {
    const std::size_t slash = input_path.find_last_of("/\\");
    const std::size_t dot = input_path.find_last_of('.');
    if (dot != std::string::npos && (slash == std::string::npos || dot > slash)) {
        return input_path.substr(0, dot) + ".cfg";
    }
    return input_path + ".cfg";
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "usage: " << argv[0]
                  << " input.csv [output.cfg] [--engine sp|bstar|hybrid] [--iters N] [--seed N] [--alpha X]"
                  << " [--exact-interval N] [--time-budget S] [--adaptive-interval]\n";
        return 2;
    }

    try {
        const std::string input_path = argv[1];
        const bool has_output_arg = argc >= 3 && std::string(argv[2]).rfind("--", 0) != 0;
        const std::string output_path = has_output_arg ? argv[2] : defaultOutputPath(input_path);
        Problem problem = parseProblem(argv[1]);
        SearchOptions options;
        options.iters = argInt(argc, argv, "--iters", 1000);
        options.seed = argInt(argc, argv, "--seed", 1);
        options.exact_interval = std::max(1, argInt(argc, argv, "--exact-interval", 50));
        // Beta Task 5: baked sweep-selected beta defaults. A no-flag run uses the chosen wall-clock
        // budget (time is the binding stop); an explicit --iters WITHOUT --time-budget selects
        // deterministic iteration-bounded search (budget OFF) so every regression suite stays
        // byte-reproducible. An explicit --time-budget always wins. kDefaultTimeBudgetSec and the
        // --engine fallback below are the two sweep-selected knobs.
        //
        // scripts/sweep.sh over the 5 official cases: the iters=1000 reference (budget 0) minimizes
        // checker_cost for BOTH engines. A wall-clock budget raises the iteration count, but the
        // elite-pass cheap-score selection mis-ranks candidates under integer geometry (Task 4 known
        // issue), so more iters inflate routed overflow and REGRESS checker_cost (e.g. hybrid case4
        // 113.98M -> 162.87M at budget 15; bstar Sigma 273.5M -> ~325M). Best measured cell:
        // engine=hybrid, budget 0 (Sigma checker_cost 252.7M vs bstar 273.5M). So the budget defaults
        // OFF; --time-budget stays available for when that search interaction is fixed post-beta.
        constexpr double kDefaultTimeBudgetSec = 0.0;
        const bool has_iters = hasFlag(argc, argv, "--iters");
        const bool has_budget = hasFlag(argc, argv, "--time-budget");
        options.time_budget_sec = (has_iters && !has_budget)
                                      ? 0.0
                                      : argDouble(argc, argv, "--time-budget", kDefaultTimeBudgetSec);
        // B6 (flag-gated adaptive interval, .superpowers/sdd/b6-brief.md): DEFAULT OFF -- absent,
        // runPlacementSearch's exact_interval stays constant exactly as before B6 (hard byte-
        // identity acceptance gate). See SearchOptions.adaptive_interval's doc comment (search.hpp).
        options.adaptive_interval = hasFlag(argc, argv, "--adaptive-interval");
        double alpha = argDouble(argc, argv, "--alpha", problem.alpha);
        problem.alpha = alpha;
        std::string engine_name = argString(argc, argv, "--engine", "hybrid");

        // B4.1 (route-pricing portfolio, b4-report.md): runs the requested engine under both the
        // official (B4) and retained pre-B4 router pricing, folding by value -- restores the
        // per-case <=-pre-B4 Pareto floor. Subsumes the old inline hybrid-vs-single-engine dispatch
        // (runPricingPortfolio does the same "hybrid" vs makePlacementEngine(engine_name) split
        // internally, once per pricing leg).
        SearchResult result = runPricingPortfolio(problem, engine_name, options);

        auto validation = validateSolution(problem, result.best_placement, result.best_routing);
        writeCfg(output_path, problem, result.best_placement, result.best_routing);

        // B6 (rank-fidelity instrumentation, .superpowers/sdd/b6-brief.md): overall/windowed
        // Kendall tau over the SA-loop's recorded (cheap, exact) pairs, tau over the elite pool's
        // own snapshot, and where in the cheap-sorted pool the exact-best member actually sits.
        // `result` is whichever leg runPricingPortfolio/runHybridPlacementSearch picked (see their
        // fold-site comments in search.cpp), so these describe the PICKED leg's rank fidelity only,
        // not a combination across legs.
        // B5a (.superpowers/sdd/b5-design.md §2): pool_tau/exact_best_cheap_rank now report over the
        // WIDENED K=48 cheap prefilter (search.cpp's top_candidates), a diagnostic on the cheap
        // proxy's own rank fidelity -- not the (separate, tight-exact-reranked) set that actually
        // received the expensive elite treatment. See SearchResult.elite_pairs's doc comment
        // (search.hpp) for the full field-semantics note.
        double tau = kendallTauB(result.fidelity_pairs);
        double tau_w = windowedTauB(result.fidelity_pairs);
        double pool_tau = kendallTauB(result.elite_pairs);
        EliteDiagnostics elite_diag = eliteDiagnostics(result.elite_pairs);

        // Beta Task 3: 12 significant figures (was the ostream default 6) so the self-consistency
        // regression can compare solver-reported score/wirelength/overflow against efgr_rescore's
        // output at <=1e-6 relative without display quantization masking the true agreement.
        std::cerr << std::setprecision(12)
                  << "engine=" << result.engine_name
                  << " pricing=" << (result.pricing_official ? "official" : "legacy")
                  << " admission=" << (result.elite_admission_v2 ? "v2" : "v1")
                  << " credit=" << (result.abutment_credit ? "on" : "off")
                  << " moves=" << (result.move_portfolio_v2 ? "v2" : "v1")
                  << " budget_phase2=" << (result.budget_phase2 ? "yes" : "no")
                  << " tau=" << tau
                  << " tau_w=" << tau_w
                  << " pool_tau=" << pool_tau
                  << " exact_best_cheap_rank=" << elite_diag.exact_best_cheap_rank << "/" << elite_diag.pool_size
                  << " iters=" << result.iterations
                  << " exact_evals=" << result.exact_evaluations
                  << " blocks=" << problem.blocks.size()
                  << " bundles=" << problem.bundles.size()
                  << " channels=" << result.best_routing.channels.size()
                  << " routes=" << result.best_routing.routes.size()
                  << " score=" << result.best_score.value
                  << " outline_area=" << result.best_score.outline_area
                  << " wirelength=" << result.best_score.wirelength
                  << " channel_overflow=" << result.best_routing.channel_overflow
                  << " channel_overflow_nets=" << result.best_routing.channel_overflow_nets
                  << " feedthrough_mismatch=" << result.best_routing.feedthrough_mismatch
                  << " open_routes=" << result.best_routing.open_routes
                  << " validation=" << (validation.ok ? "ok" : "fail") << "\n";
        for (const auto& msg : validation.messages) std::cerr << "validate: " << msg << "\n";
        return validation.ok && result.best_routing.open_routes == 0 ? 0 : 1;
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << "\n";
        return 1;
    }
}
