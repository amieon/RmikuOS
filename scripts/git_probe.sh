#!/usr/bin/env bash
#
# git 移植的分级试编工具（在 Linux 开发树上跑，需要交叉工具链）
#
#   bash scripts/git_probe.sh riscv64 probe     # 只编 3 个文件，压头文件/配置面
#   bash scripts/git_probe.sh riscv64 lib       # make libgit.a（核心对象，不含 builtin）
#   bash scripts/git_probe.sh riscv64 bin       # make git（全部 builtin + 链接成可执行）
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

# SHELL_PATH 必须同时是宿主和镜像上都能执行的路径，所以只能是 /bin/sh。
SHELL_PATH="/bin/sh"


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
  # 这两个 git 自带 compat 实现，比往 libc 里塞更安全：
  #   strlcpy/strlcat（BSD 扩展）、strtoumax/strtoimax（C99）
  # setitimer 不设 NO_SETITIMER：内核已实现 ITIMER_REAL（syscall 124/125），
  # 设了等于一边实现一边屏蔽自己，progress.c 的每秒刷新永远不会被验证。
  # setenv/unsetenv 同理不设：env.h 已有 POSIX 三参/单参版本
  # （以前同名函数是五参长度版，签名不 POSIX，才不得不关掉用 git 自带实现）。
  NO_STRLCPY NO_STRTOUMAX
)
# 注意：不设 NO_INET_PTON / NO_INET_NTOP —— user/include/arpa/inet.h 已有实现，
# 再编 git 的 compat/inet_*.c 会符号重复。

NOFLAGS=()
NOMAKE=()
for v in "${NOVARS[@]}"; do
  NOFLAGS+=("-D$v")
  NOMAKE+=("$v=1")
done

# -DNO_REGEX 只关掉"用系统 regex"，<regex.h> 这个 include 还是无条件的。
# make 里由下面两行补上真正的实现：
#     ifdef NO_REGEX
#         COMPAT_CFLAGS += -Icompat/regex
#         COMPAT_OBJS   += compat/regex/regex.o
# probe 不经过 make，必须自己镜像这个 -I，否则会在 <regex.h> 上假失败
# （make 阶段能过去 —— 之前就是这个差异把人误导了一次）。
COMPAT_INC=(
  -I"$SRC/compat/regex"
)

if [ ! -f "$SRC/compat/regex/regex.h" ]; then
  echo "[git] ✗ 找不到 $SRC/compat/regex/regex.h"
  echo "      这个目录是 git 自带的 GNU regex；源码树不完整就必然报 regex.h not found。"
  echo "      确认版本是 v2.33.1，且 clone 时没加 --filter=blob:none 之类的省流量选项。"
  exit 1
fi

# -std=gnu11 必须显式钉死，不能让编译器默认标准说了算。
#
# 起因：git 2.33 的 builtin/index-pack.c:89 拿 `struct thread_local` 当结构体名，
# 而 C23 把 thread_local 变成了关键字 → `struct thread_local` 被读成
# "struct + 存储类说明符"，直接语法错误（gcc 原文：expected '{' before 'thread_local'），
# 后续 nothread_data / thread_data 全变 undeclared，一个根因炸出十几条 error。
#
# 而 git 的 Makefile（CFLAGS = -g -O2 -Wall）根本不写 -std，用的是编译器默认：
# 老的 riscv64-unknown-elf-gcc 默认 gnu11/gnu17 能过，新的 loongarch64 gcc 默认
# 已经到 gnu23 就炸 —— 同一份源码一边过一边不过，差的不是 NO_* 宏，是标准。
#
# 全树扫过：只有 index-pack.c 这一处撞 C23 新关键字
# （nullptr / constexpr / static_assert / alignas / alignof / typeof_unqual 均 0 命中）。
# gnu11 也覆盖 git 里所有 `__STDC_VERSION__ >= 199901L` 的分支，无副作用。
CFLAGS=(
  -std=gnu11
  "${ARCHFLAGS[@]}"
  -Os -ffreestanding -fno-builtin
  -I"$ROOT/user/include"
  -I"$ROOT/user/zlib"
  -I"$SRC"
  "${COMPAT_INC[@]}"
  "${NOFLAGS[@]}"
)

mkdir -p "$OUT"

# CFLAGS 变更检测。
#
# make 只按时间戳判依赖，完全不感知命令行变量变化：改了 CFLAGS（比如这次加
# -std=gnu11）之后直接 make，旧的 .o 会被原样复用，改动等于没生效，而且一声不响 ——
# 比起编译失败，这种"静默不生效"更浪费时间（之前改 prefix 也踩过同一个坑）。
# 这里按 CFLAGS 数组内容存一份快照，变了就 clean 掉旧产物再编。
if [ "$STAGE" = "lib" ] || [ "$STAGE" = "bin" ]; then
  # SHELL_PATH 也进快照：它不是 CFLAGS 的一部分（是 make 变量），但会被编进
  # run-command.o 的字符串常量里。改了却不重编，旧 .o 会静默带着旧路径 ——
  # 和当初 -std 变了 make 不感知是同一个坑。
  #
  # 我们的头文件同样进快照。理论上 make 的 .depend 会跟踪 -I 目录下的头，
  # 但 libc 是 header-only（static inline），改了 include/*.h 等于改了源码：
  # 一旦 .d 没覆盖到（或 .o 比头新一个时间戳误差），就会跑出"改了库但行为没变"
  # 的幽灵 bug —— perror 那次就是这样，输出还是旧的 "error"。
  # 头文件变更频率极低，宁可全量重编一次，也不要静默复用旧 .o。
  INC_SIG="$(cat "$ROOT"/user/include/*.h "$ROOT"/user/zlib/*.h 2>/dev/null \
             | sha256sum | cut -d' ' -f1)"
  CFLAGS_SIG="$(printf '%s\n' "${CFLAGS[@]}" "SHELL_PATH=$SHELL_PATH" "INC=$INC_SIG" \
                | sha256sum | cut -d' ' -f1)"
  CFLAGS_SNAP="$OUT/cflags.sha256"
  if [ -f "$CFLAGS_SNAP" ] && [ "$(cat "$CFLAGS_SNAP")" != "$CFLAGS_SIG" ]; then
    echo "[git] CFLAGS 或 user 头文件变了 → 清理旧的 .o（make 不知道命令行变量变化）"
    make -C "$SRC" clean >/dev/null 2>&1 || true
  fi
  printf '%s\n' "$CFLAGS_SIG" > "$CFLAGS_SNAP"
