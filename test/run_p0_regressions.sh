#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."

make >/dev/null

g++ -std=c++17 -O2 -Wall -Wextra -pedantic \
    test/regression_p0_20260706.cpp \
    src/routing.cpp src/eval.cpp src/io.cpp src/validate.cpp \
    src/placement_engine.cpp src/search.cpp src/lns_polish.cpp src/wl_redistribute.cpp src/floorplan.cpp src/respace.cpp \
    -o test/p0_regression_bin

./test/p0_regression_bin
echo "P0 regressions passed"
