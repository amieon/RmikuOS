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

build_loongarch_trampoline() {
  if [ ! -f trampoline.bin ] || [ trampoline.S -nt trampoline.bin ]; then
    echo "=== 编译 LoongArch trampoline ==="
    "$LA_GCC" -c -x assembler-with-cpp -o trampoline.o trampoline.S
    "$LA_OBJCOPY" -O binary -j .text.boot trampoline.o trampoline.bin
    rm -f trampoline.o
  fi
}

if [ "$ARCH" = "loongarch64" ]; then
  build_loongarch_trampoline
fi

if [ "$MODE" = "release" ]; then
  cargo build --target "$TARGET" --release
else
  cargo build --target "$TARGET"
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
      -s -S
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
      -smp 4
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
