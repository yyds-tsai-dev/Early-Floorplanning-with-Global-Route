#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."

make >/dev/null

# cfg_score.cpp is self-contained, but parseProblem (io.cpp) pulls in the usual sibling set
# (usedOutline lives in eval.cpp). Same proven set as run_20260618_regressions.sh, + cfg_score.cpp.
g++ -std=c++17 -O2 -Wall -Wextra -pedantic \
    -I. \
    test/regression_cfg_score.cpp \
    src/cfg_score.cpp src/eval.cpp src/io.cpp src/placement_engine.cpp src/respace.cpp \
    src/routing.cpp src/search.cpp src/validate.cpp \
    -o test/cfg_score_regression_bin

./test/cfg_score_regression_bin
echo "cfg_score regressions passed"
