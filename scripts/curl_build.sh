#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# libcurl / curl 移植的分级试编工具（在 Linux 开发树上跑，需要交叉工具链）
#
#   bash scripts/curl_build.sh riscv64 probe    # configure + 编几个关键 TU，压头文件面
#   bash scripts/curl_build.sh riscv64 lib      # 产出 libcurl.a，扫缺口
#   bash scripts/curl_build.sh riscv64 bin      # 链接 src/curl → curl.elf
#   bash scripts/curl_build.sh riscv64 check    # 只看缺口，不编
#
# 和 openssl_build.sh 的三个关键差别，读之前先知道：
#
#   1. curl 用 autotools，没有 Configure 那种"一次性生成全部配置"的入口。
#      configure 本身就是构建的一部分，而且它探测能力的方式是**链接并运行**
#      测试程序 —— 交叉编译下"运行"全部退化，于是结论是保守值。
#   2. 更糟的是 **AC_CHECK_FUNC 一律失败**：它自己声明 `char foo();` 然后链接，
#      而我们的 libc 是 header-only 的 static inline，根本没有外部符号。
#      所有探测都答"没有"，curl 就会走一堆 fallback 分支（比如没有
#      getaddrinfo 就改用 gethostbyname_r，而那个我们也没有）。
#      => 必须用 cache 变量**显式告诉它真实能力**，见 CONFIGURE_CACHE。
#   3. 所以这里的 probe 阶段不是"编一个小文件看看"，而是"跑完 configure 之后
#      看它到底相信了什么"。后者信息量大得多。
# ---------------------------------------------------------------------------
set -uo pipefail
export LC_ALL=C

ARCH="${1:-riscv64}"
STAGE="${2:-lib}"
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SRC="$ROOT/user/curl/src"
BASE="$ROOT/user/build/$ARCH/curl"
OBJ="$BASE/obj"                 # configure 的落地目录（config.h / Makefile / 全部 .o）
OUT="$BASE"                     # 对外产物：libcurl.a / curl.elf
SSL_OUT="$ROOT/user/build/$ARCH/openssl"
SSL_OBJ="$SSL_OUT/obj"
SSL_SRC="$ROOT/user/openssl/src"
SSL_PREFIX="$BASE/ssl"          # install 形状的前缀（include/openssl + lib）

case "$ARCH" in
  riscv64)
    PREFIX="riscv64-unknown-elf-"
    CROSS_HOST="riscv64-unknown-elf"
    ARCHFLAGS=(-march=rv64gc -mabi=lp64d -mcmodel=medany -mno-relax
               -msmall-data-limit=0 -DUSER_ARCH_RISCV64)
    ;;
  loongarch64)
    PREFIX="loongarch64-unknown-linux-gnu-"
    CROSS_HOST="loongarch64-unknown-linux-gnu"
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

# -I 顺序要紧：user/include 必须在工具链 sysroot 之前，否则 <stdlib.h> 会命中
# glibc 的声明，malloc 变成 extern 引用 → 链接到它们那套堆 → 跨库 free 直接崩。
# -ffreestanding + -fno-builtin：不要 gcc 用内建 memcpy 替换我们的实现。
# -fno-stack-protector：没有 __stack_chk_fail。
CFLAGS_STR="-std=gnu11 ${ARCHFLAGS[*]} -O2 -ffreestanding -fno-builtin -fno-stack-protector"
CPPFLAGS_STR="-I$ROOT/user/include -I$SSL_OBJ/include -I$SSL_SRC/include"

# 源码树存在性**不能看 configure**。curl 的 git 仓库只带 configure.ac，
# configure 是 make dist 时才产出的 —— 只有 release tarball 里才有。
# 按 configure 判断会把刚 clone 好的树判成"源码不在"（踩过一次，白跑一轮）。
if [ ! -f "$SRC/configure.ac" ] && [ ! -f "$SRC/configure" ] && [ ! -f "$SRC/CMakeLists.txt" ]; then
  echo "[curl] ✗ 源码不在 $SRC"
  echo "       git clone --depth 1 -b curl-8_23_0 https://github.com/curl/curl.git user/curl/src"
  echo "       rm -rf user/curl/src/.git"
  exit 1
fi

if [ ! -f "$SSL_OUT/libssl.a" ] || [ ! -f "$SSL_OUT/libcrypto.a" ]; then
  echo "[curl] ✗ 没有找到 OpenSSL 静态库（$SSL_OUT）"
  echo "       先跑： bash scripts/openssl_build.sh $ARCH lib"
  exit 1
fi

CURL_VER="$(sed -n 's/^#define LIBCURL_VERSION "\(.*\)"/\1/p' "$SRC/include/curl/curlver.h" 2>/dev/null | head -1)"
echo "[curl] 源码版本 ${CURL_VER:-未知}（$SRC）"
echo "[curl] OpenSSL: $SSL_OUT（libssl.a $(wc -c <"$SSL_OUT/libssl.a") B）"

# ---------------------------------------------------------------------------
# 关掉的开关只有两类理由：**我们没有这个能力**，或者**它会引入我们测不了的分支**。
#
# 特别注意 --disable-threaded-resolver：
#   curl 默认在"有线程"时把 DNS 丢给后台线程，那需要 pthread（我们只有
#   thread_create/mutex_t，没有 pthread ABI）。关掉之后解析回到同步路径，
#   **直接调用 getaddrinfo** —— 正是我们刚实现的那个。
#
# 协议裁剪：只留 http/https。多留一个协议就多一组 .c、多一批系统调用面，
# 而它们全都测不到（配额里没有 ftp 服务器）。--disable-* 的清单按 curl
# 8.x 的 configure --help 抄，全部是官方选项。
# ---------------------------------------------------------------------------
# 只留 http/https。smb 和 rtmp 在 curl 8.x 里已经整个删掉，连同
# --disable-smb / --without-librtmp 一起消失，写上只会得到 unrecognized 警告。
PROTO_OPTS=(
  --disable-ftp --disable-file --disable-ldap --disable-ldaps
  --disable-rtsp --disable-proxy --disable-dict --disable-telnet
  --disable-tftp --disable-pop3 --disable-imap
  --disable-smtp --disable-gopher --disable-mqtt
)

# 选项名一律按 curl 8.23 的 m4/acinclude 实核过一遍，不是照文档抄的。
# autoconf 对未知的 --enable/--disable 只发 WARNING 不报错，写错了整轮
# configure 照样"成功"，只是那个开关静默不生效 —— 最难查的一类失效。
FEATURE_OPTS=(
  --disable-shared --enable-static
  # 这个名字是**下划线**：m4/curl-confopts.m4:37 是
  #   AC_ARG_ENABLE(threaded_resolver,
  # 写成连字符 threaded-resolver 会被当成未知选项。
  --disable-threaded_resolver
  --disable-ipv6
  --disable-unix-sockets
  --disable-manual
  --disable-docs
  --disable-aws
  --disable-alt-svc
  --disable-websockets
  --without-zlib
  --without-brotli
  --without-zstd
  --without-nghttp2
  --without-libidn2
  --without-libpsl
  --without-libssh2
  --with-ca-bundle=/etc/ssl/cert.pem
)

