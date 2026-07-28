#include <cstdio>

#include "../src/cfg_score.hpp"
#include "../src/io.hpp"

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: rescore input.csv output.cfg\n");
        return 2;
    }
    efgr::Problem problem = efgr::parseProblem(argv[1]);
    efgr::CfgSolution cfg = efgr::parseCfg(argv[2]);
    efgr::OfficialBreakdown b = efgr::officialScoreCfg(problem, cfg);
    // Machine-readable single line; the benchmark script derives ranking cost from these fields.
    std::printf("area=%.3f wl=%.3f overflow=%.3f capacity=%.3f rate=%.9f "
                "overflow_penalty=%.3f ft_penalty=%.3f edge_location_penalty=%.3f checker_cost=%.3f alpha=%.3f\n",
                b.outline_area, b.wirelength, b.overflow_nets, b.capacity_nets, b.rate,
                b.overflow_penalty, b.ft_penalty, b.edge_location_penalty, b.checker_cost, problem.alpha);
    return 0;
}
