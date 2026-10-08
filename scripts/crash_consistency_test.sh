#!/usr/bin/env bash
# RmikuOS 可写 ext4 崩溃一致性测试(在 CI 的 Ubuntu 机器上跑,不是在你的 OS 里!)
#
# 它验证的**不是**"数据不丢"(窗口期内丢是设计使然),而是:
#   **崩溃后文件系统依然一致 —— 能挂载、元数据没烂、已 sync 的数据还在。**
# 这正是 JBD2 的唯一承诺,也是本项目"可写 ext4"这一章的核心价值。
#
# 流程:
#   1) 用 /tmp 下一块全新的临时数据盘启动(不碰开发用的 target/data-*.img)
#   2) 首次开机:内核发现空盘 → 内核内 mkfs(写卷标 RMikuOS-DATA)→ 挂 /data
#      → ensure_layout 建出 home/ 与 var/etc/ → bind 到 /home 和 /var
#   3) 写 marker.txt,并等到日志出现 writeback ok(确认已落盘)
#   4) 跑 bigwrite,写到一半时 kill -9(模拟掉电,不是优雅关机)
#   5) 用同一块盘重启,断言:
#        a. /data 挂载成功 → journal 自动回放、元数据一致
#        b. /home 重新绑定成功
#        c. marker.txt 与 big.bin 都还在
#   6) 宿主侧 e2fsck -fn 校验磁盘结构干净
#
# 用法:
#   bash scripts/crash_consistency_test.sh <riscv64|loongarch64>
#
# 环境变量(可选):
#   CRASH_DATA_IMG  临时数据盘路径(默认 /tmp/rmiku_test_data_<arch>.img)
#   CRASH_TIMEOUT   总超时秒数,默认 1800
#   BIGWRITE_MB     bigwrite 目标大小 MB,默认 6
#   KILL_AT_MB     写到多少 MB 时 kill,默认 3(即"写到一半")
#   SKIP_E2FSCK=1  跳过 e2fsck(宿主机没装 e2fsprogs 时)

set -euo pipefail

source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/qemu_lib.sh"
qemu_init "${1:?用法: crash_consistency_test.sh <riscv64|loongarch64>}"

TIMEOUT_SEC="${CRASH_TIMEOUT:-1800}"
DATA_IMG="${CRASH_DATA_IMG:-/tmp/rmiku_test_data_${ARCH}.img}"
BIGWRITE_MB="${BIGWRITE_MB:-6}"
KILL_AT_MB="${KILL_AT_MB:-3}"

PASS=0
FAIL=0
ok()  { echo "[crash]   ✓ $1"; PASS=$((PASS+1)); }
bad() { echo "[crash] ✗ $1";   FAIL=$((FAIL+1)); }

# 每次用全新的数据盘:既验证"首次开机内核内 mkfs"这条自举路径,
# 也保证测试之间互不影响。放在 /tmp,绝不碰 target/data-*.img。
rm -f "${DATA_IMG}"
truncate -s 64M "${DATA_IMG}"
echo "[crash] ${ARCH}: 临时数据盘 ${DATA_IMG} (64M, 全新)"

echo "[crash] ${ARCH}: 第一轮启动(应触发内核内 mkfs 自举)..."
qemu_start
qemu_login
ok "首次启动进入 shell"

# bind mount 日志是"盘已正确识别并暴露"的直接证据
if grep -aqF "/home ← /data/home (bind)" "${LOG}"; then ok "/home 由 /data/home 绑定而来"; else bad "未见到 /home 绑定日志"; fi
if grep -aqF "/var ← /data/var (bind)" "${LOG}";  then ok "/var 由 /data/var 绑定而来";  else bad "未见到 /var 绑定日志"; fi

echo "[crash] ${ARCH}: 写 marker.txt,等待 writeback 落盘..."
send "echo persisted > /home/root/marker.txt"
qemu_wait_for "writeback ok" "writeback 周期落盘"
sleep 3
ok "marker.txt 已随 writeback 落盘"

echo "[crash] ${ARCH}: 启动 bigwrite(${BIGWRITE_MB}MB),写到 ${KILL_AT_MB}MB 时 kill -9..."
send "bigwrite /home/root/big.bin ${BIGWRITE_MB}"
qemu_wait_for "已写 ${KILL_AT_MB} /" "bigwrite 进度 ${KILL_AT_MB}MB"
qemu_kill_hard
ok "已在写入中途 kill -9(模拟掉电)"

echo "[crash] ${ARCH}: 第二轮启动(同一块盘,验证 journal 回放)..."
qemu_start
qemu_login
ok "崩溃后仍能启动并进入 shell"

if grep -aqF "数据盘挂载失败" "${LOG}"; then bad "数据盘挂载失败(journal 回放没救回来)"; else ok "/data 挂载成功(journal 已回放)"; fi
if grep -aqF "/home ← /data/home (bind)" "${LOG}"; then ok "/home 重新绑定成功"; else bad "/home 未绑定"; fi

send "ls /home/root"
sleep 3
if grep -aqF "marker.txt" "${LOG}"; then ok "marker.txt 跨崩溃仍存在(writeback 有效)"; else bad "marker.txt 丢失"; fi
if grep -aqF "big.bin"   "${LOG}"; then ok "big.bin 存在(崩溃瞬间已落盘的部分)";   else bad "big.bin 完全丢失"; fi

# 正常关机,让盘处于干净状态便于宿主侧校验
send "shutdown"
sleep 5
qemu_kill_hard

# ==================== 宿主侧:e2fsck 认证 ====================
# rsext4 在内核里从 mkfs 到每笔写产生的镜像,交给 e2fsprogs 参考实现检验。
if [ "${SKIP_E2FSCK:-0}" = "1" ] || ! command -v e2fsck >/dev/null 2>&1; then
  echo "[crash] ${ARCH}: 跳过 e2fsck(未安装 e2fsprogs 或 SKIP_E2FSCK=1)"
else
  echo "[crash] ${ARCH}: 宿主侧 e2fsck -fn 校验数据盘..."
  if e2fsck -fn "${DATA_IMG}" > "/tmp/crash_${ARCH}_e2fsck.log" 2>&1; then
    ok "e2fsck 检查通过(rsext4 写出的 ext4 通过参考实现认证)"
  else
    bad "e2fsck 报告错误,见 /tmp/crash_${ARCH}_e2fsck.log"
    cat "/tmp/crash_${ARCH}_e2fsck.log"
  fi
fi

echo
echo "[crash] ${ARCH}: 汇总 —— ${PASS} 项通过, ${FAIL} 项失败"
if [ "${FAIL}" -ne 0 ]; then
  echo "[crash] ✗ 崩溃一致性测试失败"
  exit 1
fi
echo "[crash] ✓ 崩溃一致性测试全部通过"
exit 0
