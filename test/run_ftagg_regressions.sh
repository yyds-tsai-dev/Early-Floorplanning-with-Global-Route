#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."

make >/dev/null

g++ -std=c++17 -O2 -Wall -Wextra -pedantic \
    test/regression_ftagg_20260728.cpp \
    src/routing.cpp src/eval.cpp src/io.cpp src/validate.cpp \
    src/placement_engine.cpp src/search.cpp src/lns_polish.cpp src/wl_redistribute.cpp src/floorplan.cpp src/respace.cpp \
    -o test/ftagg_regression_bin

./test/ftagg_regression_bin
echo "F-FT regressions passed"
