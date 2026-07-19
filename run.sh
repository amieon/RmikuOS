#!/usr/bin/env bash
set -euo pipefail

cargo clean


ARCH="${1:-riscv64}"
MODE="${2:-debug}"
NET="${3:-user}"



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
    echo "用法: $0 [riscv64|loongarch64] [release|debug] [user|pair-a|pair-b]" >&2
    exit 1
    ;;
esac

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# 网络后端: user=slirp(默认); pair-a/pair-b=两台直联互 ping
case "$NET" in
  user)
    NET_ARGS=(
      -netdev user,id=net0,hostfwd=tcp::8080-:8080,tftp="$SCRIPT_DIR/tftpboot"
      -device "virtio-net-pci,disable-legacy=on,netdev=net0,romfile="
      -object filter-dump,id=f1,netdev=net0,file=/tmp/rmiku.pcap
    )
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
  if [ "${SKIP_USER_BUILD:-0}" = "1" ]; then
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

  echo "=== 构建用户程序 ($ARCH) ==="
  python3 "$USER_BUILD_SCRIPT" "$ARCH"

  echo "=== 构建 ext4 rootfs ($ARCH) ==="
  ./user/mkfs_ext4.sh "$ARCH"

  if [ ! -f "target/fs-${ARCH}.img" ]; then
    echo "错误: rootfs 构建后找不到 target/fs-${ARCH}.img" >&2
    exit 1
  fi
}


build_user_apps

LOG_LEVEL="${LOG:-warn}"

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

case "$ARCH" in
  riscv64)
    QEMU_ARGS=(
      -machine virt
      -cpu rv64
      -accel tcg,thread=multi
      -smp 8,cores=8,threads=1,sockets=1
      -m 1G
      -nographic
      -kernel "$KERNEL_ELF"
      -drive "file=target/fs-riscv64.img,format=raw,if=none,id=blk0"
      -device "virtio-blk-device,drive=blk0"
      -drive "file=target/fat-riscv64.img,format=raw,if=none,id=blk1"
      -device "virtio-blk-device,drive=blk1"
      "${NET_ARGS[@]}"
    )
    ;;

  loongarch64)
    QEMU_ARGS=(
      -machine virt
      -cpu la464
      -m 2G
      -accel tcg,thread=multi
      -smp 8,cores=8,threads=1,sockets=1
      -nographic
      -kernel "$KERNEL_ELF"
      -drive "file=target/fs-loongarch64.img,format=raw,if=none,id=blk0"
      -device "virtio-blk-pci,drive=blk0,disable-legacy=on"
      -drive "file=target/fat-loongarch64.img,format=raw,if=none,id=blk1"
      -device "virtio-blk-pci,drive=blk1,disable-legacy=on"
      "${NET_ARGS[@]}"
    )
    ;;
esac

echo
echo "=== 启动 QEMU ($ARCH, mode=$MODE, net=$NET) ==="
echo "KERNEL: $KERNEL_ELF"
echo

exec "$QEMU" "${QEMU_ARGS[@]}"