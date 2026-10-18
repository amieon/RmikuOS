#!/usr/bin/env bash
# 运行期门禁：每个**已编译**的测试都必须在 run_all 输出里出现且 PASS。
#
# check_test_coverage.sh 管的是"源码 → ELF"，这个脚本管"ELF → 真的跑了并通过"。
# 两者互补，缺一个都可能出现"CI 全绿但某个测试没跑"：
#   - 只查 ELF 数量：测试跑挂了但数量对得上 → 漏
#   - 只查 failed==0：测试压根没进 /tests（比如 mkfs 漏拷）→ 漏
#
# 判定方式不是比数量（/tests 里还有 mkfs 额外放的 sqlite_test 之类），
# 而是**逐个名字核对**：每个 ELF 的 clean 名字都要在日志里有一行
#   [RUNALL] <name> ... PASS
#
# 用法: bash scripts/verify_runall.sh <riscv64|loongarch64> <qemu日志路径>

set -euo pipefail

ARCH="${1:?用法: verify_runall.sh <arch> <log>}"
LOG="${2:?用法: verify_runall.sh <arch> <log>}"
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD="${ROOT}/user/build/${ARCH}/tests"

if [ ! -f "${LOG}" ]; then
  echo "✗ ${ARCH}: 日志不存在: ${LOG}"
  exit 1
fi

checked=0
passed=0
skipped=0
missing=()

for elf in "${BUILD}"/*.elf; do
  [ -e "${elf}" ] || continue
  base="$(basename "${elf}" .elf)"
  # 与 user/mkfs_ext4.sh 的改名规则保持一致：去掉开头的 "123_"
  clean="$(printf '%s' "${base}" | sed -E 's/^([0-9]+_)+//')"
  checked=$((checked + 1))

  # -a: 日志里可能有二进制字节；-F: 名字里有 [] 之类字符时按字面匹配。
  # SKIP 也算"这一项有交代"：它表示测试自己判定本次不具备运行条件
  # (例如 net_backlog_test 需要宿主机跑 nc)。但会单独计数, 不混进 PASS。
  if grep -aqF "[RUNALL] ${clean} ... PASS" "${LOG}"; then
    passed=$((passed + 1))
  elif grep -aqF "[RUNALL] ${clean} ... SKIP" "${LOG}"; then
    skipped=$((skipped + 1))
  else
    missing+=("${clean}")
  fi
done

if [ "${checked}" -eq 0 ]; then
  echo "✗ ${ARCH}: 没有找到任何测试 ELF，检查 ${BUILD}"
  exit 1
fi

if [ "${#missing[@]}" -gt 0 ]; then
  echo "✗ ${ARCH}: ${#missing[@]}/${checked} 个已编译的测试既没有 PASS 也没有 SKIP："
  printf '    - %s\n' "${missing[@]}"
  echo "  常见原因：没被拷进镜像(/tests)、exec 失败、卡住超时、或退出码非 0。"
  exit 1
fi

echo "✓ ${ARCH}: ${checked} 个已编译测试全部有交代 — ${passed} PASS, ${skipped} SKIP"
if [ "${skipped}" -gt 0 ]; then
  echo "  注意: SKIP 表示该测试本次没跑(通常需要宿主机配合), 不算验证过。"
fi
