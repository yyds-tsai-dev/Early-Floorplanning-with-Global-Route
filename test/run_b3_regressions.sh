#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."

make >/dev/null

# outlineSweep (search.hpp/.cpp) needs the same proven sibling set as run_b1_regressions.sh, minus
# floorplan.cpp (verified unneeded for this file -- it links and runs clean without it; floorplan.cpp
# is the legacy order-only placement path CLAUDE.md documents as unused by search.cpp/main.cpp).
g++ -std=c++17 -O2 -Wall -Wextra -pedantic \
    test/regression_b3_20260724.cpp \
    src/routing.cpp src/eval.cpp src/io.cpp src/validate.cpp \
    src/placement_engine.cpp src/search.cpp src/respace.cpp \
    -o test/b3_regression_bin

./test/b3_regression_bin
echo "B3 regressions passed"
