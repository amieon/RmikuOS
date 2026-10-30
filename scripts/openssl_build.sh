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
  echo "          rm -rf user/openssl/src/.git   # 只删这个，别的目录一个都别删（原因见下）"
  exit 1
fi

# ---------------------------------------------------------------------------
# 版本自报。整条路线（no-threads → crypto/threads_none.c、provider 能不能静态链）
# 都是照 3.0 设计的，所以先把它打出来；不是 3.0.x 只警告不拦，但风险要说清。
# ---------------------------------------------------------------------------
openssl_version() {
  local vf="$SRC/VERSION.dat" m n p
  if [ -f "$vf" ]; then
    m="$(sed -n 's/^MAJOR=//p' "$vf" | tr -d '"')"
    n="$(sed -n 's/^MINOR=//p' "$vf" | tr -d '"')"
    p="$(sed -n 's/^PATCH=//p' "$vf" | tr -d '"')"
    echo "${m:-?}.${n:-?}.${p:-?}"
  else
    echo unknown
  fi
}

SRC_VER="$(openssl_version)"
echo "[openssl] 源码版本 $SRC_VER（$SRC）"
case "$SRC_VER" in
  3.0.*) : ;;
  *) echo "[openssl] ! 不是 3.0.x。no-threads 在别的版本走的是另一套实现，"
     echo "            且部分开关被改名/移除；真出问题优先换回 3.0："
     echo "            git clone --depth 1 -b openssl-3.0 ... user/openssl/src" ;;
esac

# ---------------------------------------------------------------------------
# 关掉的开关只有两类理由：**我们没有这个能力**，或者**它会引入我们测不了的分支**。
# 每个都写明理由，以后想开一个先读一遍，别凭"反正 no-asm 也过了"去试。
#
# 注意两类**不存在**的开关（踩过）：
#   · 算法级 no-aes / no-md5 / no-sha / no-hmac —— 3.x 把它们从 Configure 里删了，
#     算法搬进了 provider，改 Configure 不影响编进去什么。想减体积用 no-legacy
#     加运行期 provider 配置，不要在这里堆 no-xxx。
#   · no-docs —— 3.0 的 @disablables 里没有它（文档早就改成单独构建），
#     传了会被判 unsupported 直接 die。build_libs 本来也不生成 man 页。
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
  no-legacy    # 少编一批老旧算法，省体积
  no-comp      # SSL 层压缩，要额外维护 zlib 绑定
)

# 熵源钉成 DEVRANDOM（展开就是 /dev/urandom, /dev/random, ...）。
# 默认值是 os，会展开成 GETRANDOM + DEVRANDOM 两条路：
#   · riscv64-unknown-elf 是裸机 target，不定义 __linux → syscall_random() 直接
#     errno=ENOSYS → 退化读 DEVRANDOM（probe 的宏 dump 已确认没有 __linux）；
#   · loongarch64-unknown-linux-gnu 会定义 __linux，可能真去调 syscall(__NR_getrandom)，
#     而内核没有 getrandom、libc 也没有通用 syscall()（只有 syscall3/6）。
# 与其让同一份代码在两个架构上走不同分支、还可能在 loongarch 上编不过，
# 不如显式声明"我们只有 /dev/urandom"。内核侧刚好加了这台设备。
# 可选值：os|getrandom|devrandom|egd|rdcpu|librandom|none，逗号分隔。
CNF_OPTS+=(--with-rand-seed=devrandom)

# 这几个被 Configure 拒掉就不是"少个开关"的问题，而是源码形态不对（或版本不对），
# 直接停 —— 自动裁剪只用来兜住可选开关，不能拿来蒙混过关。
CRITICAL_OPTS=(no-asm no-threads no-shared no-dso no-module)

