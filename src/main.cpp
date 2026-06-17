#include <cstdlib>
#include <algorithm>
#include <iostream>
#include <memory>
#include <string>

#include "io.hpp"
#include "placement_engine.hpp"
#include "search.hpp"
#include "validate.hpp"

using namespace efgr;

namespace {

int argInt(int argc, char** argv, const std::string& name, int fallback) {
    for (int i = 3; i + 1 < argc; ++i) {
        if (argv[i] == name) return std::atoi(argv[i + 1]);
    }
    return fallback;
}

double argDouble(int argc, char** argv, const std::string& name, double fallback) {
    for (int i = 3; i + 1 < argc; ++i) {
        if (argv[i] == name) return std::atof(argv[i + 1]);
    }
    return fallback;
}

std::string argString(int argc, char** argv, const std::string& name, const std::string& fallback) {
    for (int i = 3; i + 1 < argc; ++i) {
        if (argv[i] == name) return argv[i + 1];
    }
    return fallback;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "usage: " << argv[0]
                  << " input.csv output.cfg [--engine sp|bstar|hybrid] [--iters N] [--seed N] [--alpha X]"
                  << " [--exact-interval N]\n";
        return 2;
    }

    try {
        Problem problem = parseProblem(argv[1]);
        SearchOptions options;
        options.iters = argInt(argc, argv, "--iters", 1000);
        options.seed = argInt(argc, argv, "--seed", 1);
        options.exact_interval = std::max(1, argInt(argc, argv, "--exact-interval", 50));
        double alpha = argDouble(argc, argv, "--alpha", problem.alpha);
        problem.alpha = alpha;
        std::string engine_name = argString(argc, argv, "--engine", "bstar");

        SearchResult result;
        if (engine_name == "hybrid") {
            result = runHybridPlacementSearch(problem, options);
        } else {
            std::unique_ptr<PlacementEngine> engine = makePlacementEngine(engine_name);
            result = runPlacementSearch(problem, *engine, options);
        }

        auto validation = validateSolution(problem, result.best_placement, result.best_routing);
        writeCfg(argv[2], problem, result.best_placement, result.best_routing);

        std::cerr << "engine=" << result.engine_name
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
