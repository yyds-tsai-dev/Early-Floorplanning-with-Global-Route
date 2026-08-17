#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."

make >/dev/null

# C1a (fixed-topology WL-driven whitespace redistribution, .superpowers/sdd/c1-design-20260817.md
# §8): links ONLY src/wl_redistribute.cpp + src/placement_engine.cpp, per the design's own file-plan
# scope -- the enumerator (wlRedistributeIterates) is pure and has no dependency on search.cpp,
# routing.cpp, or eval.cpp; wireMedianTarget (placement_engine.cpp) is its only cross-file call.
g++ -std=c++17 -O2 -Wall -Wextra -pedantic -I. \
    test/regression_c1_20260817.cpp \
    src/wl_redistribute.cpp src/placement_engine.cpp \
    -o test/c1_regression_bin

./test/c1_regression_bin

echo "c1 (C1a) regressions passed"
