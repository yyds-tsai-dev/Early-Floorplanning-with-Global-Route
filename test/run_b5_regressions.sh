#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."

make >/dev/null

# B5a (elite-pool rework, .superpowers/sdd/b5-design.md): admission-set unit tests
# (rememberBounded/reservoirSample/buildEliteFullSet, all header-declared free functions in
# search.hpp/.cpp) plus an end-to-end determinism smoke -- same sibling set run_b6_regressions.sh
# uses (runPricingPortfolio's own dependency chain). B5a.1 adds cfg_score.cpp (golden rescorer,
# same as run_b4_regressions.sh) for the portfolio-level floor pin test.
g++ -std=c++17 -O2 -Wall -Wextra -pedantic -I. \
    test/regression_b5_20260724.cpp \
    src/cfg_score.cpp src/routing.cpp src/eval.cpp src/io.cpp src/validate.cpp \
    src/placement_engine.cpp src/search.cpp src/respace.cpp \
    -o test/b5_regression_bin

./test/b5_regression_bin
echo "b5a regressions passed"
