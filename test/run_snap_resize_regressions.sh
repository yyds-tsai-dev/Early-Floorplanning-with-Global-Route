#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."

make >/dev/null

# D3 (snap-to-neighbor soft resize move, .superpowers/sdd/levers-batch2-briefs-20260817.md Brief
# 1): same "kitchen sink" link line as run_l4b_regressions.sh/run_l3bc_regressions.sh -- the
# flag-ON case5 smoke pulls in parseProblem (io.cpp, which itself includes eval.hpp) transitively.
g++ -std=c++17 -O2 -Wall -Wextra -pedantic -I. \
    test/regression_snap_resize_20260817.cpp \
    src/cfg_score.cpp src/eval.cpp src/io.cpp src/placement_engine.cpp src/respace.cpp \
    src/routing.cpp src/search.cpp src/validate.cpp src/wl_redistribute.cpp \
    -o test/snap_resize_regression_bin

./test/snap_resize_regression_bin

echo "snap-resize (D3) regressions passed"
