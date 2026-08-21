#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."

make >/dev/null

# P1 stage 1 (plateau-stop, .superpowers/sdd/p1-plateau-stop-design-20260819.md): same "kitchen
# sink" link line as run_b2_regressions.sh/run_l3bc_regressions.sh -- runPricingPortfolio pulls in
# the whole solver transitively, and every runner script now links src/lns_polish.cpp too.
g++ -std=c++17 -O2 -Wall -Wextra -pedantic -I. \
    test/regression_plateau_20260819.cpp \
    src/routing.cpp src/eval.cpp src/io.cpp src/validate.cpp \
    src/placement_engine.cpp src/search.cpp src/lns_polish.cpp src/wl_redistribute.cpp src/respace.cpp \
    -o test/plateau_regression_bin

./test/plateau_regression_bin

# Shell smoke (design §11.4, amended per this landing's machine constraint: case0/20s only, not
# the design's original 25s) -- exercises the REAL CLI parsing + summary-line path end to end.
# Asserts rc 0, validation=ok, open_routes=0, and that the plateau= telemetry field is present.
csv="test/2026 ICCAD_case.case0.csv"

bin/efgr "$csv" /tmp/plateau_c0_on.cfg --time-budget 20 --plateau-stop >/dev/null 2>/tmp/plateau_c0_on.log
grep -q 'validation=ok' /tmp/plateau_c0_on.log
grep -q 'open_routes=0' /tmp/plateau_c0_on.log
grep -q ' plateau=' /tmp/plateau_c0_on.log
grep -q '^plateau_parse: plateau_stop=1 .* arm=flag$' /tmp/plateau_c0_on.log

bin/efgr "$csv" /tmp/plateau_c0_off.cfg --time-budget 20 --no-plateau-stop >/dev/null 2>/tmp/plateau_c0_off.log
grep -q 'validation=ok' /tmp/plateau_c0_off.log
grep -q 'open_routes=0' /tmp/plateau_c0_off.log
grep -q ' plateau=' /tmp/plateau_c0_off.log
grep -q '^plateau_parse: plateau_stop=0 .* arm=forced-off$' /tmp/plateau_c0_off.log

bin/efgr "$csv" /tmp/plateau_c0_margin0.cfg --time-budget 20 --plateau-stop --plateau-margin 0 \
    >/dev/null 2>/tmp/plateau_c0_margin0.log
grep -q 'validation=ok' /tmp/plateau_c0_margin0.log
grep -q 'open_routes=0' /tmp/plateau_c0_margin0.log
grep -q '^plateau_parse: plateau_stop=1 plateau_margin=0 arm=flag$' /tmp/plateau_c0_margin0.log
# margin 0 disables the lever -- plateau_stopped must be false even with the flag forced on.
grep -q ' plateau=0 ' /tmp/plateau_c0_margin0.log

echo "plateau regressions passed"
