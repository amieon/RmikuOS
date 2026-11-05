#!/usr/bin/env bash
set -euo pipefail

REBUILD=0
STAGES=""
_RUN_ARGS=()
for _a in "$@"; do
  case "$_a" in
    rebuild|force|--force|-f) REBUILD=1 ;;
    --stages=*) STAGES="${_a#--stages=}" ;;
    --stages)
      echo "用法: --stages=lib,bin（要一个值，别把 lib 当位置参数）" >&2
      exit 2
      ;;
    *) _RUN_ARGS+=("$_a") ;;
  esac
done
set -- ${_RUN_ARGS[@]+"${_RUN_ARGS[@]}"}
unset _RUN_ARGS _a

ARCH="${1:-riscv64}"
MODE="${2:-debug}"
NET="${3:-user}"

# 空 = 不传，build.py 用自己的默认（lib,bin）。非空就把笔误挡在这里：
# 打错一个字母（--stages=bn）如果被静默忽略，行为是"照常构建"，没人会发现。
case "$STAGES" in
  ""|lib|bin|lib,bin|bin,lib) ;;
  *)
    echo "错误: --stages=$STAGES 不认识（可选: lib,bin / bin,lib / bin / lib）" >&2
    exit 2
    ;;
esac



case "$ARCH" in
  riscv64)
    TARGET="riscv64gc-unknown-none-elf"
    QEMU="qemu-system-riscv64"
    ;;
  loongarch64)
    TARGET="loongarch64-unknown-none"
    QEMU="qemu-system-loongarch64"
    ;;
  *)
    echo "用法: $0 [riscv64|loongarch64] [release|debug]" >&2
    exit 1
    ;;
esac

case "$MODE" in
  debug|release) ;;
  *)
    echo "用法: $0 [riscv64|loongarch64] [release|debug]" >&2
    exit 1
    ;;
esac


case "$NET" in
  user|pair-a|pair-b) ;;
  *)
    echo "用法: $0 [riscv64|loongarch64] [release|debug] [user|pair-a|pair-b] [rebuild] [--stages=lib,bin]" >&2
    echo "  rebuild（旧名 force）: 清掉全部缓存重编；--stages 决定 openssl 跑哪些阶段" >&2
    exit 1
    ;;
esac

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# 网络后端: user=slirp(默认); pair-a/pair-b=两台直联互 ping
case "$NET" in
  user)
    case "$ARCH" in
    riscv64)
      NET_ARGS=(
        -netdev user,id=net0,hostfwd=tcp::8080-:8080,tftp="$SCRIPT_DIR/tftpboot"
        -device "virtio-net-pci,disable-legacy=on,netdev=net0,romfile="
        -object filter-dump,id=f1,netdev=net0,file=/tmp/rmiku.pcap
      )
      ;;
    loongarch64)
      NET_ARGS=(
        -netdev user,id=net0,hostfwd=tcp::8081-:8081,tftp="$SCRIPT_DIR/tftpboot"
        -device "virtio-net-pci,disable-legacy=on,netdev=net0,romfile="
        -object filter-dump,id=f1,netdev=net0,file=/tmp/rmiku.pcap
      )
      ;;
    esac
    ;;
  pair-a)
    NET_ARGS=(
      -netdev socket,id=net0,listen=:1234
      -device "virtio-net-pci,disable-legacy=on,netdev=net0,romfile=,mac=52:54:00:00:00:0A"
      -object filter-dump,id=f1,netdev=net0,file=/tmp/rmiku-a.pcap
    )
    ;;
  pair-b)
    NET_ARGS=(
      -netdev socket,id=net0,connect=127.0.0.1:1234
      -device "virtio-net-pci,disable-legacy=on,netdev=net0,romfile=,mac=52:54:00:00:00:0B"
      -object filter-dump,id=f1,netdev=net0,file=/tmp/rmiku-b.pcap
    )
    ;;
esac

LA_PREFIX="${LA_PREFIX:-/opt/cross-tools/bin/loongarch64-unknown-linux-gnu-}"
LA_GCC="${LA_GCC:-${LA_PREFIX}gcc}"
LA_OBJCOPY="${LA_OBJCOPY:-${LA_PREFIX}objcopy}"

