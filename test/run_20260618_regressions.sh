#!/usr/bin/env bash
set -euo pipefail

make clean
make
test -x bin/EarlyFloorplanning_with_GlobalRoute
g++ -std=c++17 -O2 -Wall -Wextra -pedantic \
  -I. \
  test/regression_20260618.cpp \
  src/eval.cpp src/io.cpp src/placement_engine.cpp src/respace.cpp src/routing.cpp src/search.cpp src/wl_redistribute.cpp src/validate.cpp \
  -o /tmp/regression_20260618
/tmp/regression_20260618
