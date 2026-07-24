#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."

make >/dev/null

# B7 capacity-profit interior-gap rungs regressions (.superpowers/sdd/b7-brief.md): the new
# symbols (CapacityGapCandidate, capacityGapCandidates, applyGapCandidate) live entirely in
# respace.cpp, whose own dependencies are placement_engine.cpp (shrinkWrapOutline/anchorAtOutline)
# and, for this suite's one end-to-end fixture, routing.cpp (routeProblem) -- both header-only
# otherwise (route_cost.hpp/ft.hpp/route_geometry.hpp), so no eval.cpp/io.cpp/validate.cpp/
# search.cpp link entry is needed (verified: respace.cpp only references ExactCachedFn/
# RespaceResult as TYPES from search.hpp, never calling a search.cpp-defined symbol) -- the
# smallest link line of any b-series suite.
g++ -std=c++17 -O2 -Wall -Wextra -pedantic -I. \
    test/regression_b7_20260725.cpp \
    src/respace.cpp src/placement_engine.cpp src/routing.cpp \
    -o test/b7_regression_bin

./test/b7_regression_bin
echo "b7 regressions passed"
