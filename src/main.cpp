#include <cstdlib>
#include <iostream>
#include <random>
#include <string>

#include "eval.hpp"
#include "floorplan.hpp"
#include "io.hpp"
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

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "usage: " << argv[0] << " input.csv output.cfg [--iters N] [--seed N] [--alpha X] [--exact-interval N]\n";
        return 2;
    }

    try {
        Problem problem = parseProblem(argv[1]);
        int iters = argInt(argc, argv, "--iters", 1000);
        int seed = argInt(argc, argv, "--seed", 1);
        int exact_interval = std::max(1, argInt(argc, argv, "--exact-interval", 50));
        double alpha = argDouble(argc, argv, "--alpha", problem.alpha);
        problem.alpha = alpha;

        std::mt19937 rng(seed);
        BTreeState current = initialState(problem);
        Placement current_place = decodeState(problem, current);
        Score current_score = cheapScore(problem, current_place);
        BTreeState best_state = current;
        Placement best_place = current_place;
        RoutingResult best_routing;
        Score best_exact = exactScore(problem, best_place, &best_routing);

        double temp = 1000000.0;
        for (int iter = 1; iter <= iters; ++iter) {
            BTreeState candidate = current;
            mutateState(problem, candidate, rng);
            Placement cand_place = decodeState(problem, candidate);
            Score cand_score = cheapScore(problem, cand_place);
            double delta = cand_score.value - current_score.value;
            std::uniform_real_distribution<double> unit(0.0, 1.0);
            if (delta <= 0.0 || std::exp(-delta / std::max(1.0, temp)) > unit(rng)) {
                current = candidate;
                current_place = cand_place;
                current_score = cand_score;
            }
            if (iter % exact_interval == 0 || cand_score.value < best_exact.value) {
                RoutingResult routing;
                Score ex = exactScore(problem, cand_place, &routing);
                if (ex.value < best_exact.value) {
                    best_exact = ex;
                    best_state = candidate;
                    best_place = cand_place;
                    best_routing = routing;
                }
            }
            temp *= 0.995;
        }

        (void)best_state;
        auto validation = validateSolution(problem, best_place, best_routing);
        writeCfg(argv[2], problem, best_place, best_routing);

        std::cerr << "blocks=" << problem.blocks.size()
                  << " bundles=" << problem.bundles.size()
                  << " channels=" << best_routing.channels.size()
                  << " routes=" << best_routing.routes.size()
                  << " score=" << best_exact.value
                  << " wirelength=" << best_exact.wirelength
                  << " open_routes=" << best_routing.open_routes
                  << " validation=" << (validation.ok ? "ok" : "fail") << "\n";
        for (const auto& msg : validation.messages) std::cerr << "validate: " << msg << "\n";
        return validation.ok && best_routing.open_routes == 0 ? 0 : 1;
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << "\n";
        return 1;
    }
}
