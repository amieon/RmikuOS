#!/usr/bin/env bash
set -euo pipefail

ARCH="${1:-riscv64}"
MODE="${2:-debug}"

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

LA_PREFIX="${LA_PREFIX:-/opt/cross-tools/bin/loongarch64-unknown-linux-gnu-}"
LA_GCC="${LA_GCC:-${LA_PREFIX}gcc}"
LA_OBJCOPY="${LA_OBJCOPY:-${LA_PREFIX}objcopy}"

USER_BUILD_SCRIPT="user/build.py"
GENERATED_LOADER="kernel/src/loader/generated.rs"

build_user_apps() {
  if [ "${SKIP_USER_BUILD:-0}" = "1" ]; then
    echo "=== 跳过用户程序构建: SKIP_USER_BUILD=1 ==="
    return
  fi

  if [ ! -f "$USER_BUILD_SCRIPT" ]; then
    echo "错误: 找不到用户程序构建脚本: $USER_BUILD_SCRIPT" >&2
    exit 1
  fi

  echo "=== 构建用户程序 ($ARCH) ==="
  python3 "$USER_BUILD_SCRIPT" "$ARCH"
  ./user/mkfs_ext4.sh "$ARCH"

  if [ ! -f "$GENERATED_LOADER" ]; then
    echo "错误: 用户程序构建后仍找不到: $GENERATED_LOADER" >&2
    exit 1
  fi

  if ! grep -q "arch = $ARCH" "$GENERATED_LOADER"; then
    echo "错误: $GENERATED_LOADER 不是为当前架构 $ARCH 生成的" >&2
    echo "请检查 user/build.py 是否写入了 // arch = $ARCH" >&2
    exit 1
  fi
}

build_loongarch_trampoline() {
  if [ ! -f trampoline.bin ] || [ trampoline.S -nt trampoline.bin ]; then
    echo "=== 编译 LoongArch trampoline ==="
    "$LA_GCC" -c -x assembler-with-cpp -o trampoline.o trampoline.S
    "$LA_OBJCOPY" -O binary -j .text.boot trampoline.o trampoline.bin
    rm -f trampoline.o
  fi
}

build_user_apps

if [ "$ARCH" = "loongarch64" ]; then
  build_loongarch_trampoline
fi

LOG_LEVEL="${LOG:-warn}"

echo "=== 编译内核 ($ARCH, $MODE, LOG=$LOG_LEVEL) ==="

if [ "$MODE" = "release" ]; then
  LOG="$LOG_LEVEL" cargo build --target "$TARGET" --release
else
  LOG="$LOG_LEVEL" cargo build --target "$TARGET"
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
      -m 128M
      -nographic
      -kernel "$KERNEL_ELF"
    )
    ;;
  loongarch64)
    KERNEL_BIN="${KERNEL_ELF}.bin"
    "$LA_OBJCOPY" -O binary "$KERNEL_ELF" "$KERNEL_BIN"

    QEMU_ARGS=(
      -machine virt
      -cpu la464
      -m 2G
      -smp 1
      -nographic
      -bios trampoline.bin
      -device "loader,file=$KERNEL_BIN,addr=0x1000000"
    )
    ;;
esac

echo
echo "=== 启动 QEMU ($ARCH, mode=$MODE) ==="
echo "KERNEL: $KERNEL_ELF"
echo

exec "$QEMU" "${QEMU_ARGS[@]}"