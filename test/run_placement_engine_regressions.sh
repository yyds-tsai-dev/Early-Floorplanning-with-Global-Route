#!/usr/bin/env bash
set -euo pipefail

make clean
make
g++ -std=c++17 -O2 -Wall -Wextra -pedantic \
  -I. \
  test/placement_engine_regression.cpp \
  src/eval.cpp src/floorplan.cpp src/io.cpp src/placement_engine.cpp src/respace.cpp src/routing.cpp src/search.cpp src/lns_polish.cpp src/wl_redistribute.cpp src/validate.cpp \
  -o /tmp/placement_engine_regression
/tmp/placement_engine_regression

for engine in sp bstar hybrid; do
  out="/tmp/placement_${engine}.cfg"
  log="/tmp/placement_${engine}.log"
  bin/efgr "test/E_testcase_20260414.xlsx - case00.csv" "$out" \
    --engine "$engine" --iters 40 --seed 3 --exact-interval 10 2>"$log"
  test -s "$out"
  grep -q "engine=${engine}" "$log"
  grep -q '^OUTLINE' "$out"
  grep -q '^BLOCK' "$out"
  grep -q '^CHANNEL' "$out"
  grep -q '^PATH' "$out"
done
