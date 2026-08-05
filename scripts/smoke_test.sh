#!/usr/bin/env bash
# RmikuOS QEMU 冒烟测试(在 CI 的 Ubuntu 机器上跑,不是在你的 OS 里!)
#
# 逻辑:启动 QEMU → 自动登录(root/root)→ 看到 shell 提示符就算成功
#   - 编译 sqlite3 等导致启动较慢,默认总超时 360 秒(6 分钟)
#   - 需要登录:出现 "RmikuOS login:" 输账号 root,出现 "Password:" 输密码 root
#
# 用法:
#   bash scripts/smoke_test.sh <riscv64|loongarch64>
#
# 环境变量(可选):
#   SMOKE_TIMEOUT  总超时秒数,默认 360
#   SHUTDOWN_WAIT  shutdown 后等待系统关机的最长秒数,默认 30
#   LOGIN_USER     登录账号,默认 root
#   LOGIN_PASS     登录密码,默认 root
#   SHELL_PROMPT   判定"已进入 shell"的提示符关键字,默认 /home/root

set -euo pipefail

ARCH="${1:?用法: smoke_test.sh <riscv64|loongarch64>}"
LOG="/tmp/smoke_${ARCH}.log"
IN_FIFO="/tmp/smoke_${ARCH}.in"
TIMEOUT_SEC="${SMOKE_TIMEOUT:-360}"
LOGIN_USER="${LOGIN_USER:-root}"
LOGIN_PASS="${LOGIN_PASS:-root}"
SHELL_PROMPT="${SHELL_PROMPT:-/home/root}"

rm -f "${LOG}" "${IN_FIFO}"
mkfifo "${IN_FIFO}"   # 命名管道:往它写内容 = 向 QEMU 里的 OS 输入

# 在仓库根目录运行本脚本(target/ 相对路径以仓库根为基准)
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(dirname "${SCRIPT_DIR}")"

# 与 run.sh 的 QEMU 启动参数一致(debug 模式、NET=user),仅去掉 exec,
# 并把 stdin 接到 FIFO 以便脚本自动登录;tftpboot 用仓库根的绝对路径
if [ "${ARCH}" = "riscv64" ]; then
  QEMU_CMD="qemu-system-riscv64 -machine virt -cpu rv64 -accel tcg,thread=multi \
    -smp 8,cores=8,threads=1,sockets=1 -m 1G -nographic \
    -kernel target/riscv64gc-unknown-none-elf/debug/RmikuOS \
    -drive file=target/fs-riscv64.img,format=raw,if=none,id=blk0 -device virtio-blk-device,drive=blk0 \
    -drive file=target/fat-riscv64.img,format=raw,if=none,id=blk1 -device virtio-blk-device,drive=blk1 \
    -netdev user,id=net0,hostfwd=tcp::8080-:8080,tftp=${REPO_ROOT}/tftpboot \
    -device virtio-net-pci,disable-legacy=on,netdev=net0,romfile= \
    -object filter-dump,id=f1,netdev=net0,file=/tmp/rmiku.pcap < ${IN_FIFO}"
else
  QEMU_CMD="qemu-system-loongarch64 -machine virt -cpu la464 -m 2G \
    -accel tcg,thread=multi -smp 8,cores=8,threads=1,sockets=1 -nographic \
    -kernel target/loongarch64-unknown-none/debug/RmikuOS \
    -drive file=target/fs-loongarch64.img,format=raw,if=none,id=blk0 -device virtio-blk-pci,drive=blk0,disable-legacy=on \
    -drive file=target/fat-loongarch64.img,format=raw,if=none,id=blk1 -device virtio-blk-pci,drive=blk1,disable-legacy=on \
    -netdev user,id=net0,hostfwd=tcp::8081-:8081,tftp=${REPO_ROOT}/tftpboot \
    -device virtio-net-pci,disable-legacy=on,netdev=net0,romfile= \
    -object filter-dump,id=f1,netdev=net0,file=/tmp/rmiku.pcap < ${IN_FIFO}"
fi

deadline=$(( $(date +%s) + TIMEOUT_SEC ))

# 等待日志里出现关键字;超时或 QEMU 退出则失败
wait_for() {
  local kw="$1" label="$2"
  while :; do
    grep -qF "${kw}" "${LOG}" 2>/dev/null && { echo "[smoke]   ✓ ${label}"; return 0; }
    if ! kill -0 "${QEMU_PID}" 2>/dev/null; then
      echo "[smoke] ✗ QEMU 提前退出(还没等到: ${label})"
      tail -50 "${LOG}"
      return 1
    fi
    if [ "$(date +%s)" -ge "${deadline}" ]; then
      echo "[smoke] ✗ 超时 ${TIMEOUT_SEC}s 未等到: ${label}"
      tail -50 "${LOG}"
      return 1
    fi
    sleep 1
  done
}

echo "[smoke] ${ARCH}: 启动 QEMU(总超时 ${TIMEOUT_SEC}s)..."
timeout "${TIMEOUT_SEC}" bash -c "${QEMU_CMD}" > "${LOG}" 2>&1 &
QEMU_PID=$!
exec 9>"${IN_FIFO}"   # 保持管道打开,防止 QEMU 读到 EOF 提前退出

send() {
  printf '%s\n' "$1" >&9
  sleep 1
}

wait_for "RmikuOS login" "登录提示" || exit 1
echo "[smoke] ${ARCH}: 输入账号 ${LOGIN_USER}"
send "${LOGIN_USER}"

wait_for "Password" "密码提示" || exit 1
echo "[smoke] ${ARCH}: 输入密码"
send "${LOGIN_PASS}"

wait_for "${SHELL_PROMPT}" "shell 提示符" || exit 1
echo "[smoke] ${ARCH}: 已进入 shell ✓"

# 进入 shell 后发 shutdown,让系统正常关机、QEMU 自然退出(顺带验证关机功能)。
# riscv64 的 shutdown 会写 SiFive Test finisher 使 QEMU 退出;
# loongarch64 若 shutdown 仍是占位(只打印+自旋),QEMU 不会退出,
# 由下方 SHUTDOWN_WAIT 兜底强制结束,不影响冒烟结果(只打警告)。
echo "[smoke] ${ARCH}: 发送 shutdown,等待系统关机..."
send "shutdown"

SHUTDOWN_WAIT="${SHUTDOWN_WAIT:-30}"
waited=0
while [ "${waited}" -lt "${SHUTDOWN_WAIT}" ] && kill -0 "${QEMU_PID}" 2>/dev/null; do
  sleep 1
  waited=$((waited + 1))
done

if ! kill -0 "${QEMU_PID}" 2>/dev/null; then
  code=0
  wait "${QEMU_PID}" || code=$?
  if [ "${code}" -eq 0 ]; then
    echo "[smoke] ${ARCH}: 系统正常关机,QEMU 干净退出 ✓"
  else
    echo "[smoke] ${ARCH}: 关机后 QEMU 退出码=${code}(非 0,仅供参考)"
  fi
else
  echo "[smoke] ${ARCH}: 警告: shutdown 在 ${SHUTDOWN_WAIT}s 内未生效(loongarch 占位?),已强制结束"
  kill -TERM "${QEMU_PID}" 2>/dev/null || true
  pkill -f "qemu-system-${ARCH}" 2>/dev/null || true
fi

echo "[smoke] ${ARCH}: 冒烟通过 ✓"
exit 0