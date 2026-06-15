#!/usr/bin/env bash
set -euo pipefail

repeat="${1:-5}"

# initial_alpha
alphas=(0 25 50 75 100)

# control_threads ai_threads logger_threads
cases=(
  "1 14 8"
  "1 25 9"
)

for r in $(seq 1 "$repeat"); do
  echo "# repeat $r"

  for c in "${cases[@]}"; do
    for a in "${alphas[@]}"; do
      echo "adaptive_alpha_test $a $c"
    done
  done
done