#!/usr/bin/env bash
set -euo pipefail

repeat="${1:-5}"

cases=(
  "100 100 100"
  "100 200 300"
  "50 100 250"
  "80 120 300"
  "100 300 700"
)

for r in $(seq 1 "$repeat"); do
  echo "# repeat $r"

  for c in "${cases[@]}"; do
    echo "stride_ticket_test $c"
  done
done