#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."

make >/dev/null

# D2 (portfolio internal-WL-weight ladder legs, docs/research/2026-08-13-external-levers-v3-
# survey.md §5-D2, shortlist #3): binary-level wiring checks for main.cpp's hidden --wl-ladder flag
# and search.cpp's runPricingPortfolio ladder block, same "run the built binary + grep/md5 the
# outputs" style run_undirected_regressions.sh/run_f1_regressions.sh use -- `make` already built
# bin/efgr above.
CASE0="test/E_testcase_20260811/testcase_upload_20260712/2026 ICCAD_case.case0.csv"

OFF_CFG=/tmp/wl_ladder_off.cfg
FLAG_NOBUDGET_CFG=/tmp/wl_ladder_flag_nobudget.cfg
SMOKE_CFG=/tmp/wl_ladder_smoke.cfg
OFF_LOG=/tmp/wl_ladder_off.log
FLAG_NOBUDGET_LOG=/tmp/wl_ladder_flag_nobudget.log
SMOKE_LOG=/tmp/wl_ladder_smoke.log

echo "-- bare iters (no --time-budget) means budget off: --wl-ladder must not change output --"
bin/efgr "$CASE0" "$OFF_CFG" --iters 200 --seed 1 2>"$OFF_LOG" >/dev/null
bin/efgr "$CASE0" "$FLAG_NOBUDGET_CFG" --iters 200 --seed 1 --wl-ladder 2>"$FLAG_NOBUDGET_LOG" >/dev/null
md5_off=$(md5sum "$OFF_CFG" | awk '{print $1}')
md5_flag_nobudget=$(md5sum "$FLAG_NOBUDGET_CFG" | awk '{print $1}')
if [ "$md5_off" != "$md5_flag_nobudget" ]; then
    echo "FAIL: --wl-ladder changed output with no --time-budget (budget off must suppress the ladder block)"
    exit 1
fi
echo "OK: md5 identical ($md5_off)"
grep -q 'ladder_legs=0' "$OFF_LOG" || { echo "FAIL: flag-off run did not report ladder_legs=0"; cat "$OFF_LOG"; exit 1; }
grep -q 'ladder_legs=0' "$FLAG_NOBUDGET_LOG" || { echo "FAIL: budget-off --wl-ladder run did not report ladder_legs=0"; cat "$FLAG_NOBUDGET_LOG"; exit 1; }

echo "-- flag on + a real time budget: ladder block actually runs, rc=0, validation ok --"
set +e
bin/efgr "$CASE0" "$SMOKE_CFG" --iters 200 --seed 1 --time-budget 30 --wl-ladder 2>"$SMOKE_LOG" >/dev/null
rc=$?
set -e
[ "$rc" -eq 0 ] || { echo "FAIL: smoke run exited $rc"; cat "$SMOKE_LOG"; exit 1; }
grep -q 'ladder_legs=' "$SMOKE_LOG" || { echo "FAIL: smoke run stderr missing ladder_legs="; cat "$SMOKE_LOG"; exit 1; }
grep -q 'validation=ok' "$SMOKE_LOG" || { echo "FAIL: smoke run did not validate ok"; cat "$SMOKE_LOG"; exit 1; }
echo "OK: $(grep -o 'ladder_legs=[0-9]*' "$SMOKE_LOG")"

rm -f "$OFF_CFG" "$FLAG_NOBUDGET_CFG" "$SMOKE_CFG" "$OFF_LOG" "$FLAG_NOBUDGET_LOG" "$SMOKE_LOG"

echo "wl-ladder (D2) regressions passed"
