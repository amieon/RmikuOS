#!/usr/bin/env bash
# run_sched_exp.sh —— 调度实验自动采集(exp6-9)
#
# 复用 scripts/smoke_test.sh 的 FIFO 自动登录机制(root/root)。
# QEMU -smp 8, 但 OS 只起 hart 0("[ WARN] failed to start hart 1..7"), 实际单核。
# ⚠️ 网络参数必须保留(内核强制要求 virtio-net, 否则 net not initialized panic)。
#
# 无超时: 一个 sexp 程序内部循环跑完该实验所有 run(每 run 240000 tick ≈ 56min),
#   sexp6_cubic 36 run ≈ 34h / sexp7_optims 48 run ≈ 45h
#   sexp8_pid   24 run ≈ 22h / sexp9_ucb   12 run ≈ 11h
# 脚本只等 "ALL DONE" 结束标记, 中途每 30 分钟报一次进度; 只有 QEMU 提前退出才报错。
#
# 用法:
#   bash scripts/sched/run_sched_exp.sh                       # 默认四个实验
#   bash scripts/sched/run_sched_exp.sh "/sched/sexp6_cubic|logs/cubic/sexp6_cubic.csv"

set -euo pipefail

ARCH="${1:-riscv64}"
shift || true

EXPERIMENTS=()
if [ "$#" -gt 0 ]; then
    EXPERIMENTS=("$@")
else
    EXPERIMENTS=(
        "/sched/sexp6_cubic|logs/cubic/sexp6_cubic.csv"
        "/sched/sexp7_optims|logs/optims/sexp7_optims.csv"
        "/sched/sexp8_pid|logs/pid/sexp8_pid.csv"
        "/sched/sexp9_ucb|logs/ucb/sexp9_ucb.csv"
    )
fi

LOGIN_USER="${LOGIN_USER:-root}"
LOGIN_PASS="${LOGIN_PASS:-root}"
SHELL_PROMPT="${SHELL_PROMPT:-/home/root}"
DONE_MARK="${DONE_MARK:-ALL DONE}"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
cd "${REPO_ROOT}"

echo "=== 调度实验自动采集(无超时) ==="
echo "ARCH=$ARCH  实验数=${#EXPERIMENTS[@]}"
for e in "${EXPERIMENTS[@]}"; do
    echo "  ${e%%|*}  →  ${e##*|}"
done
echo

build_qemu_cmd() {
  if [ "${ARCH}" = "riscv64" ]; then
    echo "qemu-system-riscv64 -machine virt -cpu rv64 -accel tcg,thread=multi \
    -smp 8,cores=8,threads=1,sockets=1 -m 1G -nographic \
    -kernel target/riscv64gc-unknown-none-elf/debug/RmikuOS \
    -drive file=target/fs-riscv64.img,format=raw,if=none,id=blk0 -device virtio-blk-device,drive=blk0 \
    -drive file=target/fat-riscv64.img,format=raw,if=none,id=blk1 -device virtio-blk-device,drive=blk1 \
    -netdev user,id=net0,hostfwd=tcp::8080-:8080,tftp=${REPO_ROOT}/tftpboot \
    -device virtio-net-pci,disable-legacy=on,netdev=net0,romfile= \
    -object filter-dump,id=f1,netdev=net0,file=/tmp/rmiku.pcap"
  else
    echo "qemu-system-loongarch64 -machine virt -cpu la464 -m 2G \
    -accel tcg,thread=multi -smp 8,cores=8,threads=1,sockets=1 -nographic \
    -kernel target/loongarch64-unknown-none/debug/RmikuOS \
    -drive file=target/fs-loongarch64.img,format=raw,if=none,id=blk0 -device virtio-blk-pci,drive=blk0,disable-legacy=on \
    -drive file=target/fat-loongarch64.img,format=raw,if=none,id=blk1 -device virtio-blk-pci,drive=blk1,disable-legacy=on \
    -netdev user,id=net0,hostfwd=tcp::8081-:8081,tftp=${REPO_ROOT}/tftpboot \
    -device virtio-net-pci,disable-legacy=on,netdev=net0,romfile= \
    -object filter-dump,id=f1,netdev=net0,file=/tmp/rmiku.pcap"
  fi
}
QEMU_CMD="$(build_qemu_cmd)"

