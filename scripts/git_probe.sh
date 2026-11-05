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

# ---- http/https 传输：把 libcurl.a 链进来 ----
#
# NO_CURL 一开，git 里就一个 http 传输都没有：`git clone https://…` 报
# "unable to find remote helper for 'https'"。要 https 就必须**不设**它。
#
# 判据是文件而不是开关：libcurl.a 由 scripts/curl_build.sh 的 lib 阶段产出，
# user/build.py 现在保证 openssl → curl → git 的顺序，所以"libcurl.a 在"
# 就等于"curl 这一层已经可用"。WITH_CURL=0 可以显式退回纯 file:// 的旧行为，
# 出问题拿来二分（`WITH_CURL=0 bash scripts/git_probe.sh riscv64 bin`）。
CURLBASE="$ROOT/user/build/$ARCH/curl"
SSLBASE="$ROOT/user/build/$ARCH/openssl"
WITH_CURL="${WITH_CURL:-auto}"
if [ "$WITH_CURL" = "auto" ]; then
  if [ -f "$CURLBASE/libcurl.a" ]; then
    WITH_CURL=1
  else
    WITH_CURL=0
    # 降级本身是对的（缺 curl 也得让 git 本体编出来），但**必须出声**：
    # 之前这里一声不响，结果 git 编得漂漂亮亮、/bin 里就是没有 helper，
    # 看起来像"脚本白改了"。真正的原因永远在上游：openssl/curl 那层没落盘。
    echo
    echo "[git] ⚠ 没有 $CURLBASE/libcurl.a → 本轮 NO_CURL，**不带 https**"
    echo "      git 本体照常生成，但不会编 git-remote-http（= /bin/git-remote-https）"
    echo "      上游一般卡在 openssl 的 .a 上，先看这两个："
    echo "        ls -l $SSLBASE/libssl.a $SSLBASE/libcrypto.a $CURLBASE/libcurl.a"
    echo "      补齐顺序（前一步成功再跑后一步）:"
    echo "        bash scripts/openssl_build.sh $ARCH lib"
    echo "        bash scripts/curl_build.sh    $ARCH bin"
    echo "      确定不需要 https 就显式 WITH_CURL=0，消掉这条提示。"
    echo
  fi
fi

NOVARS=(
  NO_MMAP NO_PTHREADS NO_OPENSSL NO_EXPAT NO_GETTEXT NO_ICONV
  NO_TCLTK NO_PERL NO_PYTHON NO_NSEC NO_REGEX NO_MEMMEM NO_STRCASESTR
  # 这两个 git 自带 compat 实现，比往 libc 里塞更安全：
  #   strlcpy/strlcat（BSD 扩展）、strtoumax/strtoimax（C99）
  # setitimer 不设 NO_SETITIMER：内核已实现 ITIMER_REAL（syscall 124/125），
  # 设了等于一边实现一边屏蔽自己，progress.c 的每秒刷新永远不会被验证。
  # setenv/unsetenv 同理不设：env.h 已有 POSIX 三参/单参版本
  # （以前同名函数是五参长度版，签名不 POSIX，才不得不关掉用 git 自带实现）。
  NO_STRLCPY NO_STRTOUMAX
)
# NO_CURL 单独一行：它同时决定 CFLAGS 的 -I、链接行的三个 .a、以及要不要编
# git-remote-http。散在数组里最容易"只摘了一半"，然后得到链接错误。
if [ "$WITH_CURL" != "1" ]; then
  NOVARS+=(NO_CURL)
fi
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

# http.c / remote-curl.c / http-walker.c 都 #include <curl/curl.h>，头必须来自
# 我们自己那份 curl 源码树（宿主的 curl-dev 版本、ABI 都不可信）。
# 头进了 CFLAGS 就自动进 CFLAGS_SIG，改 curl 头会触发 git 全量重编 —— 对的。
if [ "$WITH_CURL" = "1" ]; then
  CFLAGS+=(-I"$ROOT/user/curl/src/include")