# -I 顺序要紧：user/include 必须在工具链 sysroot 之前，否则 `#include <stdlib.h>`
# 会命中 newlib/glibc 的声明，malloc 变成 extern 引用 → 链接到它们那套堆 → 跨库
# free 直接崩。-I 天然排在系统目录前，所以只要写了就在前面，probe 阶段会验证。
# -fno-stack-protector 不是可选项：没有 __stack_chk_fail。
#
# 另外：工具链是 GCC 14，implicit-function-declaration 已经是 **error**（老版本
# 只是 warning）。所以"少一个原型"当场就拦住构建，这是好事 —— 缺口立刻可见，
# 不用等链接期的 undefined reference。别用 -Wno-implicit-function-declaration
# 把它糊过去，那等于把"缺声明"重新变成"运行期随机崩"。
CFLAGS_STR="-std=gnu11 ${ARCHFLAGS[*]} -O2 -ffreestanding -fno-builtin -fno-stack-protector -I$ROOT/user/include"

mkdir -p "$BASE"

# ---------------------------------------------------------------------------
# 生成头必须在任何编译之前存在 —— 而且**不能只靠 probe 阶段跑过一次**。
#
# 根 build.info 里一长串 GENERATE[include/openssl/xxx.h]=...h.in，都登记在顶层
# DEPEND[]（索引为空 = 无条件先建），落到 Makefile 就是 GENERATED_MANDATORY，
# 也就是目标 build_generated。Configure 只把声明解析进 configdata.pm，**不产文件**。
#
# 两个坑叠在一起：
#   1. `make libcrypto.a` **不会**自动先建这些头。头挂在 build_generated 上，而 .o
#      的编译规则并不依赖它，-j8 下直接开编 →
#      fatal error: openssl/opensslv.h: No such file or directory。
#      原来用 build_libs 时看不出来（build_libs 依赖 build_generated），换成裸 .a
#      目标躲开 apps/lib/libapps 的同时，把这个副作用一起躲掉了。—— 踩过
#   2. Configure 一变就 rm -rf "$OBJ"，生成头跟着一起没了，所以每次 Configure
#      之后都得补一遍。
# 所以把它挂成 Configure 的收尾动作，而不是某个阶段的一次性步骤。
# ---------------------------------------------------------------------------
ensure_generated() {
  if [ -f "$OBJ/include/openssl/opensslv.h" ] && [ -f "$OBJ/include/openssl/crypto.h" ]; then
    return 0
  fi
  echo "[openssl] make build_generated（opensslv.h / crypto.h / ssl.h 等生成头）"
  if ! make -C "$OBJ" -j"$NPROC" build_generated >"$BASE/make_generated.log" 2>&1 \
     || [ ! -f "$OBJ/include/openssl/opensslv.h" ]; then
    echo "[openssl] ✗ 生成头没出来，日志 $BASE/make_generated.log"
    tail -15 "$BASE/make_generated.log" | sed 's/^/          /'
    return 1
  fi
  return 0
}

