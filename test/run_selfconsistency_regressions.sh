#!/usr/bin/env bash
# Beta Task 3 self-consistency gate (V5: docs/research/2026-07-28-alpha-v5-cost-recalculation-
# analysis.md port).
#
# Proves the solver's INTERNAL accounting -- V5 single-charge-per-axis-per-traversal channel demand
# + V5 cross/corner guiding-point wirelength (src/route_geometry.hpp) + V5 aggregate FT penalty
# (src/eval.cpp) -- agrees with the golden re-scorer (src/cfg_score.cpp, locked to the official V5
# calculation page) on the SAME written .cfg. For each contest case, run the solver, then rescore
# its own output and require, to <=REL_TOL relative (residual is pure 3-decimal .cfg coordinate
# rounding; measured <=3e-7 across all 5 cases, so 1e-6 is a ~3x-headroom gate, not a fudge):
#   * wirelength       : solver-reported == rescorer wl                  (<= REL_TOL relative)
#   * score            : solver score == rescorer checker_cost           (<= REL_TOL relative, NO
#                        adjustment term -- cfg_score.cpp now computes the SAME V5 aggregate FT
#                        penalty eval.cpp charges, so a legal/zero-open run has score == checker_cost
#                        up to rounding, whether or not it carries feedthrough)
#   * channel overflow : solver-reported channel_overflow_nets == rescorer overflow, to OVF_TOL =
#                        max(1.0, 1e-5*overflow) nets. The per-axis-per-traversal USAGE matches the
#                        judge EXACTLY (edges are written as integer tokens the re-scorer reads back
#                        verbatim); the only slack is the CAPACITY denominator 25*dim, computed from
#                        the solver's full-precision channel dims here but from the .cfg's 3-DECIMAL
#                        dims in the re-scorer. That ~0.01-0.03-net rounding is amplified relative to a
#                        small overflow (a difference of two large numbers), so a strict 1e-6-relative
#                        bound is not attainable for the raw overflow -- but the OVERFLOW PENALTY it
#                        feeds is still validated at REL_TOL through the score check above. A genuine
#                        demand-model error would shift overflow far outside this rounding-scale bound,
#                        so discriminating power is retained.
# Also requires open_routes == 0 on every case (an open route is a hard solver failure here).
set -euo pipefail
cd "$(dirname "$0")/.."

make >/dev/null
# efgr_rescore links the same proven sibling set as scripts/benchmark.sh (parseProblem -> usedOutline).
g++ -std=c++17 -O2 -Wall -Wextra -pedantic -I. \
    tools/rescore.cpp \
    src/cfg_score.cpp src/eval.cpp src/io.cpp src/placement_engine.cpp src/respace.cpp \
    src/routing.cpp src/search.cpp src/validate.cpp \
    -o bin/efgr_rescore >/dev/null

REL_TOL=1e-6
status=0
printf "%-7s %11s %11s %11s %6s\n" case rel_score rel_wl rel_ovf open
for i in 0 1 2 3 4; do
  csv="test/2026 ICCAD_case.case$i.csv"
  out="/tmp/selfcons_case$i.cfg"
  # stderr carries the summary; stdout is discarded. `|| true` so an open-route nonzero exit is
  # reported as a FAIL below (via open_routes), not an abort.
  log=$(bin/efgr "$csv" "$out" --seed 1 --iters 1000 2>&1 >/dev/null) || true
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
  echo "self-consistency regressions passed (solver == golden re-scorer to <= $REL_TOL rel, 0 open routes)"
else
  echo "SELF-CONSISTENCY REGRESSIONS FAILED"
fi
exit $status
