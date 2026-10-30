#!/usr/bin/env bash
#
# OpenSSL 移植的分级试编工具（在 Linux 开发树上跑，需要交叉工具链 + 宿主 perl）
#
#   bash scripts/openssl_build.sh riscv64 probe   # 只编几个关键文件，压头文件/配置面
#   bash scripts/openssl_build.sh riscv64 lib     # 产出 libcrypto.a + libssl.a，并扫出全部缺口
#   bash scripts/openssl_build.sh riscv64 bin     # 链接 apps/openssl → openssl.elf
#   bash scripts/openssl_build.sh riscv64 check   # 只看 libc 缺口分类，不编任何东西
#
# 和 git_probe.sh 的三点不同：
#   1. Configure 是 perl 脚本，必须在**宿主**上跑；它还会生成
#      include/openssl/configuration.h（bn_ops、OPENSSL_NO_* 全在里面），
#      所以哪怕 probe 阶段也得先 Configure 一次。
#   2. 构建是 **out-of-tree**：Configure 在 build 目录里跑，源码树一个字节都不写。
#      这样 user/openssl/src 可以原样入库，构建产物全在 user/build（已 gitignore）。
#   3. 多了 probe 阶段的 `-H` 检查：抓"悄悄用了工具链 sysroot 里的头文件"。
#      这种错编译能过，但 struct stat / sockaddr_in 的布局和内核不一致，运行期才炸。
#
# 失败不中断：和 git 一样，OpenSSL 编不出来不该让别的用户程序一起废掉。
set -uo pipefail

ARCH="${1:-riscv64}"
STAGE="${2:-lib}"
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SRC="$ROOT/user/openssl/src"
BASE="$ROOT/user/build/$ARCH/openssl"
OBJ="$BASE/obj"          # Configure 的落地目录（Makefile / configuration.h / 全部 .o）
OUT="$BASE"              # 对外产物：libcrypto.a / libssl.a / openssl.elf

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
    echo "用法: $0 <riscv64|loongarch64> [probe|lib|bin|check]"
    exit 2
    ;;
esac

CC="${CC:-${PREFIX}gcc}"
AR="${AR:-${PREFIX}ar}"
NM="${NM:-${PREFIX}nm}"
RANLIB="${RANLIB:-${PREFIX}ranlib}"
NPROC="$(nproc 2>/dev/null || echo 4)"

# 两个架构都是 LP64（long = 8 字节），bn_ops 必须落到 SIXTY_FOUR_BIT_LONG，
# 所以统一借 linux-generic64 的 target 定义，不自己写一份。
ARCH_CFG="linux-generic64"

if [ ! -f "$SRC/Configure" ]; then
  echo "[openssl] ✗ 源码不在 $SRC"
  echo "          git clone --depth 1 -b openssl-3.0 https://github.com/openssl/openssl.git user/openssl/src"
  echo "          rm -rf user/openssl/src/.git user/openssl/src/test user/openssl/src/doc"
  exit 1
fi

# ---------------------------------------------------------------------------
# 关掉的开关只有两类理由：**我们没有这个能力**，或者**它会引入我们测不了的分支**。
# 每个都写明理由，以后想开一个先读一遍，别凭"反正 no-asm 也过了"去试。
# ---------------------------------------------------------------------------
CNF_OPTS=(
  no-asm       # perlasm 要 perl 现场生成 .s；纯 C 的 BN 慢但正确
  no-threads   # libc 只有 thread_create/join，没有 pthread ABI
  no-shared    # 没有动态链接，全部静态
  no-dso       # 内核无 dlopen
  no-module    # provider 默认靠 dlopen 加载，不关就会得到一张空的算法表（静默失败）
  no-engine    # engine 是 dlopen 的另一种形式
  no-async     # async 要线程/fiber
  no-tests     # 测试要 perl 驱动 + 几千个证书，跑不了
  no-docs      # man 页没用
  no-legacy    # 少编一批老旧算法，省体积
  no-comp      # SSL 层压缩，要额外维护 zlib 绑定
)

# -I 顺序要紧：user/include 必须在工具链 sysroot 之前，否则 `#include <stdlib.h>`
# 会命中 newlib/glibc 的声明，malloc 变成 extern 引用 → 链接到它们那套堆 → 跨库
# free 直接崩。-I 天然排在系统目录前，所以只要写了就在前面，probe 阶段会验证。
# -fno-stack-protector 不是可选项：没有 __stack_chk_fail。
CFLAGS_STR="-std=gnu11 ${ARCHFLAGS[*]} -O2 -ffreestanding -fno-builtin -fno-stack-protector -I$ROOT/user/include"

mkdir -p "$BASE"

