#!/usr/bin/env bash
set -euo pipefail

make clean
make
g++ -std=c++17 -O2 -Wall -Wextra -pedantic \
  -I. \
  test/regression_20260608.cpp \
  src/io.cpp src/floorplan.cpp src/routing.cpp \
  -o /tmp/regression_20260608
/tmp/regression_20260608

bin/efgr "test/E_testcase_20260608_port.csv" /tmp/case_20260608.cfg --iters 50 --seed 1 --exact-interval 10
test -s /tmp/case_20260608.cfg
grep -q '^OUTLINE' /tmp/case_20260608.cfg
grep -q '^BLOCK' /tmp/case_20260608.cfg
grep -q '^CHANNEL' /tmp/case_20260608.cfg
grep -q '^PATH' /tmp/case_20260608.cfg