# ---- configure cache 变量 --------------------------------------------------
# 每一个都是"我们的 libc 确实有这个东西"，但因为 header-only 而探测不到。
# 写错会怎么坏：说 yes 但没有 → 链接期 undefined reference（当场就能看出）；
# 说 no 但有 → 悄悄走 fallback，运行期才发现解析全挂（更糟）。
#
# curl_cv_func_fcntl_o_nonblock=yes 是最要命的一条：
#   curl_setup.h 里有
#     #if !defined(HAVE_FCNTL_O_NONBLOCK) && !defined(HAVE_IOCTLSOCKET) ... \
#         #error "no non-blocking method"
#   交叉编译时 AC_RUN_IFELSE 走"不能运行"分支，结论是 no → 直接 #error。
#   我们确实支持（内核 F_SETFL 记 O_NONBLOCK，socket 层已按它分派），
#   所以这里必须显式说 yes。
CONFIGURE_CACHE=(
  curl_cv_func_fcntl_o_nonblock=yes
  ac_cv_func_getaddrinfo=yes
  ac_cv_func_freeaddrinfo=yes
  ac_cv_func_getnameinfo=yes
  ac_cv_func_poll=yes
  ac_cv_func_select=yes
  ac_cv_func_strdup=yes
  ac_cv_func_strtok_r=yes
  ac_cv_func_strcasecmp=yes
  ac_cv_func_strncasecmp=yes
  ac_cv_func_strtoll=yes
  ac_cv_func_strtoull=yes
  ac_cv_func_snprintf=yes
  ac_cv_func_vsnprintf=yes
  ac_cv_func_gettimeofday=yes
  ac_cv_func_alarm=yes
  ac_cv_func_sigaction=yes
  ac_cv_func_sigsetjmp=yes
  ac_cv_func_ioctl=yes
  ac_cv_func_uname=yes
  # 明确**没有**的，写出来免得它猜 yes：
  ac_cv_func_getifaddrs=no
  ac_cv_func_socketpair=no
  ac_cv_func_fork=no
  ac_cv_func_peekfunction=no
  ac_cv_func_inet_pton=yes
  ac_cv_func_inet_ntop=yes
)

# ---------------------------------------------------------------------------
# git 仓库里没有 configure，要先自己生成。tarball 自带 configure，直接跳过。
#
# **curl 8.23 已经把 buildconf 脚本删掉了**（根目录只剩 configure.ac 和
# CMakeLists.txt）。GIT-INFO.md 里给的路径就是直接跑 `autoreconf -fi`：
#
#     autoreconf -fi
#     ./configure --with-openssl
#     make
#
# 所以这里不能调 ./buildconf —— 那会 "No such file"。
#
# 为什么自动做而不是让使用者手工跑：autotools 的失败信息是
# "aclocal-1.16: not found" 这种，看不出跟本项目的因果关系；而且它往源码树里
# 写几百个文件，不 ignore 的话 git status 立刻被淹掉。
# ---------------------------------------------------------------------------
ensure_configure() {
  [ -f "$SRC/configure" ] && return 0
  mkdir -p "$BASE"
  echo "[curl] 源码树没有 configure（git 仓库只带 configure.ac），跑 autoreconf -fi 生成"

  local missing="" t
  for t in autoreconf automake libtoolize; do
    command -v "$t" >/dev/null 2>&1 || missing="$missing $t"
  done
  if [ -n "$missing" ]; then
    echo "  ✗ 缺 autotools:$missing"
    echo "    装： sudo apt install autoconf automake libtool"
    echo "    或者改用自带 configure 的 release tarball（不需要 autotools）："
    echo "      curl -LO https://curl.se/download/curl-8.23.0.tar.gz"
    echo "      rm -rf $SRC && mkdir -p $ROOT/user/curl"
    echo "      tar xf curl-8.23.0.tar.gz -C $ROOT/user/curl"
    echo "      mv $ROOT/user/curl/curl-8.23.0 $SRC"
    return 1
  fi

  # 在源码目录里跑，日志落 $BASE。源码树的 .git 已删，autoreconf 不会试图
  # 去 git describe 要版本号（要了也没有，只会多一行 warning）。
  ( cd "$SRC" && autoreconf -fi ) >"$BASE/autoreconf.log" 2>&1
  if [ ! -f "$SRC/configure" ]; then
    echo "  ✗ autoreconf 没生成 configure，日志末尾："
    tail -20 "$BASE/autoreconf.log" | sed 's/^/      /'
    return 1
  fi
  echo "  ok configure 已生成（日志 $BASE/autoreconf.log）"
  return 0
}

# ---------------------------------------------------------------------------
# 链接参数：必须先自己试一次，不能直接交给 configure。
#
# configure 的第一道门槛就是 AC_PROG_CC 的 "C compiler cannot create executables"
# —— 它拿 `int main(void){return 0;}` 走一遍**默认**编译+链接。而本项目的用户程序
# 从来不走默认链接：build.py 里每一条链接命令都是
#     gcc -nostdlib -nostartfiles -static -no-pie ... crt0.o ... -lgcc
# 加自带的 ldscript。目标机是我们自己的 OS，不是裸机 newlib 也不是 Linux。
#
# 于是「默认链接能不能用」这件事从来没被验证过，交叉工具链很可能压根没装
# crt0.o / libc。那样 configure 的每一个 conftest 都会链接失败，而它只说一句
# "C compiler cannot create executables"，真正的 `cannot find crt0.o` 埋在
# config.log 里 —— 只看 configure 的输出永远查不出来（踩过一次）。
#
# 处置：先探。默认链接能用就照旧；不能用就退到 -nostdlib。conftest 只需要
# **链接成功**，不需要真的能跑 —— 交叉编译时 configure 本来也不会运行它。
# -Wl,-e,main 是给 ld 一个入口符号，省掉 "cannot find entry symbol _start" 的警告。
# ---------------------------------------------------------------------------
LD_FLAGS_STR=""
pick_ldflags() {
  LD_FLAGS_STR=""
  if ! command -v "$CC" >/dev/null 2>&1; then
    echo "[curl] ✗ 找不到交叉编译器 $CC"
    return 1
  fi
  mkdir -p "$BASE"
  local pf="$BASE/_preflight"
  printf 'int main(void){return 0;}\n' >"$pf.c"

  if "$CC" $CFLAGS_STR $CPPFLAGS_STR -L"$SSL_OUT" "$pf.c" -o "$pf.elf" 2>"$pf.log"; then
    LD_FLAGS_STR="-L$SSL_OUT"
    echo "[curl] 预检链接 ok（工具链默认启动文件可用）"
    return 0
  fi

  echo "[curl] ! 工具链默认编译/链接失败，gcc 原话："
  head -8 "$pf.log" | sed 's/^/      /'
  echo "      （本项目所有用户程序都走 -nostdlib + 自带 crt0，这条路径从没被用过）"

  if "$CC" $CFLAGS_STR $CPPFLAGS_STR -nostdlib -nostartfiles -Wl,-e,main \
        -L"$SSL_OUT" "$pf.c" -o "$pf.elf" 2>"$pf.log"; then
    LD_FLAGS_STR="-nostdlib -nostartfiles -Wl,-e,main -L$SSL_OUT"
    echo "[curl] 预检链接 ok（退回 -nostdlib：conftest 只链接不运行，够用）"
    return 0
  fi

  echo "[curl] ✗ 连 -nostdlib 也链接不出来："
  head -15 "$pf.log" | sed 's/^/      /'
  return 1
}

