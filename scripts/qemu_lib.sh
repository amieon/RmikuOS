#!/usr/bin/env bash
# RmikuOS QEMU 测试公共库 —— 用 `source` 引入，不单独执行。
#
# 存在理由:所有测试脚本(冒烟 / 崩溃一致性)都要做同一件事 ——
#   起 QEMU → 自动登录 → 发命令 → 匹配日志 → 关机/强杀
# 以前每个脚本各抄一份 QEMU 启动参数和交互函数, 加一块盘要改三处,
# 漏改就会出现"本地能跑、CI 挂了"这种极难联想到的坑。
# 这里把它们收成**唯一真源**: 启动参数只写一次, 编排逻辑只写一次。
#
# 用法(在 scripts/ 下的脚本里):
#   source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/qemu_lib.sh"
#   qemu_init riscv64                       # 设默认路径/超时
#   qemu_start                               # 起 QEMU
#   qemu_login                               # 自动登录,等 shell 提示符
#   send "run_all"; wait_for "[RUNALL] 汇总"
#
# 可调(环境变量):
#   ROOTFS_IMG / FAT_IMG / DATA_IMG / TIMEOUT_SEC / LOGIN_USER / LOGIN_PASS / SHELL_PROMPT

qemu_init() {
  local arch="$1"
  ARCH="$arch"
  SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[1]}")" && pwd)"
  REPO_ROOT="$(dirname "${SCRIPT_DIR}")"
  ROOTFS_IMG="${ROOTFS_IMG:-${REPO_ROOT}/target/fs-${arch}.img}"
  FAT_IMG="${FAT_IMG:-${REPO_ROOT}/target/fat-${arch}.img}"
  # 崩溃测试是破坏性的: 默认用 /tmp 下的临时盘, 不碰开发用的 target/data-*.img
  DATA_IMG="${DATA_IMG:-/tmp/rmiku_test_data_${arch}.img}"
  LOG="${LOG:-/tmp/rmiku_${arch}.log}"
  IN_FIFO="${IN_FIFO:-/tmp/rmiku_${arch}.in}"
  TIMEOUT_SEC="${TIMEOUT_SEC:-1800}"
  LOGIN_USER="${LOGIN_USER:-root}"
  LOGIN_PASS="${LOGIN_PASS:-root}"
  SHELL_PROMPT="${SHELL_PROMPT:-/home/root}"
  QEMU_PID=""
  DEADLINE=$(( $(date +%s) + TIMEOUT_SEC ))
}

# 生成 QEMU 启动命令(唯一真源: 三块盘的挂载顺序在此)
# 顺序与 run.sh 一致: blk0=rootfs(只读) / blk1=FAT / blk2=可写数据盘
qemu_make_cmd() {
  local arch="$1" data_img="$2"
  if [ "$arch" = "riscv64" ]; then
    QEMU_CMD="qemu-system-riscv64 -machine virt -cpu rv64 -accel tcg,thread=multi \
      -smp 8,cores=8,threads=1,sockets=1 -m 1G -nographic \
      -kernel ${REPO_ROOT}/target/riscv64gc-unknown-none-elf/debug/RmikuOS \
      -drive file=${ROOTFS_IMG},format=raw,if=none,id=blk0 -device virtio-blk-device,drive=blk0 \
      -drive file=${FAT_IMG},format=raw,if=none,id=blk1 -device virtio-blk-device,drive=blk1 \
      -drive file=${data_img},format=raw,if=none,id=blk2 -device virtio-blk-device,drive=blk2 \
      -netdev user,id=net0,hostfwd=tcp::8080-:8080,tftp=${REPO_ROOT}/tftpboot \
      -device virtio-net-pci,disable-legacy=on,netdev=net0,romfile= \
      -object filter-dump,id=f1,netdev=net0,file=/tmp/rmiku.pcap < ${IN_FIFO}"
  else
    QEMU_CMD="qemu-system-loongarch64 -machine virt -cpu la464 -m 2G \
      -accel tcg,thread=multi -smp 8,cores=8,threads=1,sockets=1 -nographic \
      -kernel ${REPO_ROOT}/target/loongarch64-unknown-none/debug/RmikuOS \
      -drive file=${ROOTFS_IMG},format=raw,if=none,id=blk0 -device virtio-blk-pci,drive=blk0,disable-legacy=on \
      -drive file=${FAT_IMG},format=raw,if=none,id=blk1 -device virtio-blk-pci,drive=blk1,disable-legacy=on \
      -drive file=${data_img},format=raw,if=none,id=blk2 -device virtio-blk-device,drive=blk2 \
      -netdev user,id=net0,hostfwd=tcp::8081-:8081,tftp=${REPO_ROOT}/tftpboot \
      -device virtio-net-pci,disable-legacy=on,netdev=net0,romfile= \
      -object filter-dump,id=f1,netdev=net0,file=/tmp/rmiku.pcap < ${IN_FIFO}"
  fi
}

qemu_start() {
  rm -f "${IN_FIFO}" "${LOG}"
  mkfifo "${IN_FIFO}"
  qemu_make_cmd "${ARCH}" "${DATA_IMG}"
  timeout "${TIMEOUT_SEC}" bash -c "${QEMU_CMD}" > "${LOG}" 2>&1 &
  QEMU_PID=$!
  exec 9>"${IN_FIFO}"        # 保持管道打开,否则 QEMU 读到 EOF 会立刻退出
}

qemu_send() { printf '%s\n' "$1" >&9; sleep 1; }

# 在日志里等关键字(二进制安全: -a);超时或 QEMU 提前退出则失败
qemu_wait_for() {
  local kw="$1" label="$2"
  while :; do
    if grep -aqF "${kw}" "${LOG}" 2>/dev/null; then
      echo "[test]   ✓ ${label}"
      return 0
    fi
    if ! kill -0 "${QEMU_PID}" 2>/dev/null; then
      echo "[test] ✗ QEMU 提前退出(未等到: ${label})"
      tail -40 "${LOG}"
      exit 1
    fi
    if [ "$(date +%s)" -ge "${DEADLINE}" ]; then
      echo "[test] ✗ 超时 ${TIMEOUT_SEC}s 未等到: ${label}"
      tail -40 "${LOG}"
      exit 1
    fi
    sleep 1
  done
}

# 拔电源式强杀:不给客机任何同步/卸载的机会
qemu_kill_hard() {
  kill -9 "${QEMU_PID}" 2>/dev/null || true
  pkill -9 -f qemu-system 2>/dev/null || true
  sleep 2
}

# 登录并等到 shell 提示符
qemu_login() {
  qemu_wait_for "RmikuOS login" "登录提示"
  qemu_send "${LOGIN_USER}"
  qemu_wait_for "Password" "密码提示"
  qemu_send "${LOGIN_PASS}"
  qemu_wait_for "${SHELL_PROMPT}" "shell 提示符"
}