fi

# 每次把搜索路径打出来：probe / lib 两个阶段的差异一旦出现，第一眼就能看见，
# 不用去猜"我到底跑的是不是改过的脚本"。
echo "[git] -I: $ROOT/user/include | $ROOT/user/zlib | $SRC | $SRC/compat/regex"

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
    # NEEDS_LIBRT：config.mak.uname 的 Linux 分支会设它，Makefile 于是往 EXTLIBS 里加
    # `-lrt`。交叉工具链没有 librt → 最后链接才炸，而且报错信息离原因很远。
    # 我们的 clock_gettime 在头里，根本不需要它，直接置空覆盖掉。
    make -C "$SRC" -j"$(nproc 2>/dev/null || echo 4)" \
      CC="$CC" AR="$AR" \
      CFLAGS="${CFLAGS[*]}" \
      NEEDS_LIBRT= \
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

  bin)
    LINKER="$ROOT/user/linker-$ARCH.ld"
    ZLIB="$ROOT/user/build/$ARCH/zlib"
    if [ ! -f "$ZLIB/libz.a" ]; then
      echo "[git] ✗ 缺 $ZLIB/libz.a"
      echo "      git 的 object 存储全靠 zlib。先跑一次 user/build.py（build_zlib 会产出它）。"
      exit 1
    fi

    # crt0 / 系统调用入口 / memcpy+memset+errno 是 libc 侧裸件，git 的 Makefile
    # 不认识它们。编成 .o 塞进 LDFLAGS：链接命令里 .o 出现在任何位置都能被解析，
    # 不用改 Makefile。
    # 只有 string.c 必须在场：errno 的唯一定义、memset/memcpy、isatty 都在里面。
    # strdup / getpwuid / getpwnam 现在都是头文件 inline，不再需要额外的 .o。
    echo "[git] 编 libc 裸件（crt0 / syscall / string / mem）"
    "$CC" "${CFLAGS[@]}" -c "$ROOT/user/lib/crt0_$ARCH.S"    -o "$OUT/_crt0.o"    || exit 1
    "$CC" "${CFLAGS[@]}" -c "$ROOT/user/lib/syscall_$ARCH.S" -o "$OUT/_syscall.o" || exit 1
    "$CC" "${CFLAGS[@]}" -c "$ROOT/user/lib/string.c"        -o "$OUT/_string.o"  || exit 1
    # mem.o 必须带：堆状态（malloc_head / slab_freelist）从 mem.h 挪到 mem.c 之后，
    # 任何用到 malloc 的 TU 都会引用它，不带就是 undefined reference。
    "$CC" "${CFLAGS[@]}" -c "$ROOT/user/lib/mem.c"           -o "$OUT/_mem.o"     || exit 1

    LINK_FLAGS="-nostdlib -nostartfiles -static -no-pie -Wl,--build-id=none"
    LINK_FLAGS="$LINK_FLAGS $OUT/_crt0.o $OUT/_string.o $OUT/_mem.o $OUT/_syscall.o -T $LINKER"

    # LIBS 整份覆盖：默认是 `libgit.a xdiff/lib.a $(EXTLIBS)`，而 EXTLIBS 里会混进
    # -lrt / -lpthread / -lcurl 之类宿主库，交叉环境一个都没有。我们自己写死。
    LIBS_OVERRIDE="libgit.a xdiff/lib.a -L$ZLIB -lz -lgcc"

    # 先删旧产物：make 失败时 $SRC/git 还留着上一次的，会被误判成"成功"。
    rm -f "$SRC/git"

    echo "[git] make git（$ARCH，-j$(nproc 2>/dev/null || echo 4)）"
    make -C "$SRC" -j"$(nproc 2>/dev/null || echo 4)" \
      CC="$CC" AR="$AR" \
      CFLAGS="${CFLAGS[*]}" \
      LDFLAGS="$LINK_FLAGS" \
      LIBS="$LIBS_OVERRIDE" \
      NEEDS_LIBRT= SHELL_PATH="$SHELL_PATH" \
      "${NOMAKE[@]}" \
      git 2>&1 | tee "$OUT/make_git.log" | tail -40

    echo
    echo "[git] 完整日志: $OUT/make_git.log"
    if [ -s "$SRC/git" ]; then
      cp "$SRC/git" "$OUT/git.elf"
      echo "[git] ✓ git.elf $(wc -c < "$OUT/git.elf") bytes"
    else
      echo "[git] ✗ 未产出 git，错误数: $(grep -c 'error:' "$OUT/make_git.log" 2>/dev/null || echo 0)"
      exit 1
    fi
    ;;

  *)
    echo "未知阶段: $STAGE（probe | lib | bin）"
    exit 2
    ;;
esac
