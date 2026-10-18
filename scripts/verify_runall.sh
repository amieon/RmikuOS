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
missing=()

for elf in "${BUILD}"/*.elf; do
  [ -e "${elf}" ] || continue
  base="$(basename "${elf}" .elf)"
  # 与 user/mkfs_ext4.sh 的改名规则保持一致：去掉开头的 "123_"
  clean="$(printf '%s' "${base}" | sed -E 's/^([0-9]+_)+//')"
  checked=$((checked + 1))

  # -a: 日志里可能有二进制字节；-F: 名字里有 [] 之类字符时按字面匹配
  if ! grep -aqF "[RUNALL] ${clean} ... PASS" "${LOG}"; then
    missing+=("${clean}")
  fi
done

if [ "${checked}" -eq 0 ]; then
  echo "✗ ${ARCH}: 没有找到任何测试 ELF，检查 ${BUILD}"
  exit 1
fi

if [ "${#missing[@]}" -gt 0 ]; then
  echo "✗ ${ARCH}: ${#missing[@]}/${checked} 个已编译的测试没有出现在 PASS 列表里："
  printf '    - %s\n' "${missing[@]}"
  echo "  常见原因：没被拷进镜像(/tests)、exec 失败、卡住超时、或退出码非 0。"
  exit 1
fi

echo "✓ ${ARCH}: ${checked} 个已编译测试全部在 run_all 中 PASS"
