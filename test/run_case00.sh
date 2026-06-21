#!/usr/bin/env bash
set -euo pipefail

make clean
make
bin/efgr "test/E_testcase_20260414.xlsx - case00.csv" /tmp/case00.cfg --iters 500 --seed 1 --exact-interval 25
test -s /tmp/case00.cfg
cp "test/E_testcase_20260414.xlsx - case00.csv" /tmp/case00_submit.csv
rm -f /tmp/case00_submit.cfg
bin/EarlyFloorplanning_with_GlobalRoute /tmp/case00_submit.csv --iters 25 --seed 1 --exact-interval 10
test -s /tmp/case00_submit.cfg
block_count=$(awk 'BEGIN{inside=0; count=0} /^BLOCK$/{inside=1; next} /^END$/{inside=0} inside{count++} END{print count}' /tmp/case00.cfg)
test "$block_count" -eq 7
grep -q '^OUTLINE' /tmp/case00.cfg
grep -q '^BLOCK' /tmp/case00.cfg
grep -q '^CHANNEL' /tmp/case00.cfg
grep -q '^PATH' /tmp/case00.cfg
