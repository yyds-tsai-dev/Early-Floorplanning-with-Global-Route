#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."

make >/dev/null

g++ -std=c++17 -O2 -Wall -Wextra -pedantic \
    -I. \
    test/regression_testdata_20260712.cpp \
    src/eval.cpp src/io.cpp src/placement_engine.cpp src/respace.cpp src/routing.cpp src/search.cpp src/wl_redistribute.cpp src/validate.cpp \
    -o test/testdata_regression_bin

./test/testdata_regression_bin
echo "testdata regressions passed"
