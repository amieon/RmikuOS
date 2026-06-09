#!/usr/bin/env bash
set -euo pipefail

alphas=(0 25 50 75 100)
cases=(
  "1 2 3"
  "1 3 5"
  "1 5 7"
  "2 6 11"
  "3 8 13"
)

repeat="${1:-3}"

for r in $(seq 1 "$repeat"); do
  echo "# repeat $r"
  for c in "${cases[@]}"; do
    for a in "${alphas[@]}"; do
      echo "alpha_arg_test $a $c"
    done
  done
done