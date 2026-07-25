#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."

make >/dev/null

# B2 seed-sweep budget phase + anytime output regressions (.superpowers/sdd/b2-brief.md): needs the
# same sibling set run_b5_regressions.sh/run_b6_regressions.sh use (runPricingPortfolio's own
# dependency chain) -- io.cpp for writeCfg/parseProblem (the anytime-write + real-case tests), no
# cfg_score.cpp (this suite compares raw .cfg bytes / SearchResult fields directly, not a
# golden-rescored checker_cost).
g++ -std=c++17 -O2 -Wall -Wextra -pedantic -I. \
    test/regression_b2_20260725.cpp \
    src/routing.cpp src/eval.cpp src/io.cpp src/validate.cpp \
    src/placement_engine.cpp src/search.cpp src/respace.cpp \
    -o test/b2_regression_bin

./test/b2_regression_bin
echo "b2 regressions passed"
