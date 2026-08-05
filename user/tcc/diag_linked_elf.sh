#!/bin/sh
# diag_linked_elf.sh — RmikuOS TCC LoongArch64: 宿主侧完整链接诊断 v3
#
# 在 Docker 宿主(有 loongarch64-unknown-linux-gnu-* 工具链)运行:
#     sh ~/RmikuOS/user/tcc/diag_linked_elf.sh [测试号, 默认 01]
#
# 关键: 通过 /usr/lib/tcc 符号链接让宿主 tcc 走 config.h 里写死的默认
#       路径, 这样链接顺序/布局与镜像内 tcc 完全一致, era 可直接对照。

TC=loongarch64-unknown-linux-gnu
RMOS=${RMOS:-$HOME/RmikuOS}
SYS=$RMOS/user/build/loongarch64/tcc-sys/usr/lib/tcc
TN=${1:-01}
SRC=$(ls $RMOS/user/rootfs/codes/t${TN}_*.c 2>/dev/null | head -1)

[ -z "$SRC" ] && { echo "找不到 t${TN}_*.c"; exit 1; }
[ -f "$SYS/crt1.o" ] || { echo "!! $SYS/crt1.o 不存在, 先跑完整构建"; exit 1; }

echo "=== [0] 建立 /usr/lib/tcc -> sysroot 符号链接(需 root, Docker 里通常就是) ==="
if [ ! -e /usr/lib/tcc/crt1.o ]; then
    ln -sfn "$SYS" /usr/lib/tcc 2>/dev/null || sudo ln -sfn "$SYS" /usr/lib/tcc
fi
ls -l /usr/lib/tcc/ | head -5

echo "=== [1] 宿主编译 tcc ==="
cd "$RMOS/user/tcc/tcc-src" || exit 1
gcc -O1 -DTCC_TARGET_LOONGARCH64 -DONE_SOURCE=0 -I. -I.. \
    -o /tmp/tcc-la tcc.c libtcc.c tccpp.c tccgen.c tccdbg.c tccelf.c tccasm.c tccrun.c \
    loongarch64-gen.c loongarch64-link.c loongarch64-asm.c -lm -ldl 2>&1 | grep -v warn_unused_result || true
[ -x /tmp/tcc-la ] || { echo "宿主 tcc 构建失败"; exit 1; }

echo "=== [2] 默认方式完整链接 $SRC (与镜像内布局一致) ==="
/tmp/tcc-la "$SRC" -o /tmp/t$TN.elf || exit 1

ENTRY=$($TC-readelf -hW /tmp/t$TN.elf | awk '/Entry point/{print $NF}')
echo "=== [3] 入口区域反汇编 (entry=$ENTRY) ==="
$TC-readelf -hW /tmp/t$TN.elf | grep -Ei 'entry|type:|machine'
$TC-objdump -d --start-address=$ENTRY --stop-address=$((ENTRY+80)) /tmp/t$TN.elf | tail -n +5

echo "=== [3b] 程序头 + 段表(got 相关) ==="
$TC-readelf -lW /tmp/t$TN.elf | grep -E 'LOAD|RELRO'
$TC-readelf -SW /tmp/t$TN.elf | grep -Ei '\.got|\.text|\.data|\.rodata|Name' | head -15

echo "=== [4] 重定位类型统计 + 数字类型取证 ==="
for f in "$SYS/crt1.o"; do
    echo "--- $f (含数字类型):"
    $TC-readelf -rW "$f" 2>/dev/null | grep 'R_LARCH' | head -8
done
echo "--- libtcc1.a 中 ADD6/SUB6/RELAX/ALIGN 的原始行(取 Info 列数字):"
$TC-readelf -rW "$SYS/libtcc1.a" 2>/dev/null | grep -E 'R_LARCH_(ADD6|SUB6|RELAX|ALIGN)' | head -6
echo "--- libc.a 统计:"
$TC-readelf -rW "$SYS/libc.a" 2>/dev/null | grep -o 'R_LARCH_[A-Z0-9_]*' | sort | uniq -c
echo "--- libtcc1.a 统计:"
$TC-readelf -rW "$SYS/libtcc1.a" 2>/dev/null | grep -o 'R_LARCH_[A-Z0-9_]*' | sort | uniq -c

echo "=== [5] QEMU trap era 在本 ELF 中的定位 ==="
$TC-objdump -d /tmp/t$TN.elf > /tmp/t$TN.dis
for a in 12974 1e13c 12a9c 12998 1ecfc 1dd5c 1347c 15990 127fc 17484 12ad4 1df5c; do
    if grep -qE "^\s*$a:" /tmp/t$TN.dis; then
        echo "--- era 0x$a 上下文:"
        grep -B10 -E "^\s*$a:" /tmp/t$TN.dis | tail -13
    fi
done

echo "=== [6] .got / .got.plt 内容(槽里应是符号地址, 不应为 0) ==="
$TC-objdump -s -j .got /tmp/t$TN.elf 2>/dev/null || echo "(无 .got)"
$TC-objdump -s -j .got.plt /tmp/t$TN.elf 2>/dev/null || true

echo "=== [7] _start 里 la.got 的实际计算结果核对 ==="
echo "(把 [3] 的 pcalau12i/ld.d 立即数与 [6] 的 .got 地址对照)"

echo "完成。请把 [3][3b][4][5][6] 全部输出贴回。"
