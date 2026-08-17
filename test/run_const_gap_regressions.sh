#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."

make >/dev/null

# B1 (constant-gap negotiation pricing, .superpowers/sdd/levers-batch2-briefs-20260817.md Brief
# 2): same minimal link line as run_v5_tail_pricing_regressions.sh -- proven sufficient for
# respace.cpp+routing.cpp fixtures (routeProblem/roundCostOfficial only, no io/eval/search needed).
g++ -std=c++17 -O2 -Wall -Wextra -pedantic -I. \
    test/regression_const_gap_20260817.cpp \
    src/respace.cpp src/placement_engine.cpp src/routing.cpp \
    -o test/const_gap_regression_bin

./test/const_gap_regression_bin
echo "const-gap negotiation (B1) regressions passed"