# ---------------------------------------------------------------------------
# configure 的链接探测需要一个"符号存在"的环境。
#
# 有三道硬门槛**没有 cache 变量可覆盖** —— acinclude.m4 里是裸的
# AC_LINK_IFELSE 加 AC_MSG_ERROR，程序里写死要链接 recv / send / connect：
#     CURL_CHECK_FUNC_RECV     acinclude.m4:653  Unable to link function recv
#     CURL_CHECK_FUNC_SEND     acinclude.m4:700  Unable to link function send
#     CURL_CHECK_LIBS_CONNECT  acinclude.m4:962  cannot find connect
# 在 -nostdlib 下一个符号都没有，configure 到第一个就 abort。
#
# 而我们的 libc 是 header-only 的 static inline —— 这些函数**是有的**，
# 只是链接期不存在符号。所以把 user/include 里所有 static inline 的名字抽出来，
# 生成一份 `void name(void) {}` 的空壳打进 libcprobe.a，只塞给 configure 的
# LIBS（conftest 链接时用），**不参与 libcurl.a / curl.elf 的最终链接**。
#
# 光有 inline 名不够：openssl 那一步是 AC_CHECK_LIB(crypto, HMAC_Update)，
# 要真的把 libcrypto.a 拉进来，而它引用的是 string.c/mem.c 里的**实体符号**
# （memcpy / memset / errno …）和 libgcc 的 __*di2。所以桩库还要补：
#   · user/include 里的普通原型声明（memcpy 这类不在 inline 名单里）
#   · libssl.a / libcrypto.a 的 nm -u 未定义符号（精确，且不夸大）
#   · errno 这类**数据**符号（ld 只认名字，不看类型）
# 以及给 configure 的 LIBS 末尾挂 -lgcc：-nostdlib 下 gcc 不会自己补，
# 而 libcrypto.a 确实要 __ctzdi2 那几个。
#
# 顺带的好处：AC_CHECK_FUNC 的结论从"全 no"变成"头里声明的都有"，比手写
# cache 清单更接近事实 —— 清单永远会漏，上一轮就漏出了
# "checking for gethostbyname for Minix 3... yes" 这种荒唐结论。
# ---------------------------------------------------------------------------
PROBE_LIB=""
PROBE_SIG=""
build_probe_lib() {
  local names="$BASE/_probe_names.txt" cs="$BASE/_probe_stubs.c"
  local obj="$BASE/_probe_stubs.o" n
  PROBE_LIB="$BASE/libcprobe.a"
  mkdir -p "$BASE"

  # ① static inline 函数（libc 的主体形态）
  {
    grep -rhE '^[[:space:]]*static[[:space:]]+inline[[:space:]]' "$ROOT/user/include" 2>/dev/null \
      | sed -E 's/^[[:space:]]*static[[:space:]]+inline[[:space:]]+//' \
      | sed -E 's/[[:space:]]*\(.*$//' \
      | awk '{print $NF}'
    # ② 普通原型（memcpy / memset / __sync_lock_* 这些实体定义）
    grep -rhE '^[A-Za-z_][A-Za-z0-9_ *]*\**[A-Za-z_][A-Za-z0-9_]*[[:space:]]*\([^;]*\)[[:space:]]*;[[:space:]]*$' \
         "$ROOT/user/include" 2>/dev/null \
      | sed -E 's/[[:space:]]*\(.*$//' \
      | awk '{print $NF}'
  } | sed -E 's/^\*+//' \
    | grep -E '^[A-Za-z_][A-Za-z0-9_]*$' \
    | grep -vxE 'int|char|void|long|short|unsigned|signed|struct|const|float|double|union|enum|volatile|return|if|for|while|switch|sizeof|__attribute__|__asm__|__extension__|__inline' \
    | grep -vE '^__builtin_' \
    | grep -vxE 'errno|_stdin|_stdout|_stderr' \
    >"$names"

  # ③ 两个 .a 的未定义符号：openssl 的链接测试真正需要的那批
  if [ -f "$SSL_OUT/libcrypto.a" ]; then
    "$NM" -u "$SSL_OUT/libcrypto.a" "$SSL_OUT/libssl.a" 2>/dev/null \
      | awk '{print $NF}' | grep -E '^[A-Za-z_][A-Za-z0-9_]*$' \
      | grep -vxE 'errno|_stdin|_stdout|_stderr' >>"$names"
  fi

  # -------------------------------------------------------------------------
  # ③ 有个致命陷阱：**静态库内部是循环引用的**。
  # v3_info.o 定义了 ACCESS_DESCRIPTION_free，同时别的成员又在引用它 ——
  # 于是 `nm -u libcrypto.a` 会把"自己人"也列成未定义。照单造空壳，链接
  # 时就会和 libcrypto.a 里的真定义撞：
  #     multiple definition of `AES_cbc_encrypt'
  # 一屏 AES_* / ACCESS_DESCRIPTION_* / ASIdOrRange_it 全是这么来的。
  # （openssl_build.sh 的缺口扫描一直是 "U 集 − 定义集"，这里当时漏了后半段。）
  #
  # libgcc 的也一并剔掉：-lgcc 就在 LIBS 末尾，不需要桩，留着反而是拿空壳
  # 顶掉真正的 __ctzdi2。
  # -------------------------------------------------------------------------
  local defs="$BASE/_ssl_defs.txt"
  {
    "$NM" --defined-only "$SSL_OUT/libcrypto.a" "$SSL_OUT/libssl.a" 2>/dev/null \
      | awk 'NF>=3{print $NF}'
    "$NM" --defined-only "$("$CC" -print-libgcc-file-name)" 2>/dev/null \
      | awk 'NF>=3{print $NF}'
  } | grep -E '^[A-Za-z_][A-Za-z0-9_]*$' | sort -u >"$defs"

  sort -u "$names" -o "$names"
  if [ -s "$defs" ]; then
    grep -vxF -f "$defs" "$names" >"$names.f" || true
    mv "$names.f" "$names"
  fi
  if [ ! -s "$names" ]; then
    echo "[curl] ✗ 没从 $ROOT/user/include 抽到任何符号名，桩库没法建"
    return 1
  fi

  {
    echo "/* 自动生成：给 configure 的 conftest 提供符号，不参与最终链接 */"
    echo "int errno = 0;"          # 数据符号：libcrypto 里大量 set errno
    while read -r n; do echo "void $n(void) {}"; done <"$names"
  } >"$cs"

  # 必须用目标的 CFLAGS 编：桩库要进 riscv 的链接，x86 的 .o 会被 ld 拒收。
  if ! "$CC" $CFLAGS_STR -c "$cs" -o "$obj" 2>"$BASE/_probe_stubs.log"; then
    echo "[curl] ✗ 探测桩库编译失败（$BASE/_probe_stubs.log）："
    head -8 "$BASE/_probe_stubs.log" | sed 's/^/      /'
    return 1
  fi

  # -------------------------------------------------------------------------
  # 三个标准流必须单独一个 TU 定义。
  #
  # file.h:37-45 是 `extern FILE _stdin/_stdout/_stderr` + `#define stdout
  # (__init_stdout())`，即取 `&_stdout` —— 于是**任何**走 fprintf/日志的 TU
  # 都会产生对这三个**数据符号**的引用，openssl 里满地都是。
  #
  # 上面 ① ② ③ 三条都按名字把它们过滤掉了（`errno|_stdin|_stdout|_stderr`），
  # 因为它们是数据不是函数，写进 `void x(void){}` 会变成函数符号。我上一轮
  # 记得给 errno 手写 `int errno = 0;`，却忘了这三个 —— 于是预检直接报
  # `_stderr / _stdin / _stdout` 三条 undefined reference。
  #
  # 这里 include file.h 让编译器自己算 sizeof(FILE)（532 B，4 字节对齐），
  # 不手抄尺寸。**不能**并进上面那个 TU：那里若再出现同名函数桩就是
  # multiple definition。
  # -------------------------------------------------------------------------
  local sc="$BASE/_probe_streams.c" sobj="$BASE/_probe_streams.o"
  {
    echo "/* 自动生成：给 conftest 提供三个标准流的数据符号 */"
    echo '#include "file.h"'
    echo "FILE _stdin;"
    echo "FILE _stdout;"
    echo "FILE _stderr;"
  } >"$sc"
  if ! "$CC" $CFLAGS_STR $CPPFLAGS_STR -c "$sc" -o "$sobj" 2>"$BASE/_probe_streams.log"; then
    echo "[curl] ✗ 标准流符号编译失败（$BASE/_probe_streams.log）："
    head -8 "$BASE/_probe_streams.log" | sed 's/^/      /'
    return 1
  fi

  rm -f "$PROBE_LIB"
  "$AR" rcs "$PROBE_LIB" "$obj" "$sobj" || return 1
  # 签名取**符号名单**，不能取 libcprobe.a 的哈希：
  # ar 把成员的 mtime 写进归档头，而这个 .o 每次现编，mtime 每次不同
  # → 归档字节不同 → 哈希不同 → 签名永不稳定 → 每次运行都 `rm -rf $OBJ`
  # → 上一轮的 lib/libcurl.la 被一起删掉 → bin 阶段 make 报
  # "No rule to make target '../lib/libcurl.la'"（这个坑真踩过）。
  # 名单本身才是"桩库变了没有"的真实语义。
  PROBE_SIG="$(wc -l <"$names"):$(sha256sum <"$names" | cut -c1-16)"
  echo "[curl] 探测桩库：$(wc -l <"$names") 个符号 -> libcprobe.a"
  return 0
}

