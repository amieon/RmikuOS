#!/usr/bin/env bash
# RmikuOS QEMU 冒烟测试(在 CI 的 Ubuntu 机器上跑,不是在你的 OS 里!)
#
# 逻辑:启动 QEMU → 自动登录(root/root)→ 跑 run_all 回归 → 检查汇总 → shutdown 关机
#   - 编译 sqlite3 等导致启动较慢,默认总超时 1800 秒(30 分钟)
#   - 崩溃一致性测试在 scripts/crash_consistency_test.sh(需 kill -9,无法合并到本轮启动)
#
# 用法:
#   bash scripts/smoke_test.sh <riscv64|loongarch64>
#
# 环境变量(可选):
#   SMOKE_TIMEOUT  总超时秒数,默认 1800
#   SHUTDOWN_WAIT  shutdown 后等待系统关机的最长秒数,默认 30
#   LOGIN_USER     登录账号,默认 root
#   LOGIN_PASS     登录密码,默认 root
#   SHELL_PROMPT   判定"已进入 shell"的提示符关键字,默认 /home/root

set -euo pipefail

source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/qemu_lib.sh"
qemu_init "${1:?用法: smoke_test.sh <riscv64|loongarch64>}"
TIMEOUT_SEC="${SMOKE_TIMEOUT:-${TIMEOUT_SEC}}"

echo "[smoke] ${ARCH}: 启动 QEMU(总超时 ${TIMEOUT_SEC}s)..."
qemu_start
qemu_login
echo "[smoke] ${ARCH}: 已进入 shell ✓"

# ---- 回归测试:run_all 一键执行 36 个测试 ----
send "run_all"
qemu_wait_for "[RUNALL] 汇总" "run_all 汇总"

# -a:测试输出可能含二进制字节,不加会把日志当 binary 导致 grep 不给匹配行
summary="$(grep -a '\[RUNALL\] 汇总' "${LOG}" | tail -1)"
echo "[smoke] ${ARCH}: ${summary}"
failed_count="$(echo "${summary}" | sed -E 's/.* ([0-9]+) failed.*/\1/')"
if [ -z "${failed_count}" ] || [ "${failed_count}" != "0" ]; then
  echo "[smoke] ✗ 回归测试存在失败: ${summary}"
  exit 1
fi
echo "[smoke] ${ARCH}: 回归测试全部通过 ✓"

# 让系统正常关机,顺带验证关机路径
echo "[smoke] ${ARCH}: 发送 shutdown,等待系统关机..."
send "shutdown"
SHUTDOWN_WAIT="${SHUTDOWN_WAIT:-30}"
waited=0
while [ "${waited}" -lt "${SHUTDOWN_WAIT}" ] && kill -0 "${QEMU_PID}" 2>/dev/null; do
  sleep 1
  waited=$((waited + 1))
done
if kill -0 "${QEMU_PID}" 2>/dev/null; then
  echo "[smoke] ${ARCH}: 警告: shutdown 在 ${SHUTDOWN_WAIT}s 内未生效(占位?),已强制结束"
  qemu_kill_hard
fi
exit 0
