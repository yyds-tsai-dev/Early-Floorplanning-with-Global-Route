#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."

make >/dev/null

# B4.1 route-pricing portfolio regressions (.superpowers/sdd/b4-report.md): runs the full default
# search under runPricingPortfolio on real cases, so it needs the same sibling set run_b3_
# regressions.sh already uses (cfg_score.cpp for the golden rescorer, search.cpp for
# runPricingPortfolio itself).
g++ -std=c++17 -O2 -Wall -Wextra -pedantic -I. \
    test/regression_b4_20260724.cpp \
    src/cfg_score.cpp src/routing.cpp src/eval.cpp src/io.cpp src/validate.cpp \
    src/placement_engine.cpp src/search.cpp src/lns_polish.cpp src/wl_redistribute.cpp src/respace.cpp \
    -o test/b4_regression_bin

./test/b4_regression_bin
echo "b4.1 regressions passed"