USER_BUILD_SCRIPT="user/build.py"


build_user_apps() {
  # rebuild 盖过 SKIP_USER_BUILD：既然明确要求重编，就没有"跳过"的道理。
  if [ "${SKIP_USER_BUILD:-0}" = "1" ] && [ "$REBUILD" != "1" ]; then
    echo "=== 跳过用户程序构建: SKIP_USER_BUILD=1 ==="

    if [ ! -f "target/fs-${ARCH}.img" ]; then
      echo "错误: SKIP_USER_BUILD=1 但找不到 target/fs-${ARCH}.img" >&2
      exit 1
    fi

    return
  fi

  if [ ! -f "$USER_BUILD_SCRIPT" ]; then
    echo "错误: 找不到用户程序构建脚本: $USER_BUILD_SCRIPT" >&2
    exit 1
  fi

  if [ "$REBUILD" = "1" ]; then
    # build.py --clean 只清 user/build/<arch> 和缓存表；git 的 .o 落在**源码树**里
    # (user/git/src)，不在 build 目录下，得单独清。清了之后 libgit.a 里那批
    # 按 NO_CURL 编的旧 object 才会重新生成。
    if [ -d "user/git/src" ]; then
      echo "=== rebuild: 清理 git 目标文件（user/git/src/*.o, libgit.a）==="
      make -C user/git/src clean >/dev/null 2>&1 || true
      rm -f user/git/src/git user/git/src/git-remote-http
    fi
  fi

  echo "=== 构建用户程序 ($ARCH) ==="
  if [ "$REBUILD" = "1" ]; then
    echo "    rebuild: 忽略一切缓存（user/build/${ARCH}/ 已清空）"
    # STAGES 空 = 让 build.py 用默认（lib,bin）。显式传空串反而会覆盖成"没有阶段"。
    if [ -n "$STAGES" ]; then
      echo "    rebuild: vendored 大件阶段 = $STAGES"
      python3 "$USER_BUILD_SCRIPT" "$ARCH" --clean --stages="$STAGES"
    else
      python3 "$USER_BUILD_SCRIPT" "$ARCH" --clean
    fi
  else
    python3 "$USER_BUILD_SCRIPT" "$ARCH"
  fi

  echo "=== 构建 ext4 rootfs ($ARCH) ==="
  ./user/mkfs_ext4.sh "$ARCH"

  if [ ! -f "target/fs-${ARCH}.img" ]; then
    echo "错误: rootfs 构建后找不到 target/fs-${ARCH}.img" >&2
    exit 1
  fi

  # git 的 https 靠一个独立二进制 /bin/git-remote-https（remote-curl.o +
  # http.o + http-walker.o 单独链接），不是 /bin/git 的副本。它缺席时症状是
  # guest 里 `git: 'remote-https' is not a git command`，而 git 本体完全正常 ——
  # 这个"半成品"状态曾被 build.py 的缓存当成成功记下来，于是永远不再重试。
  # 所以在打包之后挡一道：缺了什么、错误在哪，一屏说完。
  local _git_out="user/build/${ARCH}/git"
  if [ -f "$_git_out/git.elf" ] && [ ! -f "$_git_out/git-remote-https.elf" ]; then
    echo
    echo "⚠️  git 编出来了，但 git-remote-https.elf 不在 —— https 传输不可用"
    echo "    guest 里会看到: git: 'remote-https' is not a git command"
    if [ -f "$_git_out/make_helper.log" ]; then
      echo "    make_helper.log 的错误行:"
      grep -E 'error:|undefined reference|No rule|Error [0-9]' "$_git_out/make_helper.log" 2>/dev/null \
        | head -15 | sed 's/^/      /' || true
    else
      echo "    连 make_helper.log 都没有 → helper 那一步根本没跑"
      echo "    先确认静态库在: ls user/build/${ARCH}/curl/libcurl.a user/build/${ARCH}/openssl/libssl.a"
    fi
    echo "    重试单步: bash scripts/git_probe.sh ${ARCH} bin"
    echo
  fi
}


