#!/usr/bin/env bash
# usage: scripts/benchmark.sh [extra solver args...]   e.g. scripts/benchmark.sh --iters 2000
# Dual-cost matrix: checker_cost (official alpha=0.2 model, from efgr_rescore) vs rank_cost (the
# alpha-test scoreboard's per-case ranking alpha + delta=0.5 runtime penalty). The ranking alpha
# table and runtime penalty live ONLY here -- the checker cost model lives in src/cfg_score.cpp.
set -euo pipefail
cd "$(dirname "$0")/.."
make >/dev/null

# rescore links the same proven sibling set as the regression (parseProblem -> usedOutline).
g++ -std=c++17 -O2 -Wall -Wextra -pedantic -I. \
    tools/rescore.cpp \
    src/cfg_score.cpp src/eval.cpp src/io.cpp src/placement_engine.cpp src/respace.cpp \
    src/routing.cpp src/search.cpp src/validate.cpp \
    -o bin/efgr_rescore

RANK_ALPHA=(0.3 0.15 0.1 0.1 0.1)   # alpha-test scoreboard values (benchmark-only)
printf "%-6s %-8s %14s %14s %14s %14s %8s\n" case engine checker_cost rank_cost overflow wl runtime
for engine in bstar hybrid; do
  total_checker=0; total_rank=0
  for i in 0 1 2 3 4; do
    csv="test/2026 ICCAD_case.case$i.csv"; out="/tmp/bench_${engine}_case$i.cfg"
    start=$(date +%s.%N)
    bin/efgr "$csv" "$out" --engine "$engine" "$@" 2>/dev/null
    rt=$(awk -v e="$(date +%s.%N)" -v s="$start" 'BEGIN{printf "%.2f", e-s}')
    line=$(bin/efgr_rescore "$csv" "$out")
    eval "$(echo "$line" | tr ' ' '\n' | sed 's/^/B_/')"   # B_area= B_wl= ...
    rank=$(awk -v a="$B_area" -v w="$B_wl" -v al="${RANK_ALPHA[$i]}" -v op="$B_overflow_penalty" \
               -v rt="$rt" 'BEGIN{printf "%.3f", a + al*w + op + 0.5*rt*sqrt(a)}')
    printf "%-6s %-8s %14.0f %14.0f %14.3f %14.0f %7.2fs\n" \
           "case$i" "$engine" "$B_checker_cost" "$rank" "$B_overflow" "$B_wl" "$rt"
    total_checker=$(awk -v t="$total_checker" -v c="$B_checker_cost" 'BEGIN{printf "%.3f", t+c}')
    total_rank=$(awk -v t="$total_rank" -v r="$rank" 'BEGIN{printf "%.3f", t+r}')
  done
  printf "%-6s %-8s %14.0f %14.0f\n" TOTAL "$engine" "$total_checker" "$total_rank"
done
