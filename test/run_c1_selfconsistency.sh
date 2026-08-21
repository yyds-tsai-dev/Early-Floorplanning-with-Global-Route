#!/usr/bin/env bash
# C1a follow-up gate (fixed-topology WL-driven whitespace redistribution,
# .superpowers/sdd/c1-design-20260817.md): flag-ON arm of the Beta Task 3 self-consistency gate
# (test/run_selfconsistency_regressions.sh) -- proves the solver's internal accounting still agrees
# with the golden re-scorer (src/cfg_score.cpp) to <=REL_TOL relative WHEN --wl-redistribute is
# active, not just on the flag-off baseline that suite already covers. Deferred at C1a's own landing
# (commit 9781725) as a follow-up rather than blocking that commit.
#
# Identical methodology/tolerances to run_selfconsistency_regressions.sh (same REL_TOL/OVF_TOL
# derivation, same score/wirelength/overflow/open-routes checks) -- the ONLY difference is passing
# --wl-redistribute on each case's solver invocation, same iters (1000) the original uses.
set -euo pipefail
cd "$(dirname "$0")/.."

make >/dev/null
# efgr_rescore links the same proven sibling set as run_selfconsistency_regressions.sh.
g++ -std=c++17 -O2 -Wall -Wextra -pedantic -I. \
    tools/rescore.cpp \
    src/cfg_score.cpp src/eval.cpp src/io.cpp src/placement_engine.cpp src/respace.cpp \
    src/routing.cpp src/search.cpp src/lns_polish.cpp src/wl_redistribute.cpp src/validate.cpp \
    -o bin/efgr_rescore >/dev/null

REL_TOL=1e-6
status=0
printf "%-7s %11s %11s %11s %6s\n" case rel_score rel_wl rel_ovf open
for i in 0 1 2 3 4; do
  csv="test/2026 ICCAD_case.case$i.csv"
  out="/tmp/c1_selfcons_case$i.cfg"
  # stderr carries the summary; stdout is discarded. `|| true` so an open-route nonzero exit is
  # reported as a FAIL below (via open_routes), not an abort.
  log=$(bin/efgr "$csv" "$out" --seed 1 --iters 1000 --wl-redistribute 2>&1 >/dev/null) || true
  sscore=$(printf '%s' "$log" | grep -oE 'score=[0-9.eE+-]+' | head -1 | cut -d= -f2)
  swl=$(printf '%s' "$log" | grep -oE ' wirelength=[0-9.eE+-]+' | cut -d= -f2)
  sovf=$(printf '%s' "$log" | grep -oE 'channel_overflow_nets=[0-9.eE+-]+' | cut -d= -f2)
  sopen=$(printf '%s' "$log" | grep -oE 'open_routes=[0-9]+' | cut -d= -f2)

  rline=$(bin/efgr_rescore "$csv" "$out")
  rwl=$(printf '%s' "$rline" | grep -oE ' wl=[0-9.eE+-]+' | cut -d= -f2)
  rovf=$(printf '%s' "$rline" | grep -oE ' overflow=[0-9.eE+-]+' | cut -d= -f2)
  rcc=$(printf '%s' "$rline" | grep -oE 'checker_cost=[0-9.eE+-]+' | cut -d= -f2)

  read -r rels relw relo ok <<EOF
$(awk -v ss="$sscore" -v rc="$rcc" -v sw="$swl" -v rw="$rwl" -v so="$sovf" -v ro="$rovf" \
      -v op="$sopen" -v tol="$REL_TOL" 'BEGIN{
    adj = ss - 1e15*op;
    ad = adj-rc; if (ad<0) ad=-ad; rels = (rc>1 ? ad/rc : ad);
    wd = sw-rw;  if (wd<0) wd=-wd; relw = (rw>1 ? wd/rw : wd);
    od = so-ro;  if (od<0) od=-od; relo = (ro>1 ? od/ro : od);
    ovf_tol = (ro*1e-5 > 1.0 ? ro*1e-5 : 1.0);   # rounding-aware: capacity is 3-decimal in the .cfg
    ok = (rels<=tol && relw<=tol && od<=ovf_tol && op==0) ? 1 : 0;
    printf "%.3e %.3e %.3e %d", rels, relw, relo, ok;
}')
EOF
  printf "%-7s %11s %11s %11s %6s\n" "case$i" "$rels" "$relw" "$relo" "$sopen"
  if [ "$ok" != "1" ]; then
    status=1
    echo "  FAIL case$i: rel_score=$rels rel_wl=$relw rel_ovf=$relo open=$sopen (tol=$REL_TOL)"
  fi
done

if [ $status -eq 0 ]; then
  echo "C1a (--wl-redistribute) self-consistency regressions passed (solver == golden re-scorer to <= $REL_TOL rel, 0 open routes)"
else
  echo "C1a (--wl-redistribute) SELF-CONSISTENCY REGRESSIONS FAILED"
fi
exit $status
