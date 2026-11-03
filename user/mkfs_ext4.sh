#!/usr/bin/env bash
set -euo pipefail

ARCH="${1:-riscv64}"

IMG="target/fs-${ARCH}.img"
ROOT="target/fsroot-${ARCH}"
OVERLAY="user/rootfs"
FAT_IMG="target/fat-${ARCH}.img"

mkdir -p target
rm -rf "$ROOT"

echo "=== 构建 ext4 rootfs ($ARCH) ==="

# 先复制用户自定义 rootfs 模板
if [ -d "$OVERLAY" ]; then
  echo "copy rootfs overlay: $OVERLAY -> $ROOT"
  mkdir -p "$ROOT"
  cp -a "$OVERLAY"/. "$ROOT"/
else
  echo "no $OVERLAY, create minimal rootfs"
  mkdir -p "$ROOT"
fi

# 确保基础目录存在
mkdir -p "$ROOT/programs" "$ROOT/bin" "$ROOT/etc" "$ROOT/home" "$ROOT/tmp" "$ROOT/sched" \
"$ROOT/dev" "$ROOT/proc" "$ROOT/tests" "$ROOT/samples" "$ROOT/fat" "$ROOT/gcn" "$ROOT/jvm" \
"$ROOT/usr/lib/tcc" "$ROOT/data" "$ROOT/var"

