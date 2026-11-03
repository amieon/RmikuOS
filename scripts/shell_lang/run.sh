#!/usr/bin/env bash
# 宿主侧回归：验证 shell 的复合命令（if/for/while）词法与执行。
#
# 不做"复制实现再测"——直接从 user/src/shell.c 抽出真实代码段编译，
# 所以测试永远跟着实现走，不会漂移。
#   host_test_lex.c   扫描器：关键字深度、语句切分、关键字配对
#   host_test_exec.c  执行层：循环/分支/break/continue/$?，底层命令用桩
#
# 只用 gcc + sed + grep，Windows Git Bash 下可直接跑。
set -e

ROOT=$(cd "$(dirname "$0")/../.." && pwd)
SRC="$ROOT/user/src/shell.c"
OUT="$ROOT/scripts/shell_lang"

extract() {   # extract <起始锚点> <结束锚点> <输出文件>
    local s e
    s=$(grep -n "$1" "$SRC" | head -1 | cut -d: -f1)
    e=$(grep -n "$2" "$SRC" | head -1 | cut -d: -f1)
    if [ -z "$s" ] || [ -z "$e" ]; then
        echo "锚点失效: $1 / $2" >&2
        exit 1
    fi
    sed -n "${s},$((e-2))p" "$SRC" > "$3"
}

extract '^#define CTL_BREAK' '^struct job {' "$OUT/_scan.inc"
extract '^static char \*skip_ws' '^static int builtin_source(const char \*path) {' "$OUT/_exec.inc"
extract '^struct redir_op {' '^static void apply_redirect_and_exec' "$OUT/_redir.inc"

CC=${CC:-gcc}
rc=0
"$CC" -std=gnu11 -O1 -o "$OUT/lex.exe"  "$OUT/host_test_lex.c"  || rc=1
[ $rc -eq 0 ] && { "$OUT/lex.exe"  || rc=1; }
"$CC" -std=gnu11 -O1 -o "$OUT/exec.exe" "$OUT/host_test_exec.c" || rc=1
[ $rc -eq 0 ] && { "$OUT/exec.exe" || rc=1; }
"$CC" -std=gnu11 -O1 -o "$OUT/redir.exe" "$OUT/host_test_redir.c" || rc=1
[ $rc -eq 0 ] && { "$OUT/redir.exe" || rc=1; }

rm -f "$OUT/_scan.inc" "$OUT/_exec.inc" "$OUT/_redir.inc" \
      "$OUT/lex.exe" "$OUT/exec.exe" "$OUT/redir.exe"
exit $rc
