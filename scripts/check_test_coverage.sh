#!/usr/bin/env bash
# 构建期门禁：user/tests 下的每个测试源码都必须产出 ELF。
#
# 为什么需要这一层：run_all 只认识 /tests 里的**可执行文件**。
# 如果某个测试因为改名、编译失败后残留旧产物、或被误从目录里移走而没有
# 产出 ELF，run_all 只会"少跑一个"，汇总仍然 `0 failed` —— CI 全绿，
# 测试却已经悄悄丢了。这个脚本把"静默少跑"变成"构建期红灯"。
#
# 用法: bash scripts/check_test_coverage.sh <riscv64|loongarch64>

set -euo pipefail

ARCH="${1:?用法: check_test_coverage.sh <riscv64|loongarch64>}"
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD="${ROOT}/user/build/${ARCH}/tests"

if [ ! -d "${BUILD}" ]; then
  echo "✗ ${ARCH}: 构建目录不存在: ${BUILD}（user/build.py 没跑？）"
  exit 1
fi

total=0
missing=()

# 用 for + 显式存在性判断，避免 nullglob 依赖
for src in "${ROOT}"/user/tests/*.c "${ROOT}"/user/tests/*.cpp; do
  [ -e "${src}" ] || continue
  total=$((total + 1))
  stem="$(basename "${src}")"
  stem="${stem%.*}"

  # build.py 的产物命名是 <app_id>_<stem>.elf
  if ! compgen -G "${BUILD}/*_${stem}.elf" >/dev/null; then
    missing+=("${stem}")
  fi
done

if [ "${total}" -eq 0 ]; then
  echo "✗ ${ARCH}: user/tests 下没找到任何 .c/.cpp，请检查路径"
  exit 1
fi

if [ "${#missing[@]}" -gt 0 ]; then
  echo "✗ ${ARCH}: ${#missing[@]}/${total} 个测试没有产出 ELF，run_all 会静默漏跑它们："
  printf '    - %s\n' "${missing[@]}"
  exit 1
fi

echo "✓ ${ARCH}: ${total} 个测试源码全部产出 ELF"