# TCC 系统文件: crt1/crti/crtn + libtcc1.a + libc.a + runmain.o + include(TCC 链接用户程序用)
if [ -d "user/build/${ARCH}/tcc-sys/usr/lib/tcc" ]; then
  cp -r user/build/${ARCH}/tcc-sys/usr/lib/tcc/* "$ROOT/usr/lib/tcc/"
fi

# 如果用户没有提供 motd，就生成默认 motd
if [ ! -f "$ROOT/etc/motd" ]; then
  cat > "$ROOT/etc/motd" <<EOF
Welcome to RmikuOS ext4 rootfs!
EOF
fi

# 把编译出来的用户程序放进 /bin
for f in user/build/${ARCH}/bin/*.elf; do
  [ -e "$f" ] || continue
  base="$(basename "$f" .elf)"
  clean="$(printf "%s" "$base" | sed -E 's/^([0-9]+_)+//')" 
  cp "$f" "$ROOT/bin/$clean"
done

# 把编译出来的程序放进 /samples
for f in user/build/${ARCH}/samples/*.elf; do
  [ -e "$f" ] || continue
  base="$(basename "$f" .elf)"          
  clean="$(printf "%s" "$base" | sed -E 's/^([0-9]+_)+//')"  
  cp "$f" "$ROOT/samples/$clean"
done

# 把编译出来的程序放进 /sched
for f in user/build/${ARCH}/sched/*.elf; do
  [ -e "$f" ] || continue
  base="$(basename "$f" .elf)"        
  clean="$(printf "%s" "$base" | sed -E 's/^([0-9]+_)+//')"  
  cp "$f" "$ROOT/sched/$clean"
done


# 把编译出来的测试程序放进 /tests
for f in user/build/${ARCH}/tests/*.elf; do
  [ -e "$f" ] || continue
  base="$(basename "$f" .elf)"          
  clean="$(printf "%s" "$base" | sed -E 's/^([0-9]+_)+//')"  
  cp "$f" "$ROOT/tests/$clean"
done

# 把 Rust 单文件/工作空间编译出来的程序放进 /programs
for f in user/build/${ARCH}/programs/*.elf; do
  [ -e "$f" ] || continue
  base="$(basename "$f" .elf)"
  cp "$f" "$ROOT/programs/$base"
done

#C 项目（单入口扁平化，多入口建子目录）
for proj_dir in user/build/${ARCH}/c/*; do
  [ -d "$proj_dir" ] || continue
  proj_name="$(basename "$proj_dir")"
  elf_count=$(ls "$proj_dir"/*.elf 2>/dev/null | wc -l)
  if [ "$elf_count" -eq 1 ]; then
    # 单入口：直接放 /programs/，和 Rust 一致
    cp "$proj_dir"/*.elf "$ROOT/programs/$proj_name"
    echo "  [c] $proj_name -> /programs/$proj_name"
  else
    # 多入口：放子目录
    mkdir -p "$ROOT/programs/$proj_name"
    for f in "$proj_dir"/*.elf; do
      [ -e "$f" ] || continue
      base="$(basename "$f" .elf)"
      cp "$f" "$ROOT/programs/$proj_name/$base"
    done
    echo "  [c multi] $proj_name -> /programs/$proj_name/"
  fi
done

# C++ 项目（单入口扁平化，多入口建子目录）
for proj_dir in user/build/${ARCH}/cpp/*; do
  [ -d "$proj_dir" ] || continue
  proj_name="$(basename "$proj_dir")"
  elf_count=$(ls "$proj_dir"/*.elf 2>/dev/null | wc -l)
  if [ "$elf_count" -eq 1 ]; then
    # 单入口：直接放 /programs/，和 Rust 一致
    cp "$proj_dir"/*.elf "$ROOT/programs/$proj_name"
    echo "  [cpp] $proj_name -> /programs/$proj_name"
  else
    # 多入口：放子目录
    mkdir -p "$ROOT/programs/$proj_name"
    for f in "$proj_dir"/*.elf; do
      [ -e "$f" ] || continue
      base="$(basename "$f" .elf)"
      cp "$f" "$ROOT/programs/$proj_name/$base"
    done
    echo "  [cpp multi] $proj_name -> /programs/$proj_name/"
  fi
done

# GCN（特殊目录 /gcn/）
if [ -d "user/build/${ARCH}/gcn" ]; then
  for f in user/build/${ARCH}/gcn/*.elf; do
    [ -e "$f" ] || continue
    base="$(basename "$f" .elf)"
    cp "$f" "$ROOT/gcn/$base"
  done
  echo "  [gcn] -> /gcn/"
fi

# SQLite3（专门构建目录 user/build/<arch>/sqlite3/）
# sqlite_test 是规范化回归测试 -> /tests/;其余(sqlite3 shell/探针)-> /programs/
if [ -d "user/build/${ARCH}/sqlite3" ]; then
  for f in user/build/${ARCH}/sqlite3/*.elf; do
    [ -e "$f" ] || continue
    base="$(basename "$f" .elf)"
    if [ "$base" = "sqlite_test" ]; then
      cp "$f" "$ROOT/tests/sqlite_test"
      echo "  [sqlite3] sqlite_test -> /tests/sqlite_test"
    else
      cp "$f" "$ROOT/programs/$base"
      echo "  [sqlite3] $base -> /programs/$base"
    fi
  done
fi

# zlib（专门构建目录 user/build/<arch>/zlib/）
# zlib_test 是移植正确性回归 -> /tests/。zlib 本体不单独出可执行文件,
# 它的 .o 会被 git 链接时复用(build_zlib 的输出目录里还留着 .o)。
if [ -d "user/build/${ARCH}/zlib" ]; then
  for f in user/build/${ARCH}/zlib/*.elf; do
    [ -e "$f" ] || continue
    base="$(basename "$f" .elf)"
    cp "$f" "$ROOT/tests/$base"
    echo "  [zlib] $base -> /tests/$base"
  done
fi

# git（专门构建目录 user/build/<arch>/git/）
# 产物固定 git.elf -> /bin/git：PATH 里有 /bin，guest 里直接 `git xxx`。
# 目录里还有一堆 .o 和 make_git.log，这里只挑 *.elf，不会把中间件灌进 rootfs。
if [ -d "user/build/${ARCH}/git" ]; then
  for f in user/build/${ARCH}/git/*.elf; do
    [ -e "$f" ] || continue
    base="$(basename "$f" .elf)"
    cp "$f" "$ROOT/bin/$base"
    echo "  [git] $base -> /bin/$base"
  done
fi

# git 的"独立命令"形态：file:// / git:// / ssh:// 传输时，客户端要反向 fork
# 一个服务端，命令名是 `git-receive-pack`(push) / `git-upload-pack`(fetch)。
# 注意它不是 `git receive-pack` —— connect.c 是拼出 `git-receive-pack '<path>'`
# 丢给 shell 做 PATH 查找，所以这两个名字必须各自有一个可执行文件。
# 为什么是 cp 而不是 ln：镜像走 mke2fs -d，它按文件逐个写 inode，不保留宿主上
# 的硬链接关系，ln 出来的两个目录项最后还是两份数据。既然都是两份，就别装。
if [ -x "$ROOT/bin/git" ]; then
  for alias in git-receive-pack git-upload-pack; do
    cp "$ROOT/bin/git" "$ROOT/bin/$alias"
    echo "  [git] git -> /bin/$alias"
  done
fi

# /bin/sh：git 的 SHELL_PATH 编死成它，且不能改成 /bin/shell。
# Makefile:2128 有 `SHELL = $(SHELL_PATH)` —— 这个变量同时是 make 自己的执行 shell，
# 所以它必须是宿主上也存在的路径（宿主只有 /bin/sh）。想让 guest 里有 sh，
# 就只能让镜像提供 /bin/sh，而不是让编译器去找 /bin/shell：
# 那样 make 会拿一个宿主上不存在的路径跑 recipe，首条 GIT-CFLAGS 就 `错误 127`，
# 而且一个 error: 都不出现（编译根本没开始），非常容易误判成"工具链坏了"。
# login 仍然 exec /bin/shell，两个名字都留着；别名理由同上（mke2fs -d 不保留硬链接）。
if [ -x "$ROOT/bin/shell" ]; then
  cp "$ROOT/bin/shell" "$ROOT/bin/sh"
  echo "  [shell] shell -> /bin/sh (git 的 SHELL_PATH)"
fi

# git 模板：init 时往 .git 里铺 hooks/*.sample、info/exclude、description。
# 源码里用 "目录--文件" 命名（如 hooks--pre-commit.sample），make -C templates
# 会生成 blt/ 下的真实布局，并替换里面的 @SHELL_PATH@ / @PERL_PATH@。
# 二进制里编死的模板目录是构建机的 $(HOME)/share/git-core/templates，所以内核给
# init 播了 GIT_TEMPLATE_DIR 指向这里（见 kernel/src/task/manager_wrapper.rs）。
GIT_TPL="user/git/src/templates"
if [ -d "$GIT_TPL" ]; then
  make -C "$GIT_TPL" all >/dev/null 2>&1 || true
  if [ -d "$GIT_TPL/blt" ]; then
    mkdir -p "$ROOT/usr/share/git-core/templates"
    cp -R "$GIT_TPL/blt/." "$ROOT/usr/share/git-core/templates/"
    echo "  [git] templates -> /usr/share/git-core/templates"
  else
    echo "  [git] ! templates 未生成（git init 会 warning，功能不受影响）"
  fi
fi

# OpenSSL（专门构建目录 user/build/<arch>/openssl/）
# 产物固定 openssl.elf -> /bin/openssl。同目录还有 libcrypto.a(8.9M)/libssl.a(1.4M)
# 和几千个 .o —— 这里只挑 *.elf。内核里没有"-l 链接期"，那两个 .a 对 guest 没用，
# 真要给 guest 里编译用再单独装（4M 的镜像不该背 10M 的静态库）。
#
# 运行时期待的三样，别处已经备好：
#   /dev/urandom  kernel/src/fs/urandom.rs（RAND 的 DEVRANDOM 第一项，唯一熵源）
#   /etc/ssl      OPENSSLDIR。没 openssl.cnf 时 OpenSSL 按"无配置"处理，不是错误
#   /etc/ssl/cert.pem  CA bundle（随 overlay 一起进来）。名字不能改：
#                 X509_get_default_cert_file() 硬编 OPENSSLDIR "/cert.pem"，
#                 Debian 那个 ca-certificates.crt 的文件名 OpenSSL 不会去找。
#   /etc/ssl/certs/    hash-dir 形，mkfs 从上面那份 bundle 拆出来（见 [ssl] hash-dir）。
#                 单文件 bundle 是"能用但慢"：X509_STORE 里塞 145 张，解析装载和
#                 建链候选扫描都要为它们付钱（哪一头为主尚未拆分）。两条路并存 ——
#                 `curl --cacert cert.pem` 走 bundle，`curl --capath /etc/ssl/certs` 走 hash-dir。
#   /bin/sh       PATH 里有就行（-engine / CA 相关子命令才真的 fork）
if [ -d "user/build/${ARCH}/openssl" ]; then
  for f in user/build/${ARCH}/openssl/*.elf; do
    [ -e "$f" ] || continue
    base="$(basename "$f" .elf)"
    cp "$f" "$ROOT/bin/$base"
    echo "  [openssl] $base -> /bin/$base"
  done
  mkdir -p "$ROOT/etc/ssl"
fi

# curl（专门构建目录 user/build/<arch>/curl/）
# 产物固定 curl.elf -> /bin/curl。同目录还有 libcurl.a（静态，guest 里用不上）。
#
# 运行时它依赖的两件事，openssl 那一段里已经备好了：
#   /etc/ssl/cert.pem  —— configure 时 --with-ca-bundle=/etc/ssl/cert.pem 写死
#   /bin/sh            —— curl 的 --engine 之类才 fork，普通请求不碰
# 注意 curl 的 CA 路径是**编译期常量**（CURL_CA_BUNDLE），所以这个文件和
# 构建时指的路径必须一致；临时换可以 export SSL_CERT_FILE=...
if [ -d "user/build/${ARCH}/curl" ]; then
  for f in user/build/${ARCH}/curl/*.elf; do
    [ -e "$f" ] || continue
    base="$(basename "$f" .elf)"
    cp "$f" "$ROOT/bin/$base"
    echo "  [curl] $base -> /bin/$base"
  done
fi

# CA bundle 兜底 + 构建期校验。
# 优先级：仓库 overlay > $CA_BUNDLE > 宿主机系统 bundle。
CA="$ROOT/etc/ssl/cert.pem"
if [ ! -s "$CA" ]; then
  mkdir -p "$ROOT/etc/ssl"
  cands=()
  [ -n "${CA_BUNDLE:-}" ] && cands+=("$CA_BUNDLE")
  # 各发行版的"单文件 PEM bundle"
  cands+=(
    /etc/ssl/certs/ca-certificates.crt
    /etc/pki/tls/certs/ca-bundle.crt
    /etc/pki/ca-trust/extracted/pem/tls-ca-bundle.pem
    /etc/ssl/ca-bundle.pem
    /etc/certs/ca-certificates.crt
    /etc/ssl/cert.pem
    /usr/local/etc/openssl/cert.pem
    /opt/homebrew/etc/openssl/cert.pem
  )
  # 再问 openssl 自己的 OPENSSLDIR —— 发行版布局差得离谱，硬编列表总会漏。
  if command -v openssl >/dev/null 2>&1; then
    _d="$(openssl version -d 2>/dev/null | sed -n 's/^OPENSSLDIR: "\(.*\)"$/\1/p')"
    if [ -n "$_d" ]; then
      cands+=("$_d/cert.pem" "$_d/certs/ca-certificates.crt")
    fi
  fi
  # Python 的 certifi：build.py 本来就依赖 python3，环境里顺手就有的概率不低。
  if command -v python3 >/dev/null 2>&1; then
    _p="$(python3 -c 'import certifi; print(certifi.where())' 2>/dev/null || true)"
    if [ -n "$_p" ]; then
      cands+=("$_p")
    fi
  fi
  for cand in "${cands[@]}"; do
    if [ -s "$cand" ]; then
      cp "$cand" "$CA"
      echo "  [ssl] overlay 没有 cert.pem，借用 $cand"
      break
    fi
  done
fi
if [ -s "$CA" ]; then
  echo "  [ssl] cert.pem $(wc -c <"$CA") bytes -> /etc/ssl/cert.pem"
else
  echo "  [ssl] ! 没有 CA bundle：guest 里 curl/openssl 会报 (77) error adding trust anchors" >&2
  echo "  [ssl]   放一份到 user/rootfs/etc/ssl/cert.pem，或用 CA_BUNDLE=<文件> 指定" >&2
fi

# 把 bundle 拆成 /etc/ssl/certs/<subject-hash>.<n> —— hash-dir 形。
#
# 为什么值得拆：`--cacert cert.pem` 把 145 张全塞进 X509_STORE，之后每次验证
# 都要为这 145 张付钱，至少两处：① 逐张 PEM 解码 + ASN.1 d2i + add_cert；
# ② X509_STORE_CTX_get1_issuer 线性扫 store->objs 做 X509_check_issued（store
# 本身没有 subject-hash 索引，哈希只在下面 by_dir 那条路上有）。
# hash-dir 把两头一起省掉：X509_LOOKUP_hash_dir 拿 issuer 的 subject hash 直接
# 开那一个文件，4 跳链只解析 4 张、也只扫 4 张。
#
# 懒加载这件事当初被怀疑过 —— 怕 OpenSSL 3.0 的 X509_STORE_load_path（curl 在
# HAVE_OPENSSL3 下用它，不是老的 load_locations）会 readdir 遍历整个目录，那样
# 拆了也白拆。实测否掉了，同一宿主同一轮、扣掉 `curl -k` 基线后的纯 CA 成本：
#
#     --cacert cert.pem（145 张）        3.41s
#     --capath /etc/ssl/certs（145 个）  0.47~0.56s   ← 三轮 0.466/0.488/0.561
#
# 7.6 倍。默认路径（CURL_CA_PATH 已编进 curl）整条 3.09~3.18s，基线 2.57~2.72s。
# 所以剩下的 ~0.5s 是 hash-dir 首次 open 两三个文件 + 解析根证书的固有成本，
# 不是"没拆干净"。

mkdir -p "$ROOT/etc/ssl/certs"
if [ -s "$CA" ] && command -v openssl >/dev/null 2>&1; then
  set +e
  CERTS="$ROOT/etc/ssl/certs"
  rm -rf "$CERTS"
  mkdir -p "$CERTS"
  _tmp="$(mktemp -d | tr '\\' '/')"
  awk -v d="$_tmp" '
    /-----BEGIN CERTIFICATE-----/ { n++; f = d "/c" n ".pem"; inblk = 1 }
    inblk { print > f }
    /-----END CERTIFICATE-----/ { inblk = 0; close(f) }
  ' "$CA"
  _n=0
  _dup=0
  _bad=0
  for f in "$_tmp"/c*.pem; do
    [ -s "$f" ] || continue
    h="$(openssl x509 -hash -noout -in "$f" 2>/dev/null)"
    if [ -z "$h" ]; then
      _bad=$((_bad + 1))
      continue
    fi
    i=0
    while [ -e "$CERTS/$h.$i" ]; do i=$((i + 1)); done
    if [ "$i" -gt 0 ]; then _dup=$((_dup + 1)); fi
    mv "$f" "$CERTS/$h.$i"
    _n=$((_n + 1))
  done
  rm -rf "$_tmp"
  if [ "$_n" -gt 0 ]; then
    echo "  [ssl] hash-dir: $_n 张 -> /etc/ssl/certs（$_dup 个 subject 撞名已顺延编号，$_bad 张解析失败已跳过）"
  else
    echo "  [ssl] ! hash-dir 拆分失败（cert.pem 仍未动，--cacert 可用，--capath 不可用）" >&2
  fi
  set -e
fi

# Java 字节码（JVM 项目）
for proj_dir in user/java/*; do
  [ -d "$proj_dir" ] || continue
  proj_name="$(basename "$proj_dir")"
  [ "$proj_name" = "rmiku" ] && continue   # 公共库目录，不单独打包
  class_count=$(ls "$proj_dir"/*.class 2>/dev/null | wc -l)
  if [ "$class_count" -eq 0 ]; then
    continue
  fi
  mkdir -p "$ROOT/jvm/$proj_name"
  for f in "$proj_dir"/*.class; do
    [ -e "$f" ] || continue
    cp "$f" "$ROOT/jvm/$proj_name/"
  done
  echo "  [jvm] $proj_name -> /jvm/$proj_name/"
done


# 简单展示 rootfs 内容
echo "rootfs content:"
find "$ROOT" -maxdepth 3 -print | sort

# 构建镜像(容量可通过 FS_SIZE_MB 覆盖;默认 96M——tests/samples/用户程序
# 全量打进 rootfs 后 32M 已不够,loongarch64 ELF 更大更容易爆盘)
rm -f "$IMG"
FS_SIZE_MB="${FS_SIZE_MB:-96}"

# 容量预检。`mkfs.ext4 -d` 塞不下时报的是 "No space left on device"，
# 完全看不出是"镜像给小了"还是"某个文件坏了"，而且它在几十行 find 输出之后
# 才炸。这里先量一遍：超过 88% 提醒（ext4 自身还要吃元数据），真的塞不下就
# 提前给出该调哪个变量。
ROOT_MB="$(du -sm "$ROOT" | cut -f1)"
echo "rootfs 用量: ${ROOT_MB}M / ${FS_SIZE_MB}M"
if [ "$ROOT_MB" -ge "$FS_SIZE_MB" ]; then
  echo "✗ rootfs 内容 ${ROOT_MB}M 已经超过镜像容量 ${FS_SIZE_MB}M" >&2
  echo "  调大: FS_SIZE_MB=$(( (ROOT_MB / 16 + 2) * 16 )) ./user/mkfs_ext4.sh $ARCH" >&2
  exit 1
elif [ "$((ROOT_MB * 100 / FS_SIZE_MB))" -ge 88 ]; then
  echo "! rootfs 已用 ${ROOT_MB}M / ${FS_SIZE_MB}M（≥88%，ext4 元数据可能不够）" >&2
fi

truncate -s "${FS_SIZE_MB}M" "$IMG"

# -L 卷标是"约定式身份"：discover_disks 按超级块 s_volume_name 识别 rootfs
mkfs.ext4 -q -F -L RMikuOS-ROOT -d "$ROOT" "$IMG"
echo "created $IMG (label=RMikuOS-ROOT)"

if [ ! -f "$FAT_IMG" ]; then
  echo "=== 构建 FAT 镜像 ($ARCH) ==="
  truncate -s 256M "$FAT_IMG"
  mkfs.fat -F 32 "$FAT_IMG"
  echo "created $FAT_IMG (fresh FAT32)"
else
  echo "FAT image exists, reuse: $FAT_IMG"
fi