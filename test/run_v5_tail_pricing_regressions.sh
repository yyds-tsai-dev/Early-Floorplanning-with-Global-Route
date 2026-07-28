#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."

make >/dev/null

# V5 tail-aware marginal overflow pricing regressions (Codex V5-port review finding #2;
# docs/research/2026-07-28-alpha-v5-cost-recalculation-analysis.md section 6): the new pricing
# surface is route_cost.hpp's marginalOverflowPrice (header-only) plus its two consumers,
# routing.cpp's per-round p_ovf re-pricing and respace.cpp's capacityGapCandidates tail branch --
# the same minimal link line run_b7_regressions.sh already proved sufficient for
# respace.cpp + routing.cpp fixtures.
g++ -std=c++17 -O2 -Wall -Wextra -pedantic -I. \
    test/regression_v5_tail_pricing_20260728.cpp \
    src/respace.cpp src/placement_engine.cpp src/routing.cpp \
    -o test/v5_tail_pricing_regression_bin

./test/v5_tail_pricing_regression_bin
echo "v5 tail-pricing regressions passed"
