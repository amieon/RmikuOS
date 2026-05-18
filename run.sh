#!/bin/bash
set -e


ARCH="${1:-riscv64}" 
MODE="${2:-release}" 


case "$ARCH" in
    riscv64)
        TARGET="riscv64gc-unknown-none-elf"
        QEMU="qemu-system-riscv64"
        # 自动找 OpenSBI
        for p in \
            /usr/lib/riscv64-linux-gnu/opensbi/generic/fw_jump.bin \
            /usr/share/opensbi/lp64/generic/fw_jump.bin \
            /usr/share/qemu/opensbi-riscv64-generic-fw_jump.bin; do
            if [ -f "$p" ]; then
                OPENSBI="$p"
                break
            fi
        done
        if [ -z "$OPENSBI" ]; then
            echo "找不到 OpenSBI fw_jump.bin，请安装 opensbi"
            exit 1
        fi
        QEMU_ARGS="-machine virt -m 128M -smp 4 -nographic -bios $OPENSBI"
        ;;

    loongarch64)
        TARGET="loongarch64-unknown-none"
        QEMU="qemu-system-loongarch64"
        QEMU_ARGS="-machine virt -cpu la464 -m 128M -smp 4 -nographic"
        ;;

    *)
        echo "用法: $0 [riscv64|loongarch64] [release|debug]"
        exit 1
        ;;
esac

KERNEL="target/$TARGET/$MODE/RmikuOS"

# 编译
cargo build --target "$TARGET" --"$MODE"

echo ""
echo "=== 启动 QEMU ($ARCH, smp=4) ==="
echo ""

# 启动
$QEMU $QEMU_ARGS -kernel "$KERNEL"