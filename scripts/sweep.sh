#!/usr/bin/env bash
# scripts/sweep.sh -- Beta Task 5 default-selection sweep. Reuses scripts/benchmark.sh's rescore +
# dual-cost machinery, adding budget/seed loops. Appends one CSV row per (seed,budget,engine,case)
# to $OUT live, then prints a seed-averaged aggregate (Sigma checker_cost / rank_cost / runtime and
# an OPEN flag if any run left open routes, per engine,budget). Knobs via env:
#   ENGINES BUDGETS SEEDS CASES REF_ITERS OUT
#   budget 0 == the --iters REF_ITERS reference (budget off); budget N>0 == --time-budget N.
set -uo pipefail
cd "$(dirname "$0")/.."
make >/dev/null || exit 1
g++ -std=c++17 -O2 -Wall -Wextra -pedantic -I. \
    tools/rescore.cpp \
    src/cfg_score.cpp src/eval.cpp src/io.cpp src/placement_engine.cpp src/respace.cpp \
    src/routing.cpp src/search.cpp src/validate.cpp \
    -o bin/efgr_rescore >/dev/null || exit 1

ENGINES="${ENGINES:-bstar hybrid}"
BUDGETS="${BUDGETS:-0 15 45 90}"
SEEDS="${SEEDS:-1}"
CASES="${CASES:-0 1 2 3 4}"
REF_ITERS="${REF_ITERS:-1000}"
OUT="${OUT:-/tmp/sweep_results.csv}"
RANK_ALPHA=(0.3 0.15 0.1 0.1 0.1)   # alpha-test scoreboard values (benchmark-only), mirrors benchmark.sh

echo "seed,budget,engine,case,checker_cost,rank_cost,overflow,wl,runtime,open_routes,validation,iters" > "$OUT"
printf "%-4s %-6s %-6s %-5s %14s %14s %8s %5s %7s\n" seed budget engine case checker_cost rank_cost runtime open iters
for seed in $SEEDS; do
  for budget in $BUDGETS; do
    for engine in $ENGINES; do
      for i in $CASES; do
        csv="test/2026 ICCAD_case.case$i.csv"; out="/tmp/sweep_${engine}_b${budget}_s${seed}_c$i.cfg"
        if [ "$budget" = "0" ]; then flag="--iters $REF_ITERS"; else flag="--time-budget $budget"; fi
        start=$(date +%s.%N)
        log=$(bin/efgr "$csv" "$out" --engine "$engine" --seed "$seed" $flag 2>&1 >/dev/null) || true
        rt=$(awk -v e="$(date +%s.%N)" -v s="$start" 'BEGIN{printf "%.2f", e-s}')
        opn=$(printf '%s' "$log" | grep -oE 'open_routes=[0-9]+' | cut -d= -f2); opn=${opn:-NA}
        val=$(printf '%s' "$log" | grep -oE 'validation=[a-z]+' | cut -d= -f2); val=${val:-NA}
        itr=$(printf '%s' "$log" | grep -oE 'iters=[0-9]+' | head -1 | cut -d= -f2); itr=${itr:-NA}
        B_area=0 B_wl=0 B_overflow=0 B_overflow_penalty=0 B_checker_cost=0
        eval "$(bin/efgr_rescore "$csv" "$out" 2>/dev/null | tr ' ' '\n' | sed 's/^/B_/')" || true
        rank=$(awk -v a="$B_area" -v w="$B_wl" -v al="${RANK_ALPHA[$i]}" -v op="$B_overflow_penalty" \
                   -v rt="$rt" 'BEGIN{printf "%.3f", a + al*w + op + 0.5*rt*sqrt(a)}')
        echo "$seed,$budget,$engine,case$i,$B_checker_cost,$rank,$B_overflow,$B_wl,$rt,$opn,$val,$itr" >> "$OUT"
        printf "%-4s %-6s %-6s %-5s %14.0f %14.0f %7.2fs %5s %7s\n" \
               "$seed" "$budget" "$engine" "case$i" "$B_checker_cost" "$rank" "$rt" "$opn" "$itr"
      done
    done
  done
done

echo
echo "=== aggregate: seed-averaged Sigma over cases per (engine,budget) ==="
printf "%-14s %16s %16s %10s %6s\n" engine_budget sum_checker sum_rank sum_rt open
awk -F, 'NR>1{
  k=$3" "$2;
  chk[k]+=$5; rnk[k]+=$6; rt[k]+=$9;
  if (!((k SUBSEP $1) in ss)) { ss[k SUBSEP $1]=1; nseed[k]++ }
  if ($10!="0" && $10!="NA") bad[k]=1;
}
END{
  for (k in chk){ ns=nseed[k]; if(ns<1)ns=1;
    printf "%-14s %16.0f %16.0f %9.1fs %6s\n", k, chk[k]/ns, rnk[k]/ns, rt[k]/ns, (k in bad)?"OPEN":"ok" }
}' "$OUT" | sort
