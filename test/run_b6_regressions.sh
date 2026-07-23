#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."

make >/dev/null

# B6 rank-fidelity instrumentation regressions (.superpowers/sdd/b6-brief.md): rank_stats.hpp is
# header-only (no matching .cpp), so it needs no extra link entry of its own -- just the same
# sibling set run_b4_regressions.sh uses (minus cfg_score.cpp, which this file's determinism smoke
# does not need: it compares raw .cfg bytes, not a golden-rescored checker_cost).
g++ -std=c++17 -O2 -Wall -Wextra -pedantic -I. \
    test/regression_b6_20260724.cpp \
    src/routing.cpp src/eval.cpp src/io.cpp src/validate.cpp \
    src/placement_engine.cpp src/search.cpp src/respace.cpp \
    -o test/b6_regression_bin

./test/b6_regression_bin
echo "b6 regressions passed"