build_user_apps

LOG_LEVEL="${LOG:-warn}"

# 内核缓存走 cargo：按文件内容哈希判定，不会出现"改了没生效"，所以 rebuild 默认
# 不清它（cargo clean 一次要几分钟，收益为零）。真要连内核一起推倒：
#   FORCE_KERNEL=1 ./run.sh riscv64 rebuild
if [ "$REBUILD" = "1" ] && [ "${FORCE_KERNEL:-0}" = "1" ]; then
  echo "=== rebuild: cargo clean（内核全量重编，target/$TARGET）==="
  cargo clean --target "$TARGET" || true
fi

echo "=== 编译内核 ($ARCH, $MODE, LOG=$LOG_LEVEL) ==="



if [ "$MODE" = "release" ]; then
  if [ "$NET" = "user" ]; then      
    LOG="$LOG_LEVEL" cargo build --target "$TARGET" --release
  else
    LOG="$LOG_LEVEL" cargo build --target "$TARGET" --release --features pair-net
  fi
else
  if [ "$NET" = "user" ]; then      
    LOG="$LOG_LEVEL" cargo build --target "$TARGET"
  else
    LOG="$LOG_LEVEL" cargo build --target "$TARGET" --features pair-net
  fi
fi

KERNEL_ELF="target/$TARGET/$MODE/RmikuOS"
if [ ! -f "$KERNEL_ELF" ]; then
  echo "错误: 找不到内核 ELF: $KERNEL_ELF" >&2
  exit 1
fi


TCG_THREAD="${TCG_THREAD:-multi}"
SMP_N="${SMP_N:-8}"

DATA_IMG="target/data-${ARCH}.img"
if [ ! -f "$DATA_IMG" ]; then
  echo "=== 创建可写数据盘 $DATA_IMG (待内核首次挂载时格式化) ==="
  truncate -s 64M "$DATA_IMG"
fi

case "$ARCH" in
  riscv64)
    QEMU_ARGS=(
      -machine virt
      -cpu rv64
      -accel "tcg,thread=$TCG_THREAD"
      -smp "$SMP_N,cores=$SMP_N,threads=1,sockets=1"
      -m 1G
      -nographic
      -kernel "$KERNEL_ELF"
      -drive "file=target/fs-riscv64.img,format=raw,if=none,id=blk0"
      -device "virtio-blk-device,drive=blk0"
      -drive "file=target/fat-riscv64.img,format=raw,if=none,id=blk1"
      -device "virtio-blk-device,drive=blk1"
      -drive "file=target/data-riscv64.img,format=raw,if=none,id=blk2"
      -device "virtio-blk-device,drive=blk2"
      # 熵源：走 PCI（不是 MMIO），因此不受 virtio-mmio 地址反序影响
      -device "virtio-rng-pci,disable-legacy=on"
      "${NET_ARGS[@]}"
    )
    ;;

  loongarch64)
    QEMU_ARGS=(
      -machine virt
      -cpu la464
      -m 2G
      -accel "tcg,thread=$TCG_THREAD"
      -smp "$SMP_N,cores=$SMP_N,threads=1,sockets=1"
      -nographic
      -kernel "$KERNEL_ELF"
      -drive "file=target/fs-loongarch64.img,format=raw,if=none,id=blk0"
      -device "virtio-blk-pci,drive=blk0,disable-legacy=on"
      -drive "file=target/fat-loongarch64.img,format=raw,if=none,id=blk1"
      -device "virtio-blk-pci,drive=blk1,disable-legacy=on"
      -drive "file=target/data-loongarch64.img,format=raw,if=none,id=blk2"
      -device "virtio-blk-pci,drive=blk2,disable-legacy=on"
      # 熵源：与 blk/net 同走 PCI
      -device "virtio-rng-pci,disable-legacy=on"
      "${NET_ARGS[@]}"
    )
    ;;
esac

echo
echo "=== 启动 QEMU ($ARCH, mode=$MODE, net=$NET) ==="
echo "KERNEL: $KERNEL_ELF"
echo

exec "$QEMU" "${QEMU_ARGS[@]}"