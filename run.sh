#!/bin/bash
set -e

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
        echo "用法: $0 [riscv64|loongarch64] [release|debug]"
        exit 1
        ;;
esac

# === 编译 trampoline（仅 loongarch64）===
if [ "$ARCH" = "loongarch64" ]; then
    if [ ! -f "trampoline.bin" ] || [ "trampoline.S" -nt "trampoline.bin" ] || [ "trampoline.ld" -nt "trampoline.bin" ]; then
        echo "=== 编译 trampoline ==="
        /opt/cross-tools/bin/loongarch64-unknown-linux-gnu-gcc \
            -nostdlib -Wl,-Ttrampoline.ld -Wl,--build-id=none \
            -o trampoline.elf trampoline.S
        /opt/cross-tools/bin/loongarch64-unknown-linux-gnu-objcopy \
            -O binary trampoline.elf trampoline.bin
    fi
fi

# === 编译内核 ===
if [ "$MODE" = "release" ]; then
    cargo build --target "$TARGET" --release
else
    cargo build --target "$TARGET"
fi

KERNEL_ELF="target/$TARGET/$MODE/RmikuOS"

if [ ! -f "$KERNEL_ELF" ]; then
    echo "错误: 找不到内核 ELF: $KERNEL_ELF"
    exit 1
fi

case "$ARCH" in
    riscv64)
        QEMU_ARGS="-machine virt -cpu rv64 -m 128M -nographic -s -S -kernel $KERNEL_ELF"
        ;;
    loongarch64)
        # 转成 flat binary，确保 _entry 在文件开头（0x1000000）
        KERNEL_BIN="${KERNEL_ELF}.bin"
        /opt/cross-tools/bin/loongarch64-unknown-linux-gnu-objcopy \
            -O binary "$KERNEL_ELF" "$KERNEL_BIN"

        # -bios trampoline: 复位后执行 trampoline，跳到 0x1000000
        # -device loader: 把 binary 加载到物理 0x1000000
        QEMU_ARGS="-machine virt -cpu la464 -m 2G -smp 4 -nographic \
            -bios trampoline.bin \
            -device loader,file=$KERNEL_BIN,addr=0x1000000"
        ;;
esac

echo ""
echo "=== 启动 QEMU ($ARCH, mode=$MODE) ==="
echo "KERNEL: $KERNEL_ELF"
echo ""

$QEMU $QEMU_ARGS