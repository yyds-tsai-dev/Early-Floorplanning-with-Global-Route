#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."

make >/dev/null

# L3b/L3c (deep mode + budget governor, case5-design.md §4-L3b/§4-L3c,
# .superpowers/sdd/case5-l3bc-brief.md): same "kitchen sink" link line as
# run_b2_regressions.sh -- runPricingPortfolio pulls in the whole solver transitively.
g++ -std=c++17 -O2 -Wall -Wextra -pedantic -I. \
    test/regression_l3bc_20260815.cpp \
    src/cfg_score.cpp src/eval.cpp src/io.cpp src/placement_engine.cpp src/respace.cpp \
    src/routing.cpp src/search.cpp src/validate.cpp \
    -o test/l3bc_regression_bin

./test/l3bc_regression_bin

echo "l3bc regressions passed"
