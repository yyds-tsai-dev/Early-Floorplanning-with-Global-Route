#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."

make >/dev/null

# C3 (exact local-group LNS re-placement, .superpowers/sdd/c3-wlrec-briefs-20260817.md BRIEF A):
# links ONLY src/lns_polish.cpp + src/placement_engine.cpp, per the brief's own file-plan scope --
# lnsSelectGroup/lnsCandidatePositions/lnsPolishIterates are pure and have no dependency on
# search.cpp, routing.cpp, or eval.cpp; wireMedianTarget (placement_engine.cpp) is the only
# cross-file call, same precedent test/run_c1_regressions.sh already established for
# wl_redistribute.cpp.
g++ -std=c++17 -O2 -Wall -Wextra -pedantic -I. \
    test/regression_lns_20260817.cpp \
    src/lns_polish.cpp src/placement_engine.cpp \
    -o test/lns_regression_bin

./test/lns_regression_bin

echo "lns (C3) regressions passed"