fi

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
  # 我们的头文件同样进快照（include 递归，子目录头也编进 .o）。理论上 make 的
  # .depend 会跟踪 -I 目录下的头，但 libc 是 header-only（static inline），
  # 改了 include/**/*.h 等于改了源码：
  # 一旦 .d 没覆盖到（或 .o 比头新一个时间戳误差），就会跑出"改了库但行为没变"
  # 的幽灵 bug —— perror 那次就是这样，输出还是旧的 "error"。
  # 头文件变更频率极低，宁可全量重编一次，也不要静默复用旧 .o。
  INC_SIG="$( { LC_ALL=C find "$ROOT/user/include" "$ROOT/user/zlib" -name '*.h' -print0 \
                  | LC_ALL=C sort -z | xargs -0 cat; } 2>/dev/null \
             | sha256sum | cut -d' ' -f1)"
  # 再并上 build.py 算的统一 ABI 哈希（它额外覆盖 lib/*.c、crt0、链接脚本，
  # 而这些正是 string.o 之类被一起链进去的东西）。
  CFLAGS_SIG="$(printf '%s\n' "${CFLAGS[@]}" "SHELL_PATH=$SHELL_PATH" "INC=$INC_SIG" \
                "ABI=${RMIKU_LIBC_ABI:-none}" \
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

    # ---- 修 http.h 的 curl 版本判断（curl 8.x 才炸） ----------------------
    # http.h 里是：
    #   #if !defined(CURLOPT_USE_SSL) && defined(CURLOPT_FTP_SSL)
    #   #define CURLUSESSL_TRY CURLFTPSSL_TRY
    # defined() 只认宏，而 CURLOPT_USE_SSL 是**枚举**成员 —— 这判断在 curl 7/8 上
    # 一直是 false（老 curl 的 CURLOPT_FTP_SSL 也是枚举），纯属从没生效过。
    # curl 8.2x 把旧名改成 deprecated 宏（#define CURLFTPSSL_TRY CURLUSESSL_TRY），
    # 于是 #if 翻成 true，两边互定义：CURLUSESSL_TRY→CURLFTPSSL_TRY→CURLUSESSL_TRY，
    # GCC 检测到循环后停下，留下未声明标识符：
    #   http.c:1007: error: 'CURLUSESSL_TRY' undeclared
    # git 自己的注释写着这两个名字从 7.16.4 起合并，用版本号判断才是它本来的意思。
    # 幂等：打过一次就认第二个分支。上游若改了这行，走第三个分支告警而不是静默跳过。
    if grep -q '^#if !defined(CURLOPT_USE_SSL) && defined(CURLOPT_FTP_SSL)$' "$SRC/http.h"; then
      sed -i 's/^#if !defined(CURLOPT_USE_SSL) && defined(CURLOPT_FTP_SSL)$/#if LIBCURL_VERSION_NUM < 0x071604/' "$SRC/http.h"
      echo "[git] 修正 http.h：curl 版本判断改用 LIBCURL_VERSION_NUM < 0x071604"
    elif grep -q '^#if LIBCURL_VERSION_NUM < 0x071604$' "$SRC/http.h"; then
      : # 已经打过
    else
      echo "[git] ⚠ http.h 里没找到 CURLOPT_USE_SSL 的版本判断，跳过（http.o 可能编不过）"
    fi

    # ---- 上一轮 NO_CURL 的 .o 不能留 --------------------------------------
    # NO_CURL 时 http.o / http-walker.o / remote-curl.o 根本不在 LIB_OBJS 里，
    # 从来没编过；而 transport.o 是那时候编的，里面 #ifdef USE_CURL 的
    #   { "http", &http_transport }
    # 压根没进二进制。Makefile 只按时间戳判依赖，命令行宏变了它照样复用旧 .o，
    # 链接**照样成功** —— 得到一个看不出问题的 git，但 transport 表里没有 http，
    # 连 "remote-https is not a git command" 都走不到。
    # 判据用 http.o 在不在：它在就说明这份 libgit.a 是带 CURL 编的。
    if [ "$WITH_CURL" = "1" ] && [ -f "$SRC/libgit.a" ] && [ ! -f "$SRC/http.o" ]; then
      echo "[git] libgit.a 是 NO_CURL 时代打的（没有 http.o）→ make clean 全量重编"
      make -C "$SRC" clean >/dev/null 2>&1 || true
    fi

    # ---- libc 头改了同样要清 .o -------------------------------------------
    # build.py 把 include/**/*.h 算进 shared_deps_hash，env.h 一变它会重跑
    # git 阶段；但那些 .o 落在**源码树**，Makefile 的依赖表里没有 libc 头，
    # 于是脚本重跑了、object 却还是旧的 —— 得到一个「编译过、语义错」的 git。
    # （setenv 不同步 environ 时，clone 照样挂在 index-pack 那两条 fatal。）
    # clean 会删掉 libgit.a（Makefile:3226），新打出来的比头新，不会反复触发。
    if [ -f "$SRC/libgit.a" ] && \
       [ -n "$(find "$ROOT/user/include" -name '*.h' -newer "$SRC/libgit.a" -print -quit)" ]; then
      echo "[git] libc 头比 libgit.a 新 → make clean 全量重编（Makefile 不追头依赖）"
      make -C "$SRC" clean >/dev/null 2>&1 || true
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
    #
    # 开了 https 之后末尾要多三个 .a。顺序是硬约束，跟 ld 解析静态库的规则一致：
    #   libgit.a / xdiff →  libcurl.a  →  libssl.a / libcrypto.a  →  -lz
    # libcurl 引用 SSL_*，所以 openssl 那两个必须在它**后面**；libcurl 还要 inflate，
    # 所以 -lz 必须再往后。写反了的症状是 "undefined reference to inflate"，
    # 而 libz.a 明明就写在命令行上 —— 这类报错只看顺序，不看库在不在。
    CURL_LIBS=""
    if [ "$WITH_CURL" = "1" ]; then
      for lib in "$CURLBASE/libcurl.a" "$SSLBASE/libssl.a" "$SSLBASE/libcrypto.a"; do
        if [ ! -f "$lib" ]; then
          echo "[git] ✗ 缺 $lib"
          echo "      WITH_CURL=0 可以先退回不带 https 的构建，其余照常。"
          exit 1
        fi
        CURL_LIBS="$CURL_LIBS $lib"
      done
      echo "[git] https 传输: 开（libcurl.a $(wc -c <"$CURLBASE/libcurl.a") bytes）"
    else
      echo "[git] https 传输: 关（NO_CURL，只剩 file:// 与 git://）"
    fi
    LIBS_OVERRIDE="libgit.a xdiff/lib.a$CURL_LIBS -L$ZLIB -lz -lgcc"

    # CURL_LIBCURL / CURL_CFLAGS 是 Makefile 自己会用的变量名，http.o 与
    # remote-curl.o 的链接规则直接展开它们。命令行赋值优先级高于 Makefile 里的
    # `=`/`+=`，所以不用动上游文件。
    # CURL_LDFLAGS 必须显式置空：Makefile 里它是
    #   ifndef CURL_LDFLAGS → $(shell $(CURL_CONFIG) --libs)
    # 宿主上要是装着 libcurl-dev，这里就会塞进宿主的 -lcurl -lssl，链接瞬间跑偏。
    # 我们两个变量都从命令行给了，那个 `CURL_LIBCURL += $(CURL_LDFLAGS)` 不会生效，
    # 但置空是零成本的保险。
    MAKE_CURL=()
    if [ "$WITH_CURL" = "1" ]; then
      # "-I" 不能省。省了它不是"没加 include 路径"，而是把一个目录当成**输入文件**
      # 塞进编译行：gcc 先把它传给 ld，链接时变成
      #   ld: cannot find .../curl/src/include: file format not recognized
      # 报错出现在链接阶段，看起来像库坏了，实际是这里拼错了。
      # 同时这也顺手挡住了 Makefile 的 $(shell $(CURL_CONFIG) --cflags)：宿主上
      # 装着 libcurl-dev 的话，它会塞进宿主的 /usr/include，编译期就用错了头。
      MAKE_CURL=(CURL_CFLAGS="-I$ROOT/user/curl/src/include"
                 CURL_LIBCURL="$CURLBASE/libcurl.a"
                 CURL_LDFLAGS=)
    fi

    # 先删旧产物：make 失败时 $SRC/git 还留着上一次的，会被误判成"成功"。
    rm -f "$SRC/git" "$SRC/git-remote-http"

    echo "[git] make git（$ARCH，-j$(nproc 2>/dev/null || echo 4)）"
    make -C "$SRC" -j"$(nproc 2>/dev/null || echo 4)" \
      CC="$CC" AR="$AR" \
      CFLAGS="${CFLAGS[*]}" \
      LDFLAGS="$LINK_FLAGS" \
      LIBS="$LIBS_OVERRIDE" \
      NEEDS_LIBRT= SHELL_PATH="$SHELL_PATH" \
      "${MAKE_CURL[@]}" \
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

    # ---- git-remote-https：https 的 remote helper，是**另一个独立二进制** ----
    #
    # 它由 remote-curl.o + http.o + http-walker.o 单独链接（Makefile 的
    # $(REMOTE_CURL_PRIMARY) 规则），**不是** git 本体的副本 —— 这点和
    # git-receive-pack / git-upload-pack 正相反，那两个确实就是 /bin/git 的复制。
    #
    # 运行期怎么被调起来（四段都查过，缺一段就是 ENOENT）：
    #   1. transport-helper.c 起子进程：args[0] = "remote-https"，git_cmd=1
    #   2. prepare_git_cmd 变 ["git","remote-https",…] → 又 exec 一次 /bin/git
    #   3. git.c 查不到这个内建命令 → execv_dashed_external("git-remote-https")
    #   4. 内核 sys_exec 对不含 '/' 的名字自动补 /bin/ → /bin/git-remote-https
    # 所以产物落到 /bin 就通，PATH 不参与（exec-cmd.c 那套 setup_path 是宿主玩法）。
    #
    # 名字取 https 而不是 http：内核只做 "/bin/" + 名字的拼接，没有符号链接，
    # 一个名字就得一个文件。ftp/ftps 先不铺（两个没用的大文件）。
    if [ "$WITH_CURL" = "1" ]; then
      echo "[git] make git-remote-http（https 的 remote helper）"
      make -C "$SRC" CC="$CC" AR="$AR" \
        CFLAGS="${CFLAGS[*]}" \
        LDFLAGS="$LINK_FLAGS" \
        LIBS="$LIBS_OVERRIDE" \
        NEEDS_LIBRT= SHELL_PATH="$SHELL_PATH" \
        "${MAKE_CURL[@]}" \
        "${NOMAKE[@]}" \
        git-remote-http 2>&1 | tee "$OUT/make_helper.log" | tail -20

      if [ -s "$SRC/git-remote-http" ]; then
        cp "$SRC/git-remote-http" "$OUT/git-remote-https.elf"
        echo "[git] ✓ git-remote-https.elf $(wc -c < "$OUT/git-remote-https.elf") bytes"
        echo "      mkfs 会把 build/$ARCH/git/*.elf 整体铺进 /bin，名字即命令名"
      else
        echo "[git] ✗ 未产出 git-remote-http，错误数: $(grep -c 'error:' "$OUT/make_helper.log" 2>/dev/null || echo 0)"
        echo "      完整日志: $OUT/make_helper.log；git 本体可用，只缺 https"
      fi
    fi
    ;;

  *)
    echo "未知阶段: $STAGE（probe | lib | bin）"
    exit 2
    ;;
esac
