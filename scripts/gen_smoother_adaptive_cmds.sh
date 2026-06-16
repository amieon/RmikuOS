#!/bin/bash
#
# 生成 adaptive vs fixed 对照实验的命令列表。
# 输出到 stdout，自行重定向到 logs/adaptive_alpha_cmds.txt。
#
# 用法：
#   ./scripts/gen_adaptive_alpha_cmds.sh > logs/adaptive_alpha_cmds.txt
#
# 然后把命令喂进 guest（QEMU）执行，例如：
#   timeout 1800 ./run.sh loongarch64 < logs/adaptive_alpha_cmds.txt 2>&1 \
#       | tee logs/adaptive_alpha_raw.log
#
# 设计：adaptive 与 fixed 在同一 (case, alpha) 处“背靠背交错”执行，
# 让对照组与实验组在相近的系统状态下运行，消除时间漂移带来的偏差。

set -euo pipefail

# ---- 可调参数 ----

# 起始 / 固定 alpha 列表。
# adaptive 模式：作为初始 alpha（AIMD 会从这里出发自适应）。
# fixed 模式：作为全程钉死的 alpha。
alphas=(0 25 50 75 100)

# 负载 case，格式 "control ai logger"。
cases=(
  "1 14 8"
  "1 25 9"
  # ---- 极端压力点：先手动单跑确认内核不崩，再取消注释纳入批量 ----
  # 需要先把 user 端 MAX_THREADS 提到 >= 225 并重编内核/用户程序。
  # "3 225 40"
)

# 每个配置重复次数（用于聚合去噪）。
repeat=5

# 实验程序名（guest 内可执行名）。
prog="smoother_adaptive_alpha_test"

# ---- 生成 ----

total=0

for r in $(seq 1 "$repeat"); do
  echo "# ===== repeat $r / $repeat ====="
  for c in "${cases[@]}"; do
    for a in "${alphas[@]}"; do
      # 交错：同一 (case, alpha) 的 adaptive 紧跟 fixed
      echo "$prog $a $c"
      echo "$prog $a $c fixed"
      total=$((total + 2))
    done
  done
done

# 命令条数统计打到 stderr，不污染命令文件（stdout）。
echo "generated $total commands: ${#cases[@]} cases x ${#alphas[@]} alphas x $repeat repeats x 2 modes" 1>&2