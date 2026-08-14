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

# P0b (L6 value-symmetric auto-merge rule, docs/research/2026-08-14-final-levers-verdicts-and-0811-
# intel.md §3.3/§5 item 2, .superpowers/sdd/p0b-brief.md): binary-level wiring checks for main.cpp's
# forced-off > flag-on > auto precedence -- the C++ unit tests above only cover the pure helper and
# parse-level adoption criterion, not that main.cpp actually wires it up and prints the right arm.
# `make` already built bin/efgr above; run each case with a small --iters so this stays fast.
CASE0="test/E_testcase_20260727/testcase-upload-20260727/2026 ICCAD_case.case0.csv"
CASE1="test/E_testcase_20260727/testcase-upload-20260727/2026 ICCAD_case.case1.csv"

echo "-- P0b wiring: case1 bare -> undirected=auto --"
out1=$(bin/efgr "$CASE1" /tmp/p0b_case1_bare.cfg --iters 50 --seed 1 2>&1 >/dev/null)
echo "$out1" | grep -q "undirected=auto" || { echo "FAIL: case1 bare did not report undirected=auto"; echo "$out1"; exit 1; }
echo "$out1" | grep -o 'undirected=[a-z-]*'

echo "-- P0b wiring: case0 bare -> undirected=off --"
out0=$(bin/efgr "$CASE0" /tmp/p0b_case0_bare.cfg --iters 50 --seed 1 2>&1 >/dev/null)
echo "$out0" | grep -q "undirected=off" || { echo "FAIL: case0 bare did not report undirected=off"; echo "$out0"; exit 1; }
echo "$out0" | grep -o 'undirected=[a-z-]*'

echo "-- P0b wiring: case0 --undirected-bundles -> undirected=flag --"
out0f=$(bin/efgr "$CASE0" /tmp/p0b_case0_flag.cfg --iters 50 --seed 1 --undirected-bundles 2>&1 >/dev/null)
echo "$out0f" | grep -q "undirected=flag" || { echo "FAIL: case0 --undirected-bundles did not report undirected=flag"; echo "$out0f"; exit 1; }
echo "$out0f" | grep -o 'undirected=[a-z-]*'

echo "-- P0b wiring: case1 --no-undirected-bundles -> undirected=forced-off --"
out1f=$(bin/efgr "$CASE1" /tmp/p0b_case1_forcedoff.cfg --iters 50 --seed 1 --no-undirected-bundles 2>&1 >/dev/null)
echo "$out1f" | grep -q "undirected=forced-off" || { echo "FAIL: case1 --no-undirected-bundles did not report undirected=forced-off"; echo "$out1f"; exit 1; }
echo "$out1f" | grep -o 'undirected=[a-z-]*'

rm -f /tmp/p0b_case1_bare.cfg /tmp/p0b_case0_bare.cfg /tmp/p0b_case0_flag.cfg /tmp/p0b_case1_forcedoff.cfg

echo "undirected bundle merge regressions passed"
