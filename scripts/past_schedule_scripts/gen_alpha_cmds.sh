#!/usr/bin/env bash
set -euo pipefail

# alpha 扫描：从 0 到 100，步长可调（默认 5）。
# 步长 5 -> 21 个点，画 n^(alpha/100) 曲线已足够平滑；
step="${STEP:-5}"

alphas=()
for a in $(seq 0 "$step" 100); do
  alphas+=("$a")
done

cases=(
  "1 5 7"
  "2 6 11"
  "1 9 25"
)

# 机制实验测的是 effective_tickets（近似确定值），不太受时序随机影响，
# 不需要多 repeat。默认 2，足够确认稳定性。
repeat="${1:-2}"

total=0
for r in $(seq 1 "$repeat"); do
  echo "# repeat $r"
  for c in "${cases[@]}"; do
    for a in "${alphas[@]}"; do
      echo "alpha_arg_test $a $c"
      total=$((total + 1))
    done
  done
done

echo "generated $total commands (step=$step, ${#alphas[@]} alphas x ${#cases[@]} cases x $repeat repeats)" 1>&2