#!/usr/bin/env bash
# F1-flip regression (2026-07-29): pins the THREE argument-parsing modes src/main.cpp resolves
# --iters/--time-budget into (see the F1 comment block in main.cpp, right before the
# "budget_parse:" stderr marker) via that cheap pre-search marker -- NOT a live 600s run. A bare
# invocation now self-imposes a 600s time budget (the F1 flip); actually waiting one out here would
# make this suite balloon, which the task explicitly forbids. Each case below pins PARSING only:
#   1. --iters given, no --time-budget  -> budget OFF (0), dev/regression semantics unchanged.
#   2. --time-budget given (0), no --iters -> resolved to exactly the passed value, unchanged.
#   3. bare (neither flag)               -> resolved budget forced to 600s (the F1 flip) -- proven
#      by timeout-killing the run almost immediately and reading the EARLY marker, which main.cpp
#      prints before the (possibly 600s) portfolio search starts, so this assertion costs ~2s, not
#      600s.
set -euo pipefail
cd "$(dirname "$0")/.."

make >/dev/null

csv="test/E_testcase_20260414.xlsx - case00.csv"
log_iters=/tmp/f1_explicit_iters.log
log_budget0=/tmp/f1_explicit_budget0.log
log_bare=/tmp/f1_bare.log

# Mode 1: explicit --iters, no --time-budget -> budget OFF. Tiny case + iters=50 completes in well
# under a second, so this runs to completion (no timeout needed).
bin/efgr "$csv" /tmp/f1_explicit_iters.cfg --iters 50 --seed 1 2>"$log_iters" >/dev/null
grep -q '^budget_parse: iters_explicit=1 time_budget_explicit=0 resolved_time_budget_sec=0$' "$log_iters"

# Mode 2: explicit --time-budget 0, no --iters -> resolved value is exactly the passed 0
# (unaffected by the default 1000 iters; budget=0 means "disabled", so the run is still
# iteration-bounded and fast on this tiny case).
bin/efgr "$csv" /tmp/f1_explicit_budget0.cfg --time-budget 0 --seed 1 2>"$log_budget0" >/dev/null
grep -q '^budget_parse: iters_explicit=0 time_budget_explicit=1 resolved_time_budget_sec=0$' "$log_budget0"

# Mode 3: bare (neither flag) -> F1 flip forces the 600s budget. A live 600s run is NOT acceptable
# in a test, so timeout-kill almost immediately and read the EARLY marker (printed before the
# portfolio search runs) rather than waiting out the budget. `|| true`: a timeout-killed process
# exits 124, which must not trip `set -e`.
timeout 2 bin/efgr "$csv" /tmp/f1_bare.cfg >/dev/null 2>"$log_bare" || true
grep -q '^budget_parse: iters_explicit=0 time_budget_explicit=0 resolved_time_budget_sec=600$' "$log_bare"

echo "F1 budget-mode regressions passed"
