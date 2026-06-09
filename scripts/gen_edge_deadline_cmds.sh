#!/usr/bin/env bash
set -euo pipefail

repeat="${1:-3}"

alphas=(0 25 50 75 100)

# control ai logger
cases=(
  "1 25 9"
  "1 14 8"
  "1 9 4"
  "1 7 3"
  "1 5 2"
)

for r in $(seq 1 "$repeat"); do
  echo "# repeat $r"
  for c in "${cases[@]}"; do
    for a in "${alphas[@]}"; do
      echo "edge_deadline_arg_test $a $c"
    done
  done
done