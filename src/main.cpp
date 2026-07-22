#include <cstdlib>
#include <algorithm>
#include <iomanip>
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
                  << " [--exact-interval N]\n";
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
        writeCfg(output_path, problem, result.best_placement, result.best_routing);

        // Beta Task 3: 12 significant figures (was the ostream default 6) so the self-consistency
        // regression can compare solver-reported score/wirelength/overflow against efgr_rescore's
        // output at <=1e-6 relative without display quantization masking the true agreement.
        std::cerr << std::setprecision(12)
                  << "engine=" << result.engine_name
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
