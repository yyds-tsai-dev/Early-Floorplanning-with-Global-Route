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

// F1 decision (2026-07-29 sweep, docs to follow): the contest organizer's harness invokes the
// binary BARE -- no flags at all (confirmed by docs/official/alpha_test/*/run.log) -- so main()
// self-imposes this budget whenever NEITHER --iters nor --time-budget is explicitly passed. Every
// explicit invocation (regression suites, scripts/benchmark.sh, scripts/sweep.sh, manual debugging)
// keeps its own semantics untouched. Hoisted to namespace scope so both the usage banner and the
// parsing block in main() reference the same literal. See the parsing block for the full 3-mode
// rationale.
constexpr double kBareInvocationTimeBudgetSec = 600.0;

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
                  << " [--exact-interval N] [--time-budget S] [--adaptive-interval]\n"
                  << "  F1: a BARE call (neither --iters nor --time-budget) self-imposes a "
                  << kBareInvocationTimeBudgetSec << "s time budget; pass --time-budget 0 for the"
                  << " old iters-only default, or --iters N for an explicit iteration bound (budget"
                  << " off).\n";
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
        // Beta Task 5 (bare-invocation default superseded by F1 below): baked sweep-selected beta
        // defaults. An explicit --iters WITHOUT --time-budget selects deterministic iteration-bounded
        // search (budget OFF) so every regression suite stays byte-reproducible -- unchanged by F1.
        // An explicit --time-budget always wins, any value including 0 -- unchanged by F1.
        //
        // scripts/sweep.sh over the 5 official cases (original Task 5 measurement, budgets <=15-90s):
        // the iters=1000 reference (budget 0) minimized checker_cost for both engines at those short
        // budgets -- the elite-pass cheap-score selection mis-ranks candidates under integer geometry
        // (Task 4 known issue), so a short budget raised iteration count without fixing that mis-rank,
        // inflating routed overflow (e.g. hybrid case4 113.98M -> 162.87M at budget 15).
        //
        // F1 (2026-07-29 sweep, docs to follow): re-ran the sweep to LARGER budgets and found Sigma
        // rank_cost is actually monotone-DECREASING in time budget, 600s best measured (Sigma checker
        // 153.93M -> 138.82M, Sigma rank 120.20M -> 115.79M). The Task 4 mis-rank risk above is
        // structurally capped by B2's value-gated portfolio fold (runPricingPortfolio /
        // PortfolioFold::admit, search.hpp/.cpp): phase 1 of a budgeted run is ALWAYS the plain
        // budget-off default computed first and folded in, so spending a budget can never make the
        // folded result worse than the default, only possibly better. The contest organizer's harness
        // invokes the binary BARE -- no flags at all (docs/official/alpha_test/*/run.log) -- so a bare
        // run now self-imposes kBareInvocationTimeBudgetSec (600s) to capture this gain: 8 cases x
        // ~600s fits the 2h contest limit with ~33% margin, and B2's anytime output (the on_improved
        // callback below) covers any overrun by leaving the latest fold-best .cfg on disk even if the
        // process is killed mid-budget. kDefaultTimeBudgetSec and the --engine fallback below are the
        // two sweep-selected knobs.
        constexpr double kDefaultTimeBudgetSec = 0.0;
        const bool has_iters = hasFlag(argc, argv, "--iters");
        const bool has_budget = hasFlag(argc, argv, "--time-budget");
        if (has_budget) {
            options.time_budget_sec = argDouble(argc, argv, "--time-budget", kDefaultTimeBudgetSec);
        } else if (has_iters) {
            options.time_budget_sec = 0.0;
        } else {
            options.time_budget_sec = kBareInvocationTimeBudgetSec;
        }
        // F1 regression probe (test/run_f1_regressions.sh): cheap marker printed BEFORE the
        // (potentially long, up to 600s) portfolio search runs, so a test can timeout-kill a bare
        // invocation and still observe which parsing mode was selected without waiting out the
        // budget. Field names deliberately avoid every substring the existing regression/benchmark/
        // sweep scripts already grep for out of the FINAL summary line below (score=, wirelength=,
        // channel_overflow_nets=, open_routes=, validation=, engine=, iters=<digit>).
        std::cerr << "budget_parse: iters_explicit=" << (has_iters ? 1 : 0)
                  << " time_budget_explicit=" << (has_budget ? 1 : 0)
                  << " resolved_time_budget_sec=" << options.time_budget_sec << "\n";
        // B6 (flag-gated adaptive interval, .superpowers/sdd/b6-brief.md): DEFAULT OFF -- absent,
        // runPlacementSearch's exact_interval stays constant exactly as before B6 (hard byte-
        // identity acceptance gate). See SearchOptions.adaptive_interval's doc comment (search.hpp).
        options.adaptive_interval = hasFlag(argc, argv, "--adaptive-interval");
        double alpha = argDouble(argc, argv, "--alpha", problem.alpha);
        problem.alpha = alpha;
        std::string engine_name = argString(argc, argv, "--engine", "hybrid");
        // G4 (edge-LOCATION soft-anchor exploratory lever, docs/research/2026-07-28-alpha-v5-cost-
        // recalculation-analysis.md): hidden/undocumented measurement flag -- intentionally NOT in
        // the usage banner above (a flag-gated exploration, not a supported user-facing knob yet).
        // Default false (Problem.edge_soft_anchor's own default, model.hpp), so an absent flag keeps
        // every downstream path (search, eval, validate) byte-identical to pre-G4.
        problem.edge_soft_anchor = hasFlag(argc, argv, "--edge-soft-anchor");
        // L6 (undirected bundle merge, docs/research/2026-07-30-final-levers-design.md): hidden/
        // undocumented measurement flag, same precedent as --edge-soft-anchor above -- intentionally
        // NOT in the usage banner. Default false (Problem.undirected_bundles's own default,
        // model.hpp), so an absent flag keeps parseProblem's directed per-matrix-entry bundles and
        // every downstream consumer byte-identical to pre-L6. When set, merges each unordered
        // {source,target} pair into one Bundle (nets summed, direction normalized to min->max) AFTER
        // parsing -- parseProblem itself (io.cpp) is never touched.
        problem.undirected_bundles = hasFlag(argc, argv, "--undirected-bundles");
        if (problem.undirected_bundles) problem.bundles = mergeUndirectedBundles(problem.bundles);
        // L2 (edge-block true un-pinning / "edge-free" mode, docs/research/2026-07-30-final-levers-
        // design.md sections L2.1-L2.8): hidden/undocumented measurement flag, same precedent as
        // --edge-soft-anchor/--undirected-bundles above -- intentionally NOT in the usage banner.
        // Default false (Problem.edge_free's own default, model.hpp), so an absent flag keeps every
        // isAnchoredEdge(problem, i) call site byte-identical to isEdge(problem.blocks[i].type), i.e.
        // pre-L2 behavior. Also force-sets edge_soft_anchor=true: edge-free reuses G4's existing
        // machinery wholesale rather than duplicating it -- the exact-tier gamma penalty (now computed
        // by cheapScore, see eval.cpp) that prices an off-LOCATION edge block, and validate.cpp's
        // warning-level downgrade of an edge-LOCATION miss (instead of a hard FAIL), so a freed block
        // that lands off its declared LOCATION is priced and reported, not silently accepted or hard-
        // rejected.
        problem.edge_free = hasFlag(argc, argv, "--edge-free");
        if (problem.edge_free) problem.edge_soft_anchor = true;

        // B4.1 (route-pricing portfolio, b4-report.md): runs the requested engine under both the
        // official (B4) and retained pre-B4 router pricing, folding by value -- restores the
        // per-case <=-pre-B4 Pareto floor. Subsumes the old inline hybrid-vs-single-engine dispatch
        // (runPricingPortfolio does the same "hybrid" vs makePlacementEngine(engine_name) split
        // internally, once per pricing leg).
        // B2 (anytime output, .superpowers/sdd/b2-brief.md item 2): write the current best cfg to
        // `output_path` after EVERY leg (both phases) that improves runPricingPortfolio's running
        // fold-best, atomically (io.cpp's writeCfg, temp+rename) -- a killed/overrun run always
        // leaves a valid best-so-far cfg on disk. The unconditional writeCfg call below (after
        // runPricingPortfolio returns) always re-publishes the exact same bytes the LAST anytime
        // write already produced, so a normal-exit run's final on-disk file is unaffected by this
        // callback's existence.
        SearchResult result = runPricingPortfolio(
            problem, engine_name, options,
            [&](const Problem& p, const Placement& pl, const RoutingResult& rr) { writeCfg(output_path, p, pl, rr); });

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
                  << " seed=" << result.picked_seed
                  << " legs_run=" << result.legs_run
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
