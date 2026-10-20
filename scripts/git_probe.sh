#!/usr/bin/env bash
#
# git 移植的分级试编工具（在 Linux 开发树上跑，需要交叉工具链）
#
#   bash scripts/git_probe.sh riscv64 probe     # 只编 3 个文件，压头文件/配置面
#   bash scripts/git_probe.sh riscv64 lib       # make libgit.a（核心对象，不含 builtin）
#
# 为什么要分级：直接 make 会一次吐几百条错误，绝大多数是同一个原因重复出现，
# 真正的问题被淹没。probe 用 3 个文件先把"头文件 + 编译开关"这一层单独压平，
# 之后 lib 阶段的报错才真正关于 git 本身。
#
# 所有 NO_* 只在这里写一份：probe 用作 -D，lib 用作 make 变量。
set -uo pipefail

ARCH="${1:-riscv64}"
STAGE="${2:-probe}"
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SRC="$ROOT/user/git/src"
OUT="$ROOT/user/build/$ARCH/git"

case "$ARCH" in
  riscv64)
    PREFIX="riscv64-unknown-elf-"
    ARCHFLAGS=(-march=rv64gc -mabi=lp64d -mcmodel=medany -mno-relax
               -msmall-data-limit=0 -DUSER_ARCH_RISCV64)
    ;;
  loongarch64)
    PREFIX="loongarch64-unknown-linux-gnu-"
    ARCHFLAGS=(-DUSER_ARCH_LOONGARCH64 -G0 -mno-relax)
    ;;
  *)
    echo "用法: $0 <riscv64|loongarch64> [probe|lib]"
    exit 2
    ;;
esac

CC="${CC:-${PREFIX}gcc}"
AR="${AR:-${PREFIX}ar}"

if [ ! -f "$SRC/git.c" ]; then
  echo "[git] ✗ 源码不在 $SRC"
  echo "      先取源码（SSH，通常最快）:"
  echo "        git clone --depth 1 --branch v2.33.1 git@github.com:git/git.git user/git/src"
  echo "        rm -rf user/git/src/.git      # 去掉上游历史，作为 vendored 源码"
  exit 1
fi

NOVARS=(
  NO_MMAP NO_PTHREADS NO_OPENSSL NO_CURL NO_EXPAT NO_GETTEXT NO_ICONV
  NO_TCLTK NO_PERL NO_PYTHON NO_NSEC NO_REGEX NO_MEMMEM NO_STRCASESTR
  NO_SETENV NO_UNSETENV
  # 这三个 git 自带 compat 实现，比往 libc 里塞更安全：
  #   strlcpy/strlcat（BSD 扩展）、strtoumax/strtoimax（C99）、setitimer（定时器）
  NO_STRLCPY NO_STRTOUMAX NO_SETITIMER
)
# 注意：不设 NO_INET_PTON / NO_INET_NTOP —— user/include/arpa/inet.h 已有实现，
# 再编 git 的 compat/inet_*.c 会符号重复。

NOFLAGS=()
NOMAKE=()
for v in "${NOVARS[@]}"; do
  NOFLAGS+=("-D$v")
  NOMAKE+=("$v=1")
done

CFLAGS=(
  "${ARCHFLAGS[@]}"
  -Os -ffreestanding -fno-builtin
  -I"$ROOT/user/include"
  -I"$ROOT/user/zlib"
  -I"$SRC"
  "${NOFLAGS[@]}"
)

mkdir -p "$OUT"

case "$STAGE" in
  probe)
    echo "[git] 探针编译（$ARCH）: wrapper.c strbuf.c hashmap.c"
    for f in wrapper.c strbuf.c hashmap.c; do
      err="$OUT/probe_${f%.c}.err"
      if "$CC" "${CFLAGS[@]}" -c "$SRC/$f" -o "$OUT/probe_${f%.c}.o" 2>"$err"; then
        echo "  ok   $f"
      else
        n="$(grep -c 'error:' "$err" 2>/dev/null || true)"
        echo "  FAIL $f  ($n errors)"
        grep -E 'error:' "$err" | head -12 | sed 's/^/         /'
      fi
    done
    echo
    echo "[git] 完整日志: $OUT/probe_*.err"
    ;;

  lib)
    echo "[git] make libgit.a（$ARCH，-j$(nproc 2>/dev/null || echo 4)）"
    make -C "$SRC" -j"$(nproc 2>/dev/null || echo 4)" \
      CC="$CC" AR="$AR" \
      CFLAGS="${CFLAGS[*]}" \
      "${NOMAKE[@]}" \
      libgit.a 2>&1 | tee "$OUT/make_libgit.log" | tail -30

    echo
    echo "[git] 完整日志: $OUT/make_libgit.log"
    if [ -f "$SRC/libgit.a" ]; then
      echo "[git] ✓ libgit.a $(wc -c < "$SRC/libgit.a") bytes"
    else
      echo "[git] ✗ 未产出 libgit.a，错误数: $(grep -c 'error:' "$OUT/make_libgit.log" 2>/dev/null || echo 0)"
    fi
    ;;

  *)
    echo "未知阶段: $STAGE（probe | lib）"
    exit 2
    ;;
esac
