#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."

make >/dev/null

# L6 (undirected bundle merge, docs/research/2026-07-30-final-levers-design.md §L6.7): links the same
# "kitchen sink" sibling set run_b5_regressions.sh uses -- io.cpp (mergeUndirectedBundles/parseProblem)
# pulls in eval.cpp/routing.cpp transitively, and cfg_score.cpp/search.cpp are needed by the later
# U7 (parseCfg/officialScoreCfg) and U4-U7 (runPricingPortfolio) tests added in this same file as the
# design's "合併實作順序" steps land, so the link line is set up once rather than revisited per step.
g++ -std=c++17 -O2 -Wall -Wextra -pedantic -I. \
    test/regression_undirected_20260730.cpp \
    src/cfg_score.cpp src/eval.cpp src/io.cpp src/placement_engine.cpp src/respace.cpp \
    src/routing.cpp src/search.cpp src/validate.cpp \
    -o test/undirected_regression_bin

./test/undirected_regression_bin
echo "undirected bundle merge regressions passed"
