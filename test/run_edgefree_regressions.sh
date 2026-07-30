#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."

make >/dev/null

# L2 (edge-free mode, docs/research/2026-07-30-final-levers-design.md sec L2.8): links the same
# sibling set run_undirected_regressions.sh uses -- placement_engine.cpp (PlacementEngine/
# shrinkWrapOutline) pulls in eval.cpp/routing.cpp transitively, and validate.cpp/cfg_score.cpp are
# needed for E7 (validateSolution) and general link completeness with the rest of the binary's object
# graph.
g++ -std=c++17 -O2 -Wall -Wextra -pedantic -I. \
    test/regression_edgefree_20260730.cpp \
    src/cfg_score.cpp src/eval.cpp src/io.cpp src/placement_engine.cpp src/respace.cpp \
    src/routing.cpp src/search.cpp src/validate.cpp \
    -o test/edgefree_regression_bin

./test/edgefree_regression_bin
echo "edge-free regressions passed"
