#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."

make >/dev/null

# D4/A5 pack (notch/pocket whitespace penalty, .superpowers/sdd/levers-batch2-briefs-20260817.md
# Brief 3): eval.cpp fixtures (cheapScore/exactScore) need placement_engine.cpp + routing.cpp
# transitively -- same minimal link line as run_v5_tail_pricing_regressions.sh/run_b7_regressions.sh
# proved sufficient for routing-backed eval.cpp fixtures, plus eval.cpp itself.
g++ -std=c++17 -O2 -Wall -Wextra -pedantic -I. \
    test/regression_notch_20260817.cpp \
    src/eval.cpp src/respace.cpp src/placement_engine.cpp src/routing.cpp \
    -o test/notch_regression_bin

./test/notch_regression_bin
echo "notch/pocket whitespace penalty (D4/A5) regressions passed"