run_one() {
  local cmd="$1" csv="$2"
  local log="${csv%.csv}.log"
  local in_fifo="/tmp/sched_$(echo "$csv" | tr '/' '_').in"
  local start_ts=$(date +%s)

  mkdir -p "$(dirname "$csv")"
  echo "=== [${csv}] 启动 QEMU 跑: ${cmd} ==="
  rm -f "${log}" "${in_fifo}"
  mkfifo "${in_fifo}"

  ( bash -c "${QEMU_CMD} < ${in_fifo}" 2>&1 | tee "${log}" ) &
  local pipe_pid=$!
  exec 9>"${in_fifo}"

  send() { printf '%s\n' "$1" >&9; sleep 1; }

  cleanup() {
    exec 9>&- 2>/dev/null || true
    kill "${pipe_pid}" 2>/dev/null || true
    sleep 1
    pkill -f "qemu-system-${ARCH}" 2>/dev/null || true
  }

  qemu_alive() { pgrep -f "qemu-system-${ARCH}" >/dev/null 2>&1; }

  # 等关键字; 无超时, 只靠 QEMU 提前退出判定失败
  wait_for() {
    local kw="$1" label="$2"
    local last_report=0
    while :; do
      grep -qF "${kw}" "${log}" 2>/dev/null && { echo "    ✓ ${label}"; return 0; }
      if ! qemu_alive; then
        echo "    ✗ QEMU 提前退出(未等到: ${label})"; tail -30 "${log}"; return 1
      fi
      # 每 30 分钟报一次进度
      local now=$(date +%s)
      if [ $(( now - last_report )) -ge 1800 ]; then
        last_report=$now
        local latest elapsed
        latest=$(grep -aE '^# RUN ' "${log}" 2>/dev/null | tail -1)
        elapsed=$(( (now - start_ts) / 60 ))
        echo "    … 已跑 ${elapsed} 分钟, 最新: ${latest:-（启动中）}"
      fi
      sleep 1
    done
  }

  wait_for "RmikuOS login" "登录提示" || { cleanup; return 1; }
  send "${LOGIN_USER}"
  wait_for "Password" "密码提示" || { cleanup; return 1; }
  send "${LOGIN_PASS}"
  wait_for "${SHELL_PROMPT}" "shell 提示符" || { cleanup; return 1; }

  echo "    → 发送实验命令: ${cmd}"
  send "${cmd}"
  wait_for "${DONE_MARK}" "实验结束" || { cleanup; return 1; }
  sleep 2

  cleanup

  grep -aE '^(#|W,|D,|A,|S,|J,|K,)' "${log}" > "${csv}" || true
  local nlines
  nlines=$(grep -acE '^(W,|D,|A,|S,|J,|K,)' "${csv}")
  echo "    ✓ 完成: ${csv} (数据行 ${nlines})"
  echo
}

for e in "${EXPERIMENTS[@]}"; do
  cmd="${e%%|*}"; csv="${e##*|}"
  if ! run_one "${cmd}" "${csv}"; then
    echo "!! [${csv}] 失败, 继续下一个"
  fi
done

echo "=== 全部完成 ==="
for e in "${EXPERIMENTS[@]}"; do
  [ -f "${e##*|}" ] && echo "  ${e##*|}"
done
echo
echo "出图(图写到 csv 同目录):"
echo "  python3 scripts/sched/stat_exp6_cubic.py logs/cubic/sexp6_cubic.csv logs/sched/phase/sexp5_phase.csv logs/sched/dyn/sexp4_dyn.csv"
echo "  python3 scripts/sched/stat_exp7_optims.py logs/optims/sexp7_optims.csv"
echo "  python3 scripts/sched/stat_exp8_pid.py logs/pid/sexp8_pid.csv logs/sched/phase/sexp5_phase.csv logs/sched/dyn/sexp4_dyn.csv"
echo "  python3 scripts/sched/stat_exp9_ucb.py logs/ucb/sexp9_ucb.csv"
