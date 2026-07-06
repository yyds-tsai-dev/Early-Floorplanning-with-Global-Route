#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."

make >/dev/null

g++ -std=c++17 -O2 -Wall -Wextra -pedantic \
    test/regression_p1_20260706.cpp \
    src/routing.cpp src/eval.cpp src/io.cpp src/validate.cpp \
    src/placement_engine.cpp src/search.cpp src/floorplan.cpp \
    -o test/p1_regression_bin

./test/p1_regression_bin
echo "P1 regressions passed"