# ---------------------------------------------------------------------------
# curl 对 --with-openssl 的前缀有硬校验（m4/curl-openssl.m4）：
#     test ! -f "$PREFIX_OPENSSL/include/openssl/ssl.h" -> AC_MSG_ERROR(
#         "$PREFIX_OPENSSL is a bad --with-openssl prefix!")
# 而 openssl_build.sh 的产物布局是 libssl.a 直接在 $SSL_OUT 下、头文件在
# $SSL_OBJ/include 与 $SSL_SRC/include 两处（生成的 configuration.h 在前者）。
# 所以这里装配一个 install 形状的前缀：include/openssl 合并两处，lib 放软链。
# 只拷头，不拷 .a（用软链，免得每次重编 openssl 后这边是旧的）。
# ---------------------------------------------------------------------------
stage_ssl() {
  local inc="$SSL_PREFIX/include/openssl"
  # 前缀为空会展开成 /lib、/include —— 那两个目录普通用户碰不动，
  # 症状是 "mkdir: Permission denied" 然后报"ssl.h 不存在"，看起来像 OpenSSL 的锅。
  # 所以这里硬卡一下。
  if [ -z "$SSL_PREFIX" ]; then
    echo "[curl] ✗ 内部错误：SSL_PREFIX 为空（脚本被改坏了，检查顶部赋值）"
    return 1
  fi
  mkdir -p "$SSL_PREFIX/lib" || return 1
  if [ ! -f "$inc/ssl.h" ]; then
    rm -rf "$inc"
    mkdir -p "$inc"
    cp -a "$SSL_SRC/include/openssl/." "$inc/" 2>/dev/null
    # 生成的 configuration.h / opensslv.h 在 obj 里，后拷覆盖
    if [ -d "$SSL_OBJ/include/openssl" ]; then
      cp -a "$SSL_OBJ/include/openssl/." "$inc/" 2>/dev/null
    fi
  fi
  # 必须查返回值：脚本没开 set -e，ln 失败会静默，最后只在"ssl.h 不存在"上暴露，
  # 而那句话指向的是完全无关的地方。
  ln -sf "$SSL_OUT/libssl.a"    "$SSL_PREFIX/lib/libssl.a"    || return 1
  ln -sf "$SSL_OUT/libcrypto.a" "$SSL_PREFIX/lib/libcrypto.a" || return 1
  # curl 拼库路径用的是 "$PREFIX_OPENSSL/lib$libsuff"（m4/curl-openssl.m4:
  # LIB_OPENSSL="$PREFIX_OPENSSL/lib$libsuff"）。libsuff 在 64 位宿主机上可能
  # 是 "64"，那 -L 就会指向一个不存在的 lib64 —— 于是 -lcrypto 找不到，
  # configure 连甩四条 no。这里补一个相对软链，两种拼法都能命中。
  [ -e "$SSL_PREFIX/lib64" ] || ln -s lib "$SSL_PREFIX/lib64"
  if [ ! -f "$inc/ssl.h" ]; then
    echo "[curl] ✗ 装配 OpenSSL 前缀失败：$inc/ssl.h 不存在"
    echo "        检查 $SSL_SRC/include/openssl 与 $SSL_OBJ/include/openssl"
    return 1
  fi
  return 0
}