# ---------------------------------------------------------------------------
# Configure：幂等。参数或 CFLAGS 变了就整份重建（不是 make clean ——
# configuration.h 变了必须重编全部 .o，而 make 不感知命令行变量变化，
# 这条坑 git 那边已经踩过：改了 -std 但旧 .o 被静默复用）。
# ---------------------------------------------------------------------------
configure_openssl() {
  local sig
  sig="$(printf '%s\n' "$SRC_VER" "$ARCH_CFG" "${CNF_OPTS[@]}" "$CFLAGS_STR" "$CC" | sha256sum | cut -d' ' -f1)"
  if [ -f "$OBJ/.cfg.sha256" ] && [ "$(cat "$OBJ/.cfg.sha256")" = "$sig" ]; then
    ensure_generated || return 1      # 生成头可能被 distclean/手删过
    return 0
  fi

  local log="$BASE/configure.log"
  local -a opts=("${CNF_OPTS[@]}")
  local attempt bad why b

  # ---- 先查源码树完整性 --------------------------------------------------
  # 坑一：**删源码目录 = 改构建配置**，不是省体积。
  # 根 build.info 第一行就是
  #   SUBDIRS=crypto ssl apps util tools fuzz providers doc
  # Configure 从这个列表开始按 SUBDIRS 递归 push 子目录，再逐个用 Text::Template
  # 读该目录的 build.info，读不到就
  #   die "Something went wrong with <dir>/build.info: No such file or directory"
  # fuzz / doc 前面**没有任何 IF 条件**，所以无条件要求存在 —— 3.0 也没有 no-docs
  # 能跳过它们（传了反被判 unsupported，见文件头）。demos 根本不在 SUBDIRS 里，
  # test 和 engines 是有条件的：
  #   IF[!$disabled{tests}]            SUBDIRS=test     ← 我们传了 no-tests
  #   IF[!$disabled{'deprecated-3.0'}] SUBDIRS=engines  ← 我们没关 deprecated
  # 即：能安全删的只有 .git 和 demos；test 是因为 no-tests 才碰巧能删（耦合，哪天
  # 开 tests 就炸）；doc / fuzz / engines 一个都不能动。省那 40MB 换一个
  # "Configure 死在 build.info" 完全不合算 —— 删 .git 就够了。
  local -a need=(crypto ssl apps util tools fuzz providers doc)
  local -a noinfo=()
  for b in "${need[@]}"; do
    [ -f "$SRC/$b/build.info" ] || noinfo+=("$b/build.info")
  done
  if [ ${#noinfo[@]} -gt 0 ]; then
    echo "[openssl] ✗ 源码树缺 build.info：${noinfo[*]}"
    echo "          这些目录被根 build.info 的 SUBDIRS 无条件要求，删目录=Configure 必死。"
    echo "          补回来（只删 .git，其余目录全留着）："
    echo "          rm -rf user/openssl/src && git clone --depth 1 -b openssl-3.0 \\"
    echo "            https://github.com/openssl/openssl.git user/openssl/src"
    return 1
  fi

  # 坑二：Configure 里还有一段目录探测（3.0 至今都在）：
  #   foreach (reverse sort('aes','aria','bf',...,'md5','sha','hmac','ripemd',...)) {
  #       unshift @argvcopy, "no-$_" if ! -d catdir($srcdir, 'crypto', $_); }
  # 也就是"crypto/ 下没这个目录 = 关掉这个算法"。这 22 个名字里，只有
  # aes/md5/sha/hmac 四个没有对应的 no-X 条目（算法进了 provider），ripemd 是
  # rmd160 的弃用别名 —— 于是"目录探测失败"最终表现为
  #   Deprecated: no-ripemd   +   Unsupported: no-aes no-hmac no-md5 no-sha
  # 和开关名写错长得一模一样。所以先自己查，把话讲清楚。
  local -a miss=()
  for b in aes md5 sha hmac ripemd; do
    [ -d "$SRC/crypto/$b" ] || miss+=("crypto/$b")
  done
  if [ ${#miss[@]} -gt 0 ]; then
    echo "[openssl] ✗ 源码树缺目录：${miss[*]}"
    echo "          这五个是 Configure 目录探测里唯一会报错的名字，缺了必然死在 Configure。"
    echo "          重新拉一份干净的（crypto/ 下任何目录都别动）："
    echo "          rm -rf user/openssl/src && git clone --depth 1 -b openssl-3.0 \\"
    echo "            https://github.com/openssl/openssl.git user/openssl/src"
    return 1
  fi

  for attempt in 1 2 3; do
    echo "[openssl] Configure（$ARCH，第 $attempt 次，共 ${#opts[@]} 个开关）"
    rm -rf "$OBJ"
    mkdir -p "$OBJ"
    (
      cd "$OBJ" || exit 1
      "$SRC/Configure" "$ARCH_CFG" "${opts[@]}" \
        CC="$CC" AR="$AR" RANLIB="$RANLIB" NM="$NM" \
        --prefix=/usr --openssldir=/etc/ssl \
        CFLAGS="$CFLAGS_STR"
    ) >"$log" 2>&1

    [ -f "$OBJ/Makefile" ] && break

    # Configure 用 die 拒掉不认识的开关，并且一次只报一批；弃用别名只 warn。
    mapfile -t bad < <(sed -n 's/^\*\*\*\*\* Unsupported options: *//p' "$log" \
                       | tr ',' '\n' | tr -d ' \r' | grep -v '^$' | sort -u)
    if [ ${#bad[@]} -eq 0 ]; then
      echo "[openssl] ✗ Configure 失败（不是开关名的问题），日志 $log"
      tail -20 "$log" | sed 's/^/          /'
      return 1
    fi

    # 分清"我们传的"和"它自己补的"。后者剔不掉（下一轮它还会补），
    # 所以不能靠重试 —— 那是死循环，正是上一版的表现。
    local -a mine=() alien=()
    for b in "${bad[@]}"; do
      if printf '%s\n' "${opts[@]}" | grep -qx "$b"; then mine+=("$b"); else alien+=("$b"); fi
    done

    if [ ${#alien[@]} -gt 0 ]; then
      echo "[openssl] ✗ Configure 自己补出来的开关：${alien[*]}"
      echo "          它们不在脚本的开关表里 —— 来自上面说的 crypto/ 目录探测。"
      echo "          目录明明在还报这个，说明它算出的 srcdir 不是 $SRC（srcdir 由"
      echo "          Configure 用 dirname(\$0) 推，正常不会错）。把这个输出发我。"
      grep -E '^\*\*\*\*\* (Deprecated|Unsupported)' "$log" | sed 's/^/          /'
      return 1
    fi

    # 关键开关被拒 = 源码形态不对，停在这里比剔掉后编出一堆 pthread_* 引用强
    for why in "${mine[@]}"; do
      if printf '%s\n' "${CRITICAL_OPTS[@]}" | grep -qx "$why"; then
        echo "[openssl] ✗ 关键开关 $why 不被这份源码接受（版本 $SRC_VER）"
        echo "          这不是可选优化，缺了它整套构建前提不成立。换 3.0 的分支再试。"
        return 1
      fi
    done

    echo "[openssl]   这份源码不接受：${mine[*]} —— 剔掉重试"
    local -a keep=()
    local o
    for o in "${opts[@]}"; do
      printf '%s\n' "${mine[@]}" | grep -qx "$o" || keep+=("$o")
    done
    opts=("${keep[@]}")
  done

  if [ -f "$log" ] && grep -q '^\*\*\*\*\* Deprecated options:' "$log"; then
    echo "[openssl]   弃用别名（只警告，不影响结果）：$(sed -n 's/^\*\*\*\*\* Deprecated options: *//p' "$log")"
  fi

  if [ ! -f "$OBJ/Makefile" ]; then
    echo "[openssl] ✗ Configure 始终没产出 Makefile，日志 $log"
    tail -20 "$log" | sed 's/^/          /'
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

  # 开关自检：Configure 收下参数 ≠ 它真写进了 config。configuration.h 是编译器
  # 唯一看到的事实，所以"我传了 no-async"和"代码里还有 async 分支"这种矛盾，
  # 只能拿这张表判。之前靠翻源码 guard 猜，纯属浪费轮次。
  local nos
  nos="$(grep -oE 'define OPENSSL_NO_[A-Z0-9_]+' "$OBJ/include/openssl/configuration.h" 2>/dev/null \
         | sed 's/define OPENSSL_NO_//' | sort | tr '\n' ' ')"
  echo "[openssl]   OPENSSL_NO_*（真正生效的）: ${nos:-（一个都没有）}"

  printf '%s\n' "$sig" > "$OBJ/.cfg.sha256"
  echo "[openssl]   日志 $BASE/configure.log"

  ensure_generated || return 1
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
    configure_openssl || exit 1        # 内部已含 ensure_generated

    # 挑 4 个文件：最底层的 mem、BN 核心、**熵源**（最担心的那条路）、时间。
    # 熵源路径是 3.0 的：1.1.1 的 crypto/rand/rand_unix.c 在 3.0 已搬到
    # providers/implementations/rands/seeding/ 下（crypto/rand/build.info 里只剩
    # rand_lib / rand_pool / prov_seed 这些），照 1.1.1 写会得到假的"No such file"。
    # 它编译期就决定走 getrandom 还是退化到 DEVRANDOM（/dev/urandom），比运行到
    # openssl rand 才报错早得多 —— 刚加的 /dev/urandom 正好落在这条路上。
    #
    # 这里不用 `make <target>.o`：3.0 给每个目标加了 libcrypto-lib- 前缀
    # （providers/implementations/rands/seeding/libcrypto-lib-rand_unix.o），
    # 拼错就是"没有这个目标"，白浪费一轮。直接调编译器，include 按 3.0 给全。
    INCS=(-I"$OBJ" -I"$OBJ/include" -I"$SRC" -I"$SRC/include" -I"$SRC/crypto"
          -I"$SRC/providers/common/include" -I"$SRC/providers/implementations/include")

    echo "[openssl] 探针编译（$ARCH）"
    for f in providers/implementations/rands/seeding/rand_unix.c crypto/mem.c crypto/bn/bn_add.c crypto/o_time.c; do
      err="$BASE/probe_$(basename "$f").err"
      if "$CC" $CFLAGS_STR "${INCS[@]}" -c "$SRC/$f" -o "$BASE/probe_$(basename "${f%.c}").o" 2>"$err"; then
        echo "  ok   $f"
      else
        # grep -c 无匹配时也会打印 0 并返回 1，用 `|| echo 0` 会拼出 "0\n0"
        n="$(grep -c 'error:' "$err" 2>/dev/null || true)"; n="${n:-0}"
        echo "  FAIL $f  ($n errors)"
        grep -E 'error:' "$err" | head -10 | sed 's/^/         /'
      fi
    done

    # 熵源分支自报。这条路两个架构会分叉，光看"编译过了"看不出来：
    # rand_unix.c 用 `#if defined(__linux) && defined(__NR_getrandom)` 决定调
    # syscall(__NR_getrandom) 还是走 DEVRANDOM。riscv64-unknown-elf 是裸机 target，
    # 不定义 __linux → 直接 errno=ENOSYS → 退化读 /dev/urandom；而
    # loongarch64-unknown-linux-gnu 会定义 __linux，可能真去调 syscall()。
    # 把最终生效的宏打出来，省得事后猜内核要不要加 getrandom。
    echo
    echo "[openssl] 熵源分支（预处理宏）"
    "$CC" $CFLAGS_STR "${INCS[@]}" -E -dM \
      "$SRC/providers/implementations/rands/seeding/rand_unix.c" 2>/dev/null \
      | grep -E '^#define (OPENSSL_RAND_SEED_\w+|DEVRANDOM\w*|OPENSSL_SYS_UNIX|__NR_getrandom|__linux)' \
      | sort | sed 's/^/      /'

    # 外部头混入检查：-H 列出所有实际被 include 的头，滤掉我们自己的和生成物，
    # 剩下的就是工具链 sysroot 里的。白名单只放编译器内建那几个（类型定义必须一致）。
    echo
    echo "[openssl] 检查有没有用到工具链 sysroot 的头（编译能过但 ABI 会错）"
    # 先确认这次 -fsyntax-only 本身通过了：编译失败时 -H 一条都不输出，谱系是空的，
    # 会被判成"ok 全部头都来自 user/include" —— 假 ok 比不查更坏（踩过）。
    if "$CC" $CFLAGS_STR "${INCS[@]}" -H -fsyntax-only "$SRC/crypto/mem.c" \
         >"$BASE/hdr_include.txt" 2>&1; then
      grep -E '^\.+ /' "$BASE/hdr_include.txt" | sed 's/^\.* //' | sort -u \
        | grep -vE "^($ROOT|$OBJ|$SRC)" \
        | grep -vE '/(stddef|stdarg|stdbool|float|stdint|limits|stdatomic|iso646)\.h$' \
        >"$BASE/foreign_headers.txt"
      if [ -s "$BASE/foreign_headers.txt" ]; then
        echo "  ! 以下头来自工具链而非 user/include，逐个确认结构体布局是否一致："
        sed 's/^/      /' "$BASE/foreign_headers.txt"
      else
        echo "  ok   全部头都来自 user/include 或 OpenSSL 自身"
      fi
    else
      echo "  ✗ 参照翻译单元编译不过，头谱系没法判（这不是 ok，是没测）"
      grep -E 'error:' "$BASE/hdr_include.txt" | head -8 | sed 's/^/      /'
    fi
    echo
    echo "[openssl] 日志: $BASE/probe_*.err"
    ;;

  lib)
    configure_openssl || exit 1
    # 不用 build_libs：它把所有 LIBS 都编一遍，**包括 apps/lib 的 noinst 库
    # libapps**（apps/lib/build.info 里 LIBS{noinst}=libapps），于是
    # apps/lib/apps.c 被拖进来——它要 fd_set/select/setbuf，而且里面有一堆
    # 和库无关的 CLI 代码。库阶段只认这两个 .a，apps 的坑留到 bin 阶段再打。
    echo "[openssl] make libcrypto.a libssl.a（-j$NPROC，只编库）"
    if ! make -C "$OBJ" -j"$NPROC" libcrypto.a libssl.a >"$BASE/make_libs.log" 2>&1; then
      if grep -q 'No rule to make target' "$BASE/make_libs.log"; then
        echo "[openssl]   这份 Makefile 没有 libcrypto.a 目标，退回 build_libs（会带上 apps）"
        make -C "$OBJ" -j"$NPROC" build_libs >"$BASE/make_libs.log" 2>&1 || true
      fi
    fi
    tail -40 "$BASE/make_libs.log"
    if [ ! -f "$OBJ/libcrypto.a" ]; then
      echo
      echo "[openssl] ✗ 未产出 libcrypto.a —— 错误按句子归并（-j8 下同一类错会在几十个"
      echo "          文件里重复，这里只列一次 + 出现次数，方便一次看全）："
      sed -n -e 's/.*: fatal error: //p' -e 's/.*: error: //p' "$BASE/make_libs.log" \
        | sort | uniq -c | sort -rn | head -20 | sed 's/^/      /'
    fi
    echo
    echo "[openssl] 日志: $BASE/make_libs.log"
    for a in libcrypto.a libssl.a; do
      if [ -f "$OBJ/$a" ]; then
        cp "$OBJ/$a" "$OUT/$a"
        echo "[openssl] ✓ $a $(wc -c <"$OUT/$a") bytes"
      else
        ne="$(grep -c 'error:' "$BASE/make_libs.log" 2>/dev/null || true)"; ne="${ne:-0}"
        echo "[openssl] ✗ 未产出 $a（错误数 $ne）"
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
    # memset/memcpy + isatty）；mem.c 提供堆状态（malloc_head / slab_freelist），
    # 少了它就是 undefined reference —— libcrypto 处处 malloc，必然撞上。
    echo "[openssl] 编 libc 裸件（crt0 / syscall / string / mem）"
    "$CC" "${ARCHFLAGS[@]}" -std=gnu11 -O2 -ffreestanding -fno-builtin \
      -I"$ROOT/user/include" -c "$ROOT/user/lib/crt0_$ARCH.S"    -o "$BASE/_crt0.o"    || exit 1
    "$CC" "${ARCHFLAGS[@]}" -std=gnu11 -O2 -ffreestanding -fno-builtin \
      -I"$ROOT/user/include" -c "$ROOT/user/lib/syscall_$ARCH.S" -o "$BASE/_syscall.o" || exit 1
    "$CC" "${ARCHFLAGS[@]}" -std=gnu11 -O2 -ffreestanding -fno-builtin \
      -I"$ROOT/user/include" -c "$ROOT/user/lib/string.c"        -o "$BASE/_string.o"  || exit 1
    "$CC" "${ARCHFLAGS[@]}" -std=gnu11 -O2 -ffreestanding -fno-builtin \
      -I"$ROOT/user/include" -c "$ROOT/user/lib/mem.c"           -o "$BASE/_mem.o"     || exit 1

    LINK_FLAGS="-nostdlib -nostartfiles -static -no-pie -Wl,--build-id=none"
    LINK_FLAGS="$LINK_FLAGS $BASE/_crt0.o $BASE/_string.o $BASE/_mem.o $BASE/_syscall.o"
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
      if grep -q 'wait_for_async\|fd_set\|FD_SET' "$BASE/make_bin.log"; then
        echo
        echo "[openssl] apps.c 的 wait_for_async 被编进来了。它的 guard 和 sys/select.h 的"
        echo "          包含位置（internal/sockets.h 只给 AIX 包了 <sys/select.h>）："
        grep -n -B6 'int wait_for_async' "$SRC/apps/lib/apps.c" 2>/dev/null | sed 's/^/      /'
        grep -n 'sys/select.h' "$SRC/apps/lib/apps.c" 2>/dev/null | sed 's/^/      /'
      fi
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