# ---------------------------------------------------------------------------
# Configure：幂等。参数或 CFLAGS 变了就整份重建（不是 make clean ——
# configuration.h 变了必须重编全部 .o，而 make 不感知命令行变量变化，
# 这条坑 git 那边已经踩过：改了 -std 但旧 .o 被静默复用）。
# ---------------------------------------------------------------------------
configure_openssl() {
  local sig
  sig="$(printf '%s\n' "$ARCH_CFG" "${CNF_OPTS[@]}" "$CFLAGS_STR" "$CC" | sha256sum | cut -d' ' -f1)"
  if [ -f "$OBJ/.cfg.sha256" ] && [ "$(cat "$OBJ/.cfg.sha256")" = "$sig" ]; then
    return 0
  fi

  echo "[openssl] Configure（$ARCH，参数变化或首次构建）"
  rm -rf "$OBJ"
  mkdir -p "$OBJ"
  (
    cd "$OBJ" || exit 1
    "$SRC/Configure" "$ARCH_CFG" "${CNF_OPTS[@]}" \
      CC="$CC" AR="$AR" RANLIB="$RANLIB" NM="$NM" \
      --prefix=/usr --openssldir=/etc/ssl \
      CFLAGS="$CFLAGS_STR"
  ) >"$BASE/configure.log" 2>&1

  if [ ! -f "$OBJ/Makefile" ]; then
    echo "[openssl] ✗ Configure 失败，日志 $BASE/configure.log"
    tail -20 "$BASE/configure.log" | sed 's/^/          /'
    return 1
  fi

  # 断言一：字宽。BN 是任意精度，选错字宽不会算错，只会慢；
  # 但选成 THIRTY_TWO_BIT（32 位模板）会让 BN_ULONG 变成 4 字节 —— 还是慢，不是错。
  # 所以这条断言不是为了正确性，是为了**确认我们真的拿到了 64 位路径**。
  if grep -q "define SIXTY_FOUR_BIT_LONG" "$OBJ/include/openssl/configuration.h" 2>/dev/null; then
    echo "[openssl]   bn_ops = SIXTY_FOUR_BIT_LONG ✓"
  else
    echo "[openssl] ! configuration.h 里没有 SIXTY_FOUR_BIT_LONG，确认一下 target 是不是 linux-generic64"
  fi

  printf '%s\n' "$sig" > "$OBJ/.cfg.sha256"
  echo "[openssl]   日志 $BASE/configure.log"
}