# ---------------------------------------------------------------------------
# OpenSSL 探测的失败信息是**一条链上四个 no**，真话只在第一个里。
#
# m4/curl-openssl.m4 的 CURL_WITH_OPENSSL 是这样串的：
#     AC_CHECK_LIB(crypto, HMAC_Update)          <- 真的失败在这里
#       └ no -> AC_CHECK_LIB(crypto, HMAC_Init_ex)   <- 同一个失败，换函数名再试
#           └ no -> 加 -ldl 再试                      <- 还是同一个失败
#               └ no -> 加 -ldl -lpthread 再试         <- 还是同一个失败
#                   └ 全部回滚 -> OPENSSL_ENABLED 空
# 然后 configure 在**一千行之后**才报
#     "--with-openssl was given but OpenSSL could not be detected"
# 中间夹着 "OpenSSL version does not speak any known QUIC API" —— 那句是
# 3.0.23 的正常形态（它本来就没有 QUIC API 那几个函数），跟失败毫无关系，
# 但位置离错误行最近，所以最容易被当成原因（上一轮就是这样）。
#
# 和 pick_ldflags 同一个道理：**先自己撞一次，把 ld 的原话拿出来**，不要让
# 使用者从 configure 的转述里反推。
# ---------------------------------------------------------------------------
probe_ssl_link() {
  local pf="$BASE/_preflight_ssl"
  mkdir -p "$BASE"
  # 与 AC_CHECK_LIB(crypto, HMAC_Update) 同形：只声明不 include 头文件，
  # 链接 -lcrypto，LIBS 顺序照 autoconf 的做法（-lcrypto 在最前）。
  printf 'char HMAC_Update ();\nint main(void) { return HMAC_Update (); }\n' >"$pf.c"
  if "$CC" $CFLAGS_STR $CPPFLAGS_STR -L"$SSL_PREFIX/lib" $LD_FLAGS_STR \
       "$pf.c" -o "$pf.elf" -lcrypto "$PROBE_LIB" -lgcc 2>"$pf.log"; then
    echo "[curl] 预检 libcrypto 链接 ok"
    return 0
  fi
  echo "[curl] ✗ 预检链接 libcrypto 失败 —— configure 那句"
  echo "       \"--with-openssl was given but OpenSSL could not be detected\" 就是这个。"
  echo "       （QUIC 那两行是 3.0.23 的正常形态，不是原因）"
  echo "       ld 原话："
  if grep -q 'multiple definition' "$pf.log"; then
    # 桩库和真定义撞了。这类错误一次几百行，必须按符号归并，否则看不出规律。
    local nmul
    nmul="$(grep -o "multiple definition of [\`'][^']*'" "$pf.log" | sed "s/.*of [\`']//; s/'$//" | sort -u)"
    echo "       multiple definition $(printf '%s\n' "$nmul" | wc -l) 个（去重）："
    printf '%s\n' "$nmul" | head -12 | sed 's/^/         /'
    echo "       => 桩库里出现了**别人已经定义**的符号。减法没做干净："
    echo "          build_probe_lib 必须把 libcrypto.a/libssl.a 的 --defined-only 集"
    echo "          从名字集里剔掉（静态库内部循环引用会让 nm -u 列出自己人）。"
  elif grep -q 'undefined reference' "$pf.log"; then
    echo "       缺符号（去重）："
    grep -o "undefined reference to [\`'][^']*'" "$pf.log" \
      | sed "s/.*to [\`']//; s/'\$//" | sort -u | head -30 | sed 's/^/         /'
    echo "       这些要补进 build_probe_lib 的名字集（含数据符号 —— 三个标准流"
    echo "       那种就属于被过滤掉却忘了手写定义的）。"
  elif grep -q 'cannot find' "$pf.log"; then
    grep 'cannot find' "$pf.log" | head -5 | sed 's/^/       /'
    echo "       路径问题：确认 $SSL_PREFIX/lib/libcrypto.a 存在且不是断链。"
  else
    head -20 "$pf.log" | sed 's/^/       /'
  fi
  echo "       （原始日志 $pf.log）"
  return 1
}

# configure 失败时，真正的错误只在 config.log 里。导出最后一个
# `configure:N: error:` 之前的 30 行 —— 那里正好是失败的 conftest 命令行
# 加编译器/链接器的原始输出。
dump_config_log() {
  local log="$OBJ/config.log" n s
  if [ ! -f "$log" ]; then
    echo "      (没有 config.log，configure 可能没真正启动)"
    return 0
  fi
  # --with-openssl 失败时最后那条 error 离真话有一千行，中间还夹着 QUIC 的
  # 正常输出。所以先单独把第一个 -lcrypto 链接测试整段捞出来 —— 那才是
  # 失败发生的地方（见 probe_ssl_link 的注释）。
  s="$(grep -n 'checking for HMAC_Update in -lcrypto' "$log" | head -1 | cut -d: -f1)"
  if [ -n "${s:-}" ]; then
    echo "      ↓ OpenSSL 探测的第一跳（后面三个 no 都是它的回声）："
    sed -n "${s},$(( s + 40 ))p" "$log" | sed 's/^/      /'
    echo
  fi
  n="$(grep -n '^configure:[0-9]*: error:' "$log" | tail -1 | cut -d: -f1)"
  if [ -z "${n:-}" ]; then
    echo "      (config.log 里没有 configure:N: error: 行，末尾 20 行：)"
    tail -20 "$log" | sed 's/^/      /'
    return 0
  fi
  # 编译器自己的报错在 config.log 里**不带 configure: 前缀**
  # （形如 conftest.c:16:23: error: unknown type name 'size_t'），
  # 而最后那条 configure:N: error: 只是 autoconf 的转述 —— 「Failed to find
  # size of size_t」这种话完全看不出是 size_t 没定义。所以单独把它捞出来。
  local cerr
  cerr="$(grep -E '^conftest\.c:[0-9]+:[0-9]*:? ?(fatal )?error:' "$log" | tail -6)"
  if [ -n "$cerr" ]; then
    echo "      ↓ conftest 的编译器原话（最后 6 条）："
    printf '%s\n' "$cerr" | sed 's/^/      /'
    echo
  fi
  echo "      ↓ 最后一条 configure:N: error: 及其前 60 行："
  sed -n "$(( n > 60 ? n - 60 : 1 )),${n}p" "$log" | sed 's/^/      /'
}

# ---------------------------------------------------------------------------
# 平台补丁：把 configure 判不出来、但**我们确知为真**的事实写进 curl_config.h。
#
# 这不是"绕过检查"，是 curl 上游自己就有的机制 —— lib/setup-os400.h、
# lib/setup-win32.h 是签入仓库的手写常量，压根不走 autoconf 探测。一个在
# autoconf 眼里只是"generic unix ELF"的目标平台，只能走同一条路。
#
# HAVE_FCNTL_O_NONBLOCK 为什么必须手写：
#   m4 里的 CURL_CHECK_FUNC_FCNTL 是四段串联：
#       if fcntl can be linked        ← AC_LANG_FUNC_LINK_TRY
#         → if fcntl is prototyped    ← AC_EGREP_CPP
#           → if fcntl O_NONBLOCK is compilable
#             → if fcntl usage allowed
#   第一段的 conftest 是 `#include <assert.h>` + `char fcntl ();`。我们的
#   fcntl() 是头文件里的 static inline **真原型**，只要任何一条 include 链
#   把它带进 conftest，`char fcntl ();` 立刻 conflicting types。
#   这条链既没有 ac_cv_* 可以预置（不是 AC_CACHE_CHECK），也没有
#   ACTION-IF-CROSS-COMPILING 分支 —— 判成 no 之后 configure 一个字都不说，
#   一路静默到 lib/curlx/nonblock.c 的
#       #error "no non-blocking method was found/used/set"
#
#   而我们确实有：内核 sys_fcntl 支持 F_GETFL / F_SETFL，O_NONBLOCK 记进
#   process.fd_flags，并在 connect / recv / send 三个入口真读取。
#   所以直接声明，语义上是**如实**的，只是绕开了一条判不准的探测链。
#
# 只补这一条：HAVE_FCNTL 在 8.x 全树只用于 Amiga 分支的 #undef，没有消费者，
# 补它等于凭空扩大语义面。
# ---------------------------------------------------------------------------
overlay_curl_config() {
  local h="$OBJ/lib/curl_config.h"
  [ -f "$h" ] || { echo "[curl] ✗ $h 不存在，configure 没产出配置头"; return 1; }
  grep -q 'RK_PLATFORM_OVERRIDES' "$h" && return 0   # 幂等
  cat >>"$h" <<'RKEOF'

/* =====================================================================
 * RmikuOS 平台补丁（本块由 scripts/curl_build.sh 追加，configure 不生成）
 * 原因见脚本里 overlay_curl_config() 的注释；curl 上游对 OS/400、Win32
 * 也是用签入仓库的 setup-*.h 手写这类常量，不走探测。
 * ===================================================================== */
#define RK_PLATFORM_OVERRIDES 1

/* configure 的 fcntl 探测链在第一步就断（conftest 里 `char fcntl ();`
 * 与我们头文件里的 static inline 真原型冲突），而且没有 cache 变量可覆盖。
 * 本内核 sys_fcntl 支持 F_GETFL / F_SETFL，O_NONBLOCK 会被 connect、
 * recv、send 三个入口真正读取，所以这里有 non-blocking 手段。 */
#define HAVE_FCNTL_O_NONBLOCK 1
RKEOF
  return 0
}

# ---------------------------------------------------------------------------
# configure 只在签名变化时重跑。签名里必须包含所有**会影响产物**的东西：
# configure 参数、CFLAGS、CPPFLAGS、CC 路径、cache 变量、源码版本。
# 少签一项的后果是"改了开关却没重配"，症状是行为诡异但不报错 —— openssl
# 那边靠 Configure 自己 rm -rf，autotools 没有这个保护，只能自己算。
# ---------------------------------------------------------------------------
configure_curl() {
  ensure_configure || return 1
  mkdir -p "$OBJ"
  pick_ldflags || {
    echo "[curl] ✗ 这套参数链接不出任何可执行文件，configure 必然失败，先解决工具链"
    return 1
  }
  build_probe_lib || return 1
  stage_ssl || return 1
  probe_ssl_link || return 1
  local sig
  sig="$(printf '%s\n' "$CURL_VER" "$CROSS_HOST" "${FEATURE_OPTS[@]}" "${PROTO_OPTS[@]}" \
                     "${CONFIGURE_CACHE[@]}" "$CFLAGS_STR" "$CPPFLAGS_STR" "$LD_FLAGS_STR" \
                     "$PROBE_SIG" "$CC" \
         | sha256sum | cut -d' ' -f1)"
  if [ -f "$OBJ/config.h" ] && [ "$(cat "$OBJ/.cfg_sig" 2>/dev/null)" = "$sig" ]; then
    echo "[curl] configure 参数没变，复用 $OBJ"
    overlay_curl_config || return 1
    return 0
  fi
  echo "[curl] configure（交叉目标 $CROSS_HOST）"
  rm -rf "$OBJ"
  mkdir -p "$OBJ"
  # LIBS 的位置在 gcc 命令行最尾部（autoconf 的链接规则），所以 -lgcc 排最后。
  # -nostdlib 下 gcc 不会自己补 -lgcc，而 libcrypto.a 确实要 __ctzdi2 那几个。
  local libs_str="$PROBE_LIB -lgcc"
  (
    cd "$OBJ" || exit 1
    CC="$CC" AR="$AR" RANLIB="$RANLIB" NM="$NM" \
    CFLAGS="$CFLAGS_STR" CPPFLAGS="$CPPFLAGS_STR" \
    LDFLAGS="$LD_FLAGS_STR" \
    LIBS="$libs_str" \
    "$SRC/configure" \
      --host="$CROSS_HOST" \
      --build="x86_64-pc-linux-gnu" \
      --prefix=/usr \
      --with-openssl="$SSL_PREFIX" \
      "${FEATURE_OPTS[@]}" \
      "${PROTO_OPTS[@]}" \
      "${CONFIGURE_CACHE[@]}" \
      >"$BASE/configure.log" 2>&1
  ) || {
    echo "[curl] ✗ configure 失败，末尾 30 行："
    tail -30 "$BASE/configure.log" | sed 's/^/      /'
    echo
    echo "[curl] config.log 里失败的那个 conftest（这才是真正的原因）："
    dump_config_log
    echo
    echo "[curl] 常见原因："
    echo "      · unrecognized option —— 这个版本的 curl 没有那个 --disable-xxx，删掉那行"
    echo "      · C compiler cannot create executables —— 看上面 conftest 那段；"
    echo "        多为缺 crt0.o / -lc，或某个 -m* 选项这个工具链不认"
    echo "      · cannot run test program while cross compiling —— 该宏没有交叉编译兜底，"
    echo "        加一条 a_cv_xxx=yes 到 CONFIGURE_CACHE（先看 configure.log 里是哪一项）"
    echo "      · 找不到 openssl/ssl.h —— 确认 SSL_OBJ/include 存在且 openssl 已跑过 lib 阶段"
    return 1
  }
  # autoconf 对未知的 --enable/--disable/--with 只发 WARNING，退出码仍是 0。
  # 不主动查的话，"选项写错 → configure 成功 → 开关静默不生效"这条路径
  # 完全没有声音 —— threaded-resolver 差一个连字符就是这么漏掉的。
  if grep -q "unrecognized options" "$BASE/configure.log"; then
    echo "[curl] ! configure 报了未知选项（不会失败，但那个开关没生效）："
    grep -A1 "unrecognized options" "$BASE/configure.log" | head -8 | sed 's/^/      /'
  fi
  overlay_curl_config || return 1
  echo "$sig" >"$OBJ/.cfg_sig"
  echo "[curl] ✓ configure 完成（日志 $BASE/configure.log）"
  return 0
}

# ---------------------------------------------------------------------------
# 从 autoconf 的 config.log 里挖出**某一条 check 的完整块**：
# 链接/编译命令行 + 编译器原话 + 那个失败的 conftest 程序本身，一直挖到 result:。
#
# 必须读 $OBJ/config.log 而不是 $BASE/configure.log：
# configure.log 只是 stdout 的转录，里面只有 "checking ... no" 几个字，
# 一个字的真实原因都没有。之前就是读错了文件，于是告警块打出一堆
# 与 fcntl 无关的后续检查行（看着像"失败原因"，其实全是噪声）。
# ---------------------------------------------------------------------------
dump_check_block() {
  local key="$1" log="$OBJ/config.log" s
  [ -f "$log" ] || { echo "            (没有 config.log)"; return 0; }
  s="$(grep -n "checking $key" "$log" | head -1 | cut -d: -f1)"
  if [ -z "${s:-}" ]; then
    echo "            (config.log 里没有 \"checking $key\"，要么这条检查没跑，要么被 cache 掉了)"
    return 0
  fi
  awk -v s="$s" 'NR>=s {print; if (NR>s && /^configure:[0-9]+: result:/) exit}' "$log" \
    | cut -c1-150 | head -30 | sed 's/^/            /'
}

# ---------------------------------------------------------------------------
# configure 相信了什么。这一步比"编没编过"重要：
# 交叉编译下 configure 会把大量能力判成 no，而 curl 的源码靠这些宏决定
# 走哪条实现。判错不会有构建错误，只会在 guest 里表现成"解析失败"。
# ---------------------------------------------------------------------------
dump_config() {
  echo
  echo "[curl] configure 的结论（只列会影响网络路径的）"
  local keys="HAVE_GETADDRINFO HAVE_GETADDRINFO_THREADSAFE
              HAVE_FCNTL HAVE_FCNTL_H HAVE_FCNTL_O_NONBLOCK
              HAVE_UNISTD_H HAVE_POLL HAVE_SYS_SELECT_H HAVE_SYS_POLL_H
              HAVE_POLL_H HAVE_NETDB_H USE_IPV6
              HAVE_SOCKET HAVE_GETIFADDRS HAVE_SOCKETPAIR
              SIZEOF_CURL_OFF_T"
  local k v lno mk tag
  # 平台补丁块起始行：在它之后的 #define 是**我们**补的，不是 configure 判的。
  # 不标出来的话，下一次看到 HAVE_FCNTL_O_NONBLOCK = 1 会误以为探测链通了。
  mk="$(grep -n 'RK_PLATFORM_OVERRIDES' "$OBJ/lib/curl_config.h" 2>/dev/null | head -1 | cut -d: -f1)"
  mk="${mk:-0}"
  for k in $keys; do
    # 三种状态必须分开，混在一起就是上一轮误判的来源：
    #   ① `#define X 1`        → 探测通过
    #   ② `/* #undef X */`    → 探测过了，结论是 no       ← autoheader 的写法，是**注释**
    #   ③ 两行都没有           → 这个版本的 curl 根本没有这个宏
    # 旧版只判 ①，于是 ②③ 一律打成"(未定义)"。上一屏里 HAVE_SELECT /
    # HAVE_INET_PTON / USE_THREADED_RESOLVER 全是在 ③（curl 8.x 里压根不存在
    # 这几个宏），却被当成"我们缺"，白追一轮。
    # 键名后面必须有空白或行尾，否则 HAVE_FCNTL 会去匹配 `#define HAVE_FCNTL_H 1`
    # 那一行，打成 "HAVE_FCNTL = _H 1"（上一轮就是这个）。
    lno="$(grep -nE "^#define[[:space:]]+$k([[:space:]]|\$)" "$OBJ/lib/curl_config.h" 2>/dev/null | head -1 | cut -d: -f1)"
    v=""
    if [ -n "$lno" ]; then
      v="$(sed -n "${lno}p" "$OBJ/lib/curl_config.h" \
            | sed -E "s/^#define[[:space:]]+$k//; s/^[[:space:]]+//; s/[[:space:]]+\$//")"
      [ -n "$v" ] || v=1        # `#define X`（无值）在 C 里就等于 1
    fi
    tag=""
    [ -n "$lno" ] && [ "$mk" -gt 0 ] && [ "$lno" -gt "$mk" ] && tag="   ← 本脚本补的"
    if [ -n "$v" ]; then
      printf '        %-26s = %s%s\n' "$k" "$v" "$tag"
    elif grep -q "^/\* #undef $k \*/" "$OBJ/lib/curl_config.h" 2>/dev/null; then
      printf '        %-26s = (探测过，结论 no)\n' "$k"
    else
      printf '        %-26s = (本版本无此宏)\n' "$k"
    fi
  done

  # 这两条是硬红线，不满足就别浪费时间编了。
  if ! grep -q '^#define HAVE_GETADDRINFO' "$OBJ/lib/curl_config.h" 2>/dev/null; then
    echo
    echo "[curl] ⚠ HAVE_GETADDRINFO 没定义。curl 会改走 gethostbyname_r，"
    echo "          而那个我们没有 —— 运行期所有域名解析必然失败（IP 字面量也失败，"
    echo "          因为两条路径都在同一个函数里分流）。"
    echo "          处置：确认 CONFIGURE_CACHE 里的 ac_cv_func_getaddrinfo=yes 生效。"
  fi
  if ! grep -q '^#define HAVE_FCNTL_O_NONBLOCK' "$OBJ/lib/curl_config.h" 2>/dev/null; then
    echo
    echo "[curl] ⚠ HAVE_FCNTL_O_NONBLOCK 没定义 —— 平台补丁没落进 curl_config.h。"
    echo "          它本该由 overlay_curl_config() 补上（见脚本里那段长注释）。"
    echo "          可能的原因：\$OBJ 被清过但补丁没重跑 / 脚本没同步到目标机 /"
    echo "          上游把这个宏改名了。"
    echo "          没有它，lib/curlx/nonblock.c 会直接"
    echo "          #error \"no non-blocking method was found/used/set\""
  fi
  # HAVE_FCNTL 仍然是 no：8.x 全树只有 Amiga 分支在 #undef 它，没有消费者，
  # 所以不补。这里把探测断在哪一步挖出来，纯粹为了确认原因没变。
  if ! grep -q '^#define HAVE_FCNTL ' "$OBJ/lib/curl_config.h" 2>/dev/null; then
    echo
    echo "[curl] 附：为什么 HAVE_FCNTL = no（不影响构建，只为留证据）"
    dump_check_block "if fcntl can be linked"
    # 最常见的成因：目标机上的 assert.h 还是旧版（include 了 stdio/stdlib）。
    # autoconf 的 AC_LANG_FUNC_LINK_TRY 模板固定 `#include <assert.h>`，
    # 旧版会顺着 stdlib.h → fs.h → fcntl.h 把真原型带进 conftest，
    # 于是 `char fcntl ();` conflicting types。直接点出来，别再猜。
    if grep -q 'include <stdio\.h>' "$ROOT/user/include/assert.h" 2>/dev/null; then
      echo
      echo "            ↑ 注意：$ROOT/user/include/assert.h 还是旧版"
      echo "              （里面 include 了 <stdio.h>，会连带把 <stdlib.h> → fs.h →"
      echo "              fcntl.h 整条链带进 conftest）。这大概率就是上面那条报错的原因："
      echo "              该文件是空 include 版的话，conftest 里根本看不到 fcntl。"
    fi
  fi
  echo
}

# ---------------------------------------------------------------------------
# 缺口扫描：nm 取 libcurl.a 的 U（未定义）集，减掉我方 libc 提供的符号。
# .S 必须一起编进去再取定义集，否则汇编实现的 syscall 全被算成"缺"。
# LC_ALL=C 不能省：locale 不同会让 sort 的序和 comm 期待的不一致，
# comm 直接给垃圾清单（openssl 那边踩过，白看半天）。
# ---------------------------------------------------------------------------
scan_undefined() {
  local lib="$OUT/libcurl.a"
  [ -f "$lib" ] || { echo "[curl] 没有 $lib，先跑 lib 阶段"; return 0; }

  local defs="$BASE/_provided.txt"
  : >"$defs"
  # 我方 libc 的实体符号（.c + .S）
  if ls "$ROOT"/user/lib/*.c >/dev/null 2>&1; then
    for f in "$ROOT"/user/lib/*.c; do
      "$CC" "${ARCHFLAGS[@]}" -std=gnu11 -O2 -ffreestanding -fno-builtin \
        -I"$ROOT/user/include" -c "$f" -o "$BASE/_scan_$(basename "${f%.c}").o" 2>/dev/null \
        && "$NM" --defined-only "$BASE/_scan_$(basename "${f%.c}").o" 2>/dev/null \
           | awk '{print $NF}' >>"$defs"
    done
  fi
  for f in "$ROOT"/user/lib/*.S; do
    [ -f "$f" ] || continue
    "$CC" "${ARCHFLAGS[@]}" -c "$f" -o "$BASE/_scan_$(basename "${f%.S}").o" 2>/dev/null \
      && "$NM" --defined-only "$BASE/_scan_$(basename "${f%.S}").o" 2>/dev/null \
         | awk '{print $NF}' >>"$defs"
  done
  # libgcc 族（__ctzdi2 / __divdi3 之类由编译器运行时提供，不是 libc 缺口）
  "$NM" --defined-only "$("$CC" -print-libgcc-file-name)" 2>/dev/null | awk '{print $NF}' >>"$defs"
  # libcurl.a **自己定义**的符号必须减掉，这是上一轮"缺口 952 个"的全部成因。
  # 静态归档内部成员互相引用：bufq.c 定义 Curl_bufq_init，http.c 引用它，
  # 于是 nm -u 把 Curl_bufq_* 这一大票"自己人"也列成未定义。不减去它们，
  # 清单里 95% 是 Curl_* 内部符号，真正的 libc 缺口被埋在里面看不见。
  # （同一个减法在 build_probe_lib 里也踩过一次，方向相反。）
  "$NM" --defined-only "$lib" 2>/dev/null | awk 'NF==3{print $3}' >>"$defs"
  # libssl / libcrypto 提供的那部分（curl 静态链接要连它们）
  "$NM" --defined-only "$SSL_OUT/libssl.a"       2>/dev/null | awk 'NF==3{print $3}' >>"$defs"
  "$NM" --defined-only "$SSL_OUT/libcrypto.a"    2>/dev/null | awk 'NF==3{print $3}' >>"$defs"

  local undef="$BASE/_undef.txt" miss="$BASE/_missing.txt"
  # 这里曾经是两个 bug 叠一起，直接把清单变成垃圾（缺口 201 个全是假的）：
  #   ① `nm -u some.a` 对**每个归档成员**都打一行标题 `libcurl_la-altsvc.o:`，
  #      它的 NF 是 1，`awk '{print $NF}'` 就拿这行标题当符号 → 200 来条 "缺口"。
  #      加 $1 ~ /^[Uu]$/ 卡住：未定义行才是 `… U Curl_foo`（$1 恰是 U）。
  #   ② `sed 's/^_*//'` 只作用在**未定义**这一侧，定义侧（libgcc 的 __ctzdi2、
  #      我方的 __sync_lock_*）都带前缀 → 永远匹配不上，于是 ctzdi2 被报成缺口。
  #      要么两边一起剥，要么都不剥；这里选都不剥（ELF 本来就不加下划线前缀）。
  "$NM" --undefined-only "$lib" 2>/dev/null \
    | awk '$1 ~ /^[UuWw]$/ {print $NF}' | sort -u >"$undef"
  sort -u "$defs" >"$BASE/_provided_sorted.txt"

  # 这里**故意不**去抓 user/include 里的 static inline 名字当"已定义"。
  # 理由：static inline 在使用的那个 TU 里就地展开，压根不会产生未定义引用，
  # 所以它本来就不会出现在 U 集里。把它们塞进"已定义"只会掩盖真正的缺口 ——
  # 一个只有 inline 声明、却没有实现关联的符号，正是最该被报出来的东西。

  comm -23 "$undef" "$BASE/_provided_sorted.txt" >"$miss"

  local n; n="$(wc -l <"$miss")"
  echo
  if [ "$n" -eq 0 ]; then
    echo "[curl] ✓ 缺口 0（libcurl.a 里所有未定义符号都有来源）"
  else
    echo "[curl] 缺口 $n 个（去重后）："
    head -60 "$miss" | sed 's/^/      /'
    echo
    echo "      注意：这里会包含 libc 里确实没有的东西，逐个看，别一次全补。"
    echo "      常见无害项：__stack_chk_fail（不该出现，出现说明 -fno-stack-protector 没生效）、"
    echo "      getpwuid_r / getifaddrs（明确不支持，相关功能裁剪掉即可）。"
  fi
}

# ---------------------------------------------------------------------------
case "$STAGE" in
  probe)
    configure_curl || exit 1
    dump_config
    echo "[curl] 编几个关键 TU（压头文件面，不做链接）"
    # 选这几个是因为它们分别代表不同风险：
    #   hostip4.c   —— getaddrinfo / sockaddr 布局 / PF_INET 这类常量
    #   connect.c   —— non-blocking connect 状态机、fcntl、SO_ERROR
    #   select.c    —— fd_set / poll 抽象
    #   curlx/nonblock.c —— HAVE_FCNTL_O_NONBLOCK 的 #error 就住在这里
    #                       （八百年没变过的写法：8.x 从 curl_setup.h 搬过来了，
    #                        找不到那句 #error 时别以为它消失了）
    #   vtls/openssl.c —— TLS 后端，最挑头文件
    probe_ok=0
    for f in lib/vdns/hostip4.c lib/connect.c lib/select.c \
             lib/curlx/nonblock.c lib/vtls/openssl.c; do
      [ -f "$SRC/$f" ] || { echo "  -  $f 不存在（版本差异，跳过）"; continue; }
      log="$BASE/probe_$(basename "${f%.c}").log"
      # -DHAVE_CONFIG_H 不能省，这是 lib 目录下编译的**前提**：
      # curl_setup.h 开头就是
      #     #ifdef HAVE_CONFIG_H
      #     #include "curl_config.h"
      #     #endif
      # 少了它，configure 的全部结论一行都看不见，于是四个 TU 一起报
      #   #error "too small curl_off_t"      (SIZEOF_CURL_OFF_T 未定义 → 当 0)
      #   redefinition of 'struct timeval'  (HAVE_STRUCT_TIMEVAL 没进来)
      #   #error "Missing definition of macro sread"/"swrite"  (HAVE_RECV/SEND)
      # 表面像四个独立缺陷，实际是"没读配置"。automake 的 lib/Makefile 里
      # DEFS 就带着它，走 make 的时候不会踩 —— 只有我们手工编才会。
      # -DBUILDING_LIBCURL 同理（lib/Makefile 的 AM_CPPFLAGS）。
      if "$CC" $CFLAGS_STR $CPPFLAGS_STR -DHAVE_CONFIG_H -DBUILDING_LIBCURL \
           -I"$OBJ" -I"$OBJ/include" -I"$OBJ/lib" \
           -I"$SRC" -I"$SRC/include" -I"$SRC/lib" \
           -c "$SRC/$f" -o "$BASE/probe_$(basename "${f%.c}").o" 2>"$log"; then
        echo "  ok $f"
      else
        echo "  ✗  $f"
        grep -E 'error:' "$log" | head -6 | sed 's/^/        /'
        probe_ok=1
      fi
    done
    echo
    echo "[curl] probe 完成（有 ✗ 时看 $BASE/probe_*.log）"
    exit $probe_ok
    ;;

  lib)
    configure_curl || exit 1
    dump_config
    echo "[curl] make -C lib（-j$NPROC，只编 libcurl.a）"
    make -C "$OBJ/lib" -j"$NPROC" 2>&1 | tee "$BASE/make_lib.log" | tail -30
    echo
    echo "[curl] 日志: $BASE/make_lib.log"
    if [ -f "$OBJ/lib/.libs/libcurl.a" ]; then
      mkdir -p "$OUT"
      cp "$OBJ/lib/.libs/libcurl.a" "$OUT/libcurl.a"
      echo "[curl] ✓ libcurl.a $(wc -c <"$OUT/libcurl.a") bytes"
      scan_undefined
    else
      echo "[curl] ✗ 未产出 libcurl.a —— 错误按句子归并："
      sed -n -e 's/.*: fatal error: //p' -e 's/.*: error: //p' "$BASE/make_lib.log" \
        | sort | uniq -c | sort -rn | head -20 | sed 's/^/      /'
      exit 1
    fi
    ;;

  bin)
    configure_curl || exit 1
    [ -f "$OUT/libcurl.a" ] || {
      echo "[curl] 还没有 libcurl.a，先跑 lib 阶段"
      exit 1
    }

    # libc 裸件，和 openssl 的 bin 阶段完全同一套。
    echo "[curl] 编 libc 裸件（crt0 / syscall / string / mem）"
    for pair in "crt0_$ARCH.S:_crt0.o" "syscall_$ARCH.S:_syscall.o" "string.c:_string.o" "mem.c:_mem.o"; do
      src_f="${pair%%:*}"; dst_f="${pair##*:}"
      "$CC" "${ARCHFLAGS[@]}" -std=gnu11 -O2 -ffreestanding -fno-builtin \
        -I"$ROOT/user/include" -c "$ROOT/user/lib/$src_f" -o "$BASE/$dst_f" || exit 1
    done

    LINK_FLAGS="-nostdlib -nostartfiles -static -no-pie -Wl,--build-id=none"
    LINK_FLAGS="$LINK_FLAGS $BASE/_crt0.o $BASE/_string.o $BASE/_mem.o $BASE/_syscall.o"
    LINK_FLAGS="$LINK_FLAGS -T $ROOT/user/linker-$ARCH.ld"

    # 只编 .o，不碰 curl 这个链接目标。
    #
    # 原来的写法是 `make -C src` 然后 `ls src/*.o`，两处都错：
    #   ① make 一上来就为 `curl` 建依赖图，而它依赖 ../lib/libcurl.la（libtool
    #      产物）。这个文件一旦不在，make 就报 "No rule to make target" 并**退出**，
    #      只有恰好被 -j 抢先调度的十几个 .o 编了出来。上一轮就是 13 个，
    #      于是链接期才报 `undefined reference to toolx_localtime`
    #      —— 它住在 src/toolx/tool_time.c，压根没被编。
    #   ② 就算编全了，`ls src/*.o` 也只覆盖顶层。src/toolx/ 是子目录
    #      （automake 的 subdir-objects），.o 落在 src/toolx/curl-tool_time.o。
    #      所以这个 glob 天生漏掉整个 toolx/。
    #
    # 现在把 curl_OBJECTS 从生成的 Makefile 里问出来，当**显式目标**交给 make：
    # 只建对象、不建 curl，天然不会踩到那个 .la。--eval 用 `目标: ; 动作`
    # 的单行形式，省掉 recipe 行必须打头的 TAB（脚本里塞真 TAB 太脆）。
    src_goals="$(make -s -C "$OBJ/src" \
                   --eval='print-curl-objects: ; @echo $(curl_OBJECTS)' \
                   print-curl-objects 2>/dev/null)"
    if [ -n "$src_goals" ]; then
      echo "[curl] make -C src（只编 .o，$(printf '%s\n' $src_goals | wc -l) 个目标）"
      # shellcheck disable=SC2086
      make -C "$OBJ/src" -j"$NPROC" $src_goals 2>&1 | tee "$BASE/make_src.log" | tail -20
    else
      echo "[curl] ! 问不出 curl_OBJECTS（make 版本太老或 Makefile 结构不同），退回整目录 make"
      : >"$OBJ/lib/libcurl.la"    # 让依赖图能解析；后面 libtool 自己的链接会失败，不管它
      make -C "$OBJ/src" -j"$NPROC" LDFLAGS="$LINK_FLAGS" LIBS="-lgcc" \
        2>&1 | tee "$BASE/make_src.log" | tail -20
    fi

    # 递归收 .o：src/toolx/ 这类子目录必须算进来，同时排除 libtool 的 .libs。
    OBJS=$(find "$OBJ/src" -name '*.o' -not -path '*/.libs/*' | sort)
    if [ -z "$OBJS" ]; then
      echo "[curl] ✗ src 下一个 .o 都没编出来，先看 $BASE/make_src.log"
      grep -E 'error:' "$BASE/make_src.log" | head -10 | sed 's/^/      /'
      exit 1
    fi

    echo "[curl] 手工链接 curl.elf"
    # shellcheck disable=SC2086
    "$CC" "${ARCHFLAGS[@]}" $LINK_FLAGS -o "$OUT/curl.elf" \
      $OBJS \
      "$OUT/libcurl.a" \
      "$SSL_OUT/libssl.a" "$SSL_OUT/libcrypto.a" \
      -lgcc 2>&1 | tee "$BASE/link.log" | tail -30

    if [ -s "$OUT/curl.elf" ]; then
      echo
      echo "[curl] ✓ curl.elf $(wc -c <"$OUT/curl.elf") bytes"
      # 链接能过，本身就是「缺口 = 0」的权威证明：ld 不会放过任何一个未定义
      # 符号。lib 阶段那份 scan_undefined 只是提前预警，会被 nm 的归档标题行带偏
      # （见那边的注释），两份结论打架时以这里为准。
      if "$NM" --undefined-only "$OUT/curl.elf" 2>/dev/null | grep -q .; then
        echo "[curl] ! 静态二进制里还有未定义符号（异常，说明链接靠 -shared 之类蒙过去）："
        "$NM" --undefined-only "$OUT/curl.elf" | head -10 | sed 's/^/      /'
      fi
      # 证物：OpenSSL 是不是真的链进了镜像，而不是 configure 嘴上说说。
      # 只看 curl -V 会骗人（版本串是编译期字符串），符号表才认账。
      if "$NM" "$OUT/curl.elf" 2>/dev/null | grep -qw 'SSL_CTX_new'; then
        echo "[curl]   证物：符号表里有 SSL_CTX_new（OpenSSL 真链进去了）"
      else
        echo "[curl]   ! 符号表里没有 SSL_CTX_new —— TLS 后端可能没被编进来"
      fi
      echo "[curl]   下一步在 guest 里先跑： curl -V"
      echo "[curl]   version 行里要看到 OpenSSL/3.0.23 和 libcurl/$CURL_VER"
    else
      echo
      echo "[curl] ✗ 链接失败"
      if grep -q 'undefined reference' "$BASE/link.log"; then
        echo "       缺符号（去重）："
        grep -o "undefined reference to [\`'][^']*'" "$BASE/link.log" \
          | sed "s/^undefined reference to [\`']//; s/'$//" | sort -u | head -30 | sed 's/^/        /'
        echo
        echo "       这些要去 user/include 里补（header-only static inline），"
        echo "       或者确认对应的功能开关已经 --disable 掉。"
      fi
      exit 1
    fi
    ;;

  check)
    scan_undefined
    ;;

  *)
    echo "未知阶段: $STAGE（probe | lib | bin | check）"
    exit 2
    ;;
esac
