#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."

make >/dev/null

# L1 (cheap-tier objective realignment --cheap-proxy-v2, case5-design.md §4-L1,
# .superpowers/sdd/case5-l1-brief.md): link line copied from run_undirected_regressions.sh's
# "kitchen sink" sibling set per the brief -- eval.cpp is the unit under test, and exactScore's
# cancellation test pulls in routing/search/io transitively, so reusing the proven set beats
# hand-minimising a new one.
g++ -std=c++17 -O2 -Wall -Wextra -pedantic -I. \
    test/regression_cheap_proxy_v2.cpp \
    src/cfg_score.cpp src/eval.cpp src/io.cpp src/placement_engine.cpp src/respace.cpp \
    src/routing.cpp src/search.cpp src/lns_polish.cpp src/wl_redistribute.cpp src/validate.cpp \
    -o test/cheap_proxy_v2_regression_bin

./test/cheap_proxy_v2_regression_bin

echo "cheap_proxy_v2 (L1) regressions passed"
