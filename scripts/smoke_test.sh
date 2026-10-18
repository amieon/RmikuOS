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
# 冒烟测试每次都从**空白数据盘**开始: 免得上一次跑挂留下的半成品盘
# 让这一轮出现"上次的错"这种最难查的 flaky。崩溃测试需要跨启动复用, 不受影响。
DATA_IMG_FRESH="${DATA_IMG_FRESH:-1}"

echo "[smoke] ${ARCH}: 启动 QEMU(总超时 ${TIMEOUT_SEC}s)..."
qemu_start
qemu_login
echo "[smoke] ${ARCH}: 已进入 shell ✓"

# ---- 回归测试:run_all 一键执行全部测试 ----
qemu_send "run_all"

# net_backlog_test 是"手工联测": 它先 listen(8080), 然后等**宿主机**来连两条 nc
# (要复现 backlog 满 → SYN 被丢 → 重传成功的时序, 必须由外部发起连接)。
# CI 里没人坐在键盘前, 所以这里在后台守候它的 READY 标记, 一出现就替人补上。
#
# 注: 没有 nc 的环境不致命 —— 测试会自己打印 SKIP(退出码 77), run_all 记成 SKIP,
#     不会伪装成 PASS, 也不会挂住整个回归(以前就是因为 accept() 阻塞把 CI 拖到超时)。
NC_WATCH_PID=""
if command -v nc >/dev/null 2>&1; then
  (
    while kill -0 "${QEMU_PID}" 2>/dev/null; do
      if grep -aqF "READY: run on host now" "${LOG}" 2>/dev/null; then
        echo "[smoke] ${ARCH}: 检测到 net_backlog READY → 宿主侧连 127.0.0.1:${HOST_FWD_PORT}"
        if nc -z 127.0.0.1 "${HOST_FWD_PORT}" >/dev/null 2>&1; then
          echo "[smoke]   nc#1 已连上(占用唯一 backlog 名额后立刻断开)"
        fi
        # 第二条: 此刻 backlog 已满, SYN 会被丢; 靠内核重传在 5~10s 后握手成功
        if nc -z -w 25 127.0.0.1 "${HOST_FWD_PORT}" >/dev/null 2>&1; then
          echo "[smoke]   nc#2 已连上(SYN 重传后补进 backlog)"
        fi
        break
      fi
      sleep 1
    done
  ) &
  NC_WATCH_PID=$!
else
  echo "[smoke] ${ARCH}: 宿主没有 nc, net_backlog_test 将自行 SKIP"
fi

qemu_wait_for "[RUNALL] 汇总" "run_all 汇总"

if [ -n "${NC_WATCH_PID}" ]; then
  kill "${NC_WATCH_PID}" 2>/dev/null || true
  wait "${NC_WATCH_PID}" 2>/dev/null || true
fi

# -a:测试输出可能含二进制字节,不加会把日志当 binary 导致 grep 不给匹配行
summary="$(grep -a '\[RUNALL\] 汇总' "${LOG}" | tail -1)"
echo "[smoke] ${ARCH}: ${summary}"
failed_count="$(echo "${summary}" | sed -E 's/.* ([0-9]+) failed.*/\1/')"
if [ -z "${failed_count}" ] || [ "${failed_count}" != "0" ]; then
  echo "[smoke] ✗ 回归测试存在失败: ${summary}"
  exit 1
fi
echo "[smoke] ${ARCH}: 回归测试全部通过 ✓"

# "0 failed" 还不够 —— 如果某个测试压根没进 /tests，它连 FAIL 都不会有。
# 这里逐个核对：每个已编译测试都必须在 PASS 列表里出现。
bash "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/verify_runall.sh" "${ARCH}" "${LOG}"

# 让系统正常关机,顺带验证关机路径
echo "[smoke] ${ARCH}: 发送 shutdown,等待系统关机..."
qemu_send "shutdown"
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
