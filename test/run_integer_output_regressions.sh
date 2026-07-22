#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."

make >/dev/null

g++ -std=c++17 -O2 -Wall -Wextra -pedantic \
    test/regression_integer_output.cpp \
    src/routing.cpp src/eval.cpp src/io.cpp src/validate.cpp \
    src/placement_engine.cpp src/search.cpp src/floorplan.cpp src/respace.cpp \
    -o test/integer_output_regression_bin

./test/integer_output_regression_bin
echo "Integer output regressions passed"
