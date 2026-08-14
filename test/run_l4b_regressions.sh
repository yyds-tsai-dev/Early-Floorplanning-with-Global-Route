#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."

make >/dev/null

# L4b (shelf seed + aspect stamp, case5-design.md §4-L4b): same "kitchen sink" link line as
# run_l3bc_regressions.sh -- the seeded-hybrid smoke test pulls in the whole solver transitively.
g++ -std=c++17 -O2 -Wall -Wextra -pedantic -I. \
    test/regression_l4b_20260815.cpp \
    src/cfg_score.cpp src/eval.cpp src/io.cpp src/placement_engine.cpp src/respace.cpp \
    src/routing.cpp src/search.cpp src/validate.cpp \
    -o test/l4b_regression_bin

./test/l4b_regression_bin

echo "l4b regressions passed"
