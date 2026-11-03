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

# 附加一件：setsockopt 的 level 分派。
# IPPROTO_TCP 一度一律返 ENOPROTOOPT，而 OpenSSL 的 BIO_connect 无条件设
# TCP_NODELAY、失败就拒绝连接 —— 表现为 s_client 连不上；curl 又不检查返回值，
# 把缺口盖住了。这个测试盯的就是那 12 个 (level, optname) 组合。
python3 "$DIR/extract_sockopt.py" "$OUT/_sockopt.inc"
if gcc -std=gnu11 -O1 -Wall -I "$OUT" -o "$OUT/sockopt_test.exe" \
        "$DIR/host_test_sockopt.c" 2>"$OUT/cc_sockopt.log"; then
    "$OUT/sockopt_test.exe"
else
    echo "[sockopt] 编译失败："
    sed -n '1,40p' "$OUT/cc_sockopt.log"
    exit 1
fi
