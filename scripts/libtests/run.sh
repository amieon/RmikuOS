#!/usr/bin/env bash
# 用户态基础件（memcpy/memset/fread）宿主回归。
# 被测代码是从 user/lib/string.c 与 user/include/file.h 抽出的原文，不复制实现。
set -euo pipefail

DIR="$(cd "$(dirname "$0")" && pwd)"
OUT="$DIR/.build"
mkdir -p "$OUT"

python3 "$DIR/extract.py" "$OUT/_extracted.inc"

if ! gcc -std=gnu11 -O1 -Wall -Wextra -I "$OUT" -o "$OUT/lib_test.exe" "$DIR/host_test_lib.c" 2>"$OUT/cc.log"; then
    echo "[lib] 编译失败："
    sed -n '1,40p' "$OUT/cc.log"
    exit 1
fi
grep -E "warning:" "$OUT/cc.log" | grep -v "unused parameter" | head -5 || true

"$OUT/lib_test.exe"
