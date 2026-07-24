#!/bin/bash
#
# 生成 adaptive vs fixed 对照实验的命令列表。
# 输出到 stdout，自行重定向到 logs/adaptive_alpha_cmds.txt。
#
# 用法：
#   ./scripts/gen_adaptive_alpha_cmds.sh > logs/adaptive_alpha_cmds.txt
#   timeout 1800 ./run.sh loongarch64 < logs/adaptive_alpha_cmds.txt 2>&1 \
#       | tee logs/adaptive_alpha_raw.log
#
# 对照设计：
#   - adaptive 与 fixed 成对出现（同一 case、同一 alpha）。
#   - SHUFFLE=1（默认）：每个 repeat 内，把所有 (case,alpha,mode) 命令整体打散，
#     彻底消除“顺序相关的系统漂移”对 adaptive/fixed 对照的影响。
#   - SHUFFLE=0：保持 (case, alpha) 下 adaptive 紧跟 fixed 的交错顺序（可读性好）。
#   日志按 [adaptive_alpha] run 行切分，乱序不影响 Python 解析。

set -euo pipefail

# ---- 可调参数 ----

alphas=(0 25 50 75 100)
cases=(
  "1 25 9"
  "2 40 10"
  "2 60 15"         
  "3 125 25"
  "4 225 64"         
)

repeat=5

prog="smoother_adaptive_alpha_test"

# 1=每个 repeat 内整体随机打散；0=保持交错顺序
SHUFFLE=${SHUFFLE:-1}

# 随机种子：固定它可让“随机顺序”可复现（便于他人重跑出同样的序列）。
# 留空则用系统随机。
SEED=${SEED:-12345}

# ---- 实现 ----

# 可复现的洗牌：优先用 shuf --random-source，回退到 awk。
shuffle_lines() {
  if command -v shuf >/dev/null 2>&1; then
    if [ -n "$SEED" ]; then
      # 用种子驱动一个确定性字节流喂给 shuf
      shuf --random-source=<(yes "$SEED" | head -c 100000)
    else
      shuf
    fi
  else
    # 无 shuf 的回退：awk 按随机键排序
    awk -v seed="${SEED:-0}" 'BEGIN{srand(seed)} {print rand()"\t"$0}' \
      | sort -k1,1 | cut -f2-
  fi
}

total=0

for r in $(seq 1 "$repeat"); do
  echo "# ===== repeat $r / $repeat ====="

  # 先把本 repeat 的所有命令收集到一个缓冲，再决定是否打散
  block=""
  for c in "${cases[@]}"; do
    for a in "${alphas[@]}"; do
      block+="$prog $a $c"$'\n'
      block+="$prog $a $c fixed"$'\n'
      total=$((total + 2))
    done
  done

  if [ "$SHUFFLE" = "1" ]; then
    # 每个 repeat 用不同但确定的种子（SEED+r），保证既随机又可复现
    SEED="${SEED}${r}" printf '%s' "$block" | shuffle_lines
  else
    printf '%s' "$block"
  fi
done

echo "generated $total commands: ${#cases[@]} cases x ${#alphas[@]} alphas x $repeat repeats x 2 modes (shuffle=$SHUFFLE)" 1>&2