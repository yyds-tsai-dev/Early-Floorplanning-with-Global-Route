#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."

make >/dev/null

# L4b (shelf seed + aspect stamp, case5-design.md §4-L4b): same "kitchen sink" link line as
# run_l3bc_regressions.sh -- the seeded-hybrid smoke test pulls in the whole solver transitively.
# C1a (.superpowers/sdd/c1-design-20260817.md): search.cpp now calls wlRedistributeIterates
# (search.cpp's maybeAdoptWlRedistribute), so src/wl_redistribute.cpp joins this link line --
# linker-forced (undefined reference without it), not a design choice of this suite.
g++ -std=c++17 -O2 -Wall -Wextra -pedantic -I. \
    test/regression_l4b_20260815.cpp \
    src/cfg_score.cpp src/eval.cpp src/io.cpp src/placement_engine.cpp src/respace.cpp \
    src/routing.cpp src/search.cpp src/lns_polish.cpp src/validate.cpp src/wl_redistribute.cpp \
    -o test/l4b_regression_bin

./test/l4b_regression_bin

echo "l4b regressions passed"