# ---------------------------------------------------------------------------
# 缺口扫描：这是整个脚本最有价值的一步。
#
# 为什么这份清单会这么干净：我们的 libc 是 header-only `static inline`，
# OpenSSL 的 .c 一旦 include 到它，malloc/memcpy 就**内联进它自己的 .o**，
# 根本不产生 extern 引用。所以 libcrypto.a 的 undefined 天然只包含"我们真没有的"。
#
# 于是可以一次拿全，不用撞一个补一个（git 那次是撞一个补一个，代价很大）。
# ---------------------------------------------------------------------------
scan_undefined() {
  local -a libs=()
  [ -f "$OBJ/libcrypto.a" ] && libs+=("$OBJ/libcrypto.a")
  [ -f "$OBJ/libssl.a" ]    && libs+=("$OBJ/libssl.a")
  if [ ${#libs[@]} -eq 0 ]; then
    echo "[openssl] 没有 .a 可扫，先跑 lib 阶段"
    return 1
  fi

  "$NM" -u "${libs[@]}" 2>/dev/null | awk '$1 == "U" {print $2}' | sort -u >"$BASE/u.txt"
  "$NM" --defined-only "${libs[@]}" 2>/dev/null | awk 'NF >= 3 {print $3}' | sort -u >"$BASE/d.txt"
  comm -23 "$BASE/u.txt" "$BASE/d.txt" >"$BASE/undefined.txt"

  local total; total="$(wc -l <"$BASE/undefined.txt")"
  echo
  echo "[openssl] 外部依赖 $total 个 → $BASE/undefined.txt"

  # ---- 第一类：本该被 inline 掉的。出现在这里 = 头文件没生效，不是"缺函数" ----
  local leaked
  leaked="$(grep -E '^(malloc|calloc|realloc|free|memcpy|memmove|memset|memcmp|strlen|strcmp|strncmp|strcpy|strncpy|strdup|snprintf|sprintf|printf|fprintf|abort|qsort|bsearch|time|getenv|isatty)$' \
            "$BASE/undefined.txt" || true)"
  if [ -n "$leaked" ]; then
    echo
    echo "  ✗ 这些符号我们有 inline 实现却出现在 extern 引用里："
    printf '%s\n' "$leaked" | sed 's/^/      /'
    echo "    → 不是缺函数，是头文件根本没被用上（-I 顺序 / -nostdinc）。"
    echo "      这种状态下链接到的是工具链自带的那套 libc，跨库 free 会崩。先解决这个。"
  fi

  # ---- 第二类：gcc 运行时，链接加 -lgcc / 调整 CFLAGS 就好，不是 libc 的事 ----
  local rt
  rt="$(grep -E '^__(udiv|umod|div|mod|mul|ashl|ashr|lshr)ti3$|^__atomic_|^__sync_|^__stack_chk|^__gcc_' \
        "$BASE/undefined.txt" || true)"
  if [ -n "$rt" ]; then
    echo
    echo "  · gcc 运行时（链接加 -lgcc；__stack_chk_* 说明 -fno-stack-protector 没生效）："
    printf '%s\n' "$rt" | sed 's/^/      /'
  fi

  # ---- 第三类：真缺的 libc 函数 / crt 符号，要补进 user/include ----
  local missing
  missing="$(grep -vE '^(malloc|calloc|realloc|free|memcpy|memmove|memset|memcmp|strlen|strcmp|strncmp|strcpy|strncpy|strdup|snprintf|sprintf|printf|fprintf|abort|qsort|bsearch|time|getenv|isatty)$' \
             "$BASE/undefined.txt" \
           | grep -vE '^__(udiv|umod|div|mod|mul|ashl|ashr|lshr)ti3$|^__atomic_|^__sync_|^__stack_chk|^__gcc_' || true)"
  if [ -n "$missing" ]; then
    echo
    echo "  → 需要我们补的（补进 user/include/*.h，header-only inline）："
    printf '%s\n' "$missing" | sed 's/^/      /'
    echo
    echo "    补之前先想清楚每一类的归处："
    echo "      gettimeofday/clock_gettime → time.h 已有，可能只是没 include"
    echo "      open/read/write/lseek/stat → io.h / fs.h 已有"
    echo "      dlopen/dlsym/dlerror      → 不该出现，no-dso 应已剪掉"
    echo "      pthread_*                 → 不该出现，no-threads 应已剪掉"
  fi

  echo
  echo "[openssl] 提示：crt0 提供的 _start 等符号也会在这份清单里，链接时自然解决。"
}

case "$STAGE" in
  probe)
    configure_openssl || exit 1

    # 挑 4 个文件：最底层的 mem、BN 核心、**熵源**（最担心的那条路）、时间。
    # 熵源排在前面不是偶然：rand_unix.c 编译期就能看出它到底走 /dev/urandom 还是
    # getrandom 分支，比运行到 openssl rand 才报错早得多。
    #
    # 这里不用 `make <target>.o`：3.0 给每个目标加了 libcrypto-lib- 前缀
    # （crypto/rand/libcrypto-lib-rand_unix.o），拼错就是"没有这个目标"，
    # 白浪费一轮。直接调编译器，include 路径按 3.0 的标准给全。
    INCS=(-I"$OBJ" -I"$OBJ/include" -I"$SRC" -I"$SRC/include" -I"$SRC/crypto"
          -I"$SRC/providers/common/include" -I"$SRC/providers/implementations/include")

    echo "[openssl] 探针编译（$ARCH）"
    for f in crypto/rand/rand_unix.c crypto/mem.c crypto/bn/bn_add.c crypto/o_time.c; do
      err="$BASE/probe_$(basename "$f").err"
      if "$CC" $CFLAGS_STR "${INCS[@]}" -c "$SRC/$f" -o "$BASE/probe_$(basename "${f%.c}").o" 2>"$err"; then
        echo "  ok   $f"
      else
        n="$(grep -c 'error:' "$err" 2>/dev/null || echo 0)"
        echo "  FAIL $f  ($n errors)"
        grep -E 'error:' "$err" | head -10 | sed 's/^/         /'
      fi
    done

    # 外部头混入检查：-H 列出所有实际被 include 的头，滤掉我们自己的和生成物，
    # 剩下的就是工具链 sysroot 里的。白名单只放编译器内建那几个（类型定义必须一致）。
    echo
    echo "[openssl] 检查有没有用到工具链 sysroot 的头（编译能过但 ABI 会错）"
    "$CC" $CFLAGS_STR "${INCS[@]}" \
      -H -fsyntax-only "$SRC/crypto/mem.c" 2>&1 \
      | grep -E '^\.+ /' | sed 's/^\.* //' | sort -u \
      | grep -vE "^($ROOT|$OBJ|$SRC)" \
      | grep -vE '/(stddef|stdarg|stdbool|float|stdint|limits|stdatomic|iso646)\.h$' \
      | sed 's/^/      /' >"$BASE/foreign_headers.txt"
    if [ -s "$BASE/foreign_headers.txt" ]; then
      echo "  ! 以下头来自工具链而非 user/include，逐个确认结构体布局是否一致："
      cat "$BASE/foreign_headers.txt"
    else
      echo "  ok   全部头都来自 user/include 或 OpenSSL 自身"
    fi
    echo
    echo "[openssl] 日志: $BASE/probe_*.err"
    ;;

  lib)
    configure_openssl || exit 1
    echo "[openssl] make build_libs（-j$NPROC）"
    make -C "$OBJ" -j"$NPROC" build_libs 2>&1 | tee "$BASE/make_libs.log" | tail -30
    echo
    echo "[openssl] 日志: $BASE/make_libs.log"
    for a in libcrypto.a libssl.a; do
      if [ -f "$OBJ/$a" ]; then
        cp "$OBJ/$a" "$OUT/$a"
        echo "[openssl] ✓ $a $(wc -c <"$OUT/$a") bytes"
      else
        echo "[openssl] ✗ 未产出 $a（错误数 $(grep -c 'error:' "$BASE/make_libs.log" 2>/dev/null || echo 0)）"
      fi
    done
    scan_undefined
    ;;

  check)
    scan_undefined
    ;;

  bin)
    configure_openssl || exit 1
    if [ ! -f "$OBJ/libcrypto.a" ] || [ ! -f "$OBJ/libssl.a" ]; then
      echo "[openssl] ✗ 缺 .a，先跑: bash $0 $ARCH lib"
      exit 1
    fi

    # libc 裸件照 git 那一套。只有 string.c 必须在场（errno 的唯一真定义 +
    # memset/memcpy + isatty）；其余 libc 都在头里 inline 进去了。
    echo "[openssl] 编 libc 裸件（crt0 / syscall / string）"
    "$CC" "${ARCHFLAGS[@]}" -std=gnu11 -O2 -ffreestanding -fno-builtin \
      -I"$ROOT/user/include" -c "$ROOT/user/lib/crt0_$ARCH.S"    -o "$BASE/_crt0.o"    || exit 1
    "$CC" "${ARCHFLAGS[@]}" -std=gnu11 -O2 -ffreestanding -fno-builtin \
      -I"$ROOT/user/include" -c "$ROOT/user/lib/syscall_$ARCH.S" -o "$BASE/_syscall.o" || exit 1
    "$CC" "${ARCHFLAGS[@]}" -std=gnu11 -O2 -ffreestanding -fno-builtin \
      -I"$ROOT/user/include" -c "$ROOT/user/lib/string.c"        -o "$BASE/_string.o"  || exit 1

    LINK_FLAGS="-nostdlib -nostartfiles -static -no-pie -Wl,--build-id=none"
    LINK_FLAGS="$LINK_FLAGS $BASE/_crt0.o $BASE/_string.o $BASE/_syscall.o"
    LINK_FLAGS="$LINK_FLAGS -T $ROOT/user/linker-$ARCH.ld"

    rm -f "$OBJ/apps/openssl"
    echo "[openssl] make apps/openssl（-j$NPROC）"
    make -C "$OBJ" -j"$NPROC" LDFLAGS="$LINK_FLAGS" apps/openssl \
      2>&1 | tee "$BASE/make_bin.log" | tail -40
    echo
    echo "[openssl] 日志: $BASE/make_bin.log"

    if [ -s "$OBJ/apps/openssl" ]; then
      cp "$OBJ/apps/openssl" "$OUT/openssl.elf"
      echo "[openssl] ✓ openssl.elf $(wc -c <"$OUT/openssl.elf") bytes"
      echo "[openssl]   下一步在 guest 里先跑： openssl list -providers"
      echo "[openssl]   看不到 default 就别往下测了，任何算法都会说找不到"
    else
      echo "[openssl] ✗ 链接失败（错误数 $(grep -c 'error:\|undefined reference' "$BASE/make_bin.log" 2>/dev/null || echo 0)）"
      grep -E 'undefined reference|error:' "$BASE/make_bin.log" | head -20 | sed 's/^/      /'
      echo
      echo "[openssl] 若是 undefined reference，把它和 undefined.txt 对一遍；"
      echo "[openssl] 链接期的 -lgcc 缺口要手工加：make ... LDFLAGS=\"$LINK_FLAGS -lgcc\""
      exit 1
    fi
    ;;

  *)
    echo "未知阶段: $STAGE（probe | lib | bin | check）"
    exit 2
    ;;
esac
