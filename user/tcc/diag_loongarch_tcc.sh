#!/usr/bin/env bash
# diag_loongarch_tcc.sh —— 诊断 LoongArch64 下 libtcc1.a 软浮点符号缺失问题
#
# 在构建机(Docker 容器)里、仓库根目录下运行:
#     bash user/tcc/diag_loongarch_tcc.sh
#
# 目的:确认 __extenddftf2/__addtf3 等 __*tf* 符号在以下三处的存在性:
#   1) 交叉工具链的 libgcc.a(合并来源)
#   2) 构建产物 libtcc1.a(合并结果)
#   3) libc.a 中谁引用了 __extenddftf2(引用来源)
set -u

P="${LA_PREFIX:-/opt/cross-tools/bin/loongarch64-unknown-linux-gnu-}"
GCC="${P}gcc"; NM="${P}nm"; AR="${P}ar"

echo "==================================================="
echo "== 0) 工具链"
echo "==================================================="
if ! command -v "$GCC" >/dev/null 2>&1; then
    echo "找不到 $GCC"
    echo "请设置 LA_PREFIX, 例如: export LA_PREFIX=/opt/cross-tools/bin/loongarch64-unknown-linux-gnu-"
    exit 1
fi
"$GCC" --version | head -1

LIBGCC=$("$GCC" -print-libgcc-file-name)
echo "libgcc: $LIBGCC"
[ -f "$LIBGCC" ] || { echo "!! libgcc.a 不存在"; exit 1; }

echo
echo "==================================================="
echo "== 1) libgcc.a 里的 quad 软浮点符号(合并来源)"
echo "==================================================="
TF_IN_LIBGCC=$("$NM" --defined-only "$LIBGCC" 2>/dev/null | grep -cE " T (__addtf3|__subtf3|__multf3|__divtf3|__extenddftf2|__trunctfdf2|__gttf2|__lttf2|__eqtf2|__netf2|__getf2|__letf2|__extendsftf2|__trunctfsf2)$" || true)
echo "tf 符号数: $TF_IN_LIBGCC"
"$NM" --defined-only "$LIBGCC" 2>/dev/null | grep -E " T __(addtf3|extenddftf2|multf3|divtf3|gttf2|lttf2|trunctfdf2)$" | sort -u
if [ "$TF_IN_LIBGCC" = "0" ]; then
    echo "!! libgcc.a 里没有 quad 软浮点 -> cross-tools 工具链被裁剪过"
    echo "   备选方案: 从 LLVM compiler-rt 的 builtins(addtf3.c/extenddftf2.c 等)"
    echo "   摘出 tf 软浮点源码编进 libtcc1.a"
fi

echo
echo "== 1b) libgcc.a 里的 __clear_cache(tcc -run 需要)"
"$NM" --defined-only "$LIBGCC" 2>/dev/null | grep -i clear_cache || echo "(无 -> -run JIT 模式还需要另外解决)"

echo
echo "==================================================="
echo "== 2) 构建产物 libtcc1.a(合并结果)"
echo "==================================================="
LIBTCC1=user/build/loongarch64/tcc-sys/usr/lib/tcc/libtcc1.a
if [ -f "$LIBTCC1" ]; then
    echo "成员数: $("$AR" t "$LIBTCC1" | wc -l)"
    TF_IN_LIBTCC1=$("$NM" --defined-only "$LIBTCC1" 2>/dev/null | grep -cE " T (__addtf3|__subtf3|__multf3|__divtf3|__extenddftf2|__trunctfdf2|__gttf2|__lttf2)$" || true)
    echo "tf 符号数: $TF_IN_LIBTCC1"
    "$NM" --defined-only "$LIBTCC1" 2>/dev/null | grep -E " T __(addtf3|extenddftf2|multf3|divtf3)$" | sort -u
    if [ "$TF_IN_LIBGCC" != "0" ] && [ "$TF_IN_LIBTCC1" = "0" ]; then
        echo "!! libgcc 有但 libtcc1.a 没有 -> _tcc_merge_libgcc 未生效,查构建日志里"
        echo "   是否有 'libgcc merged into libtcc1.a: N objects' 这行"
    fi
else
    echo "不存在: $LIBTCC1 (先完整构建一次: ./run.sh loongarch64)"
fi

echo
echo "==================================================="
echo "== 3) libc.a 中谁引用了 __extenddftf2(引用来源)"
echo "==================================================="
LIBC=user/build/loongarch64/tcc-sys/usr/lib/tcc/libc.a
if [ -f "$LIBC" ]; then
    "$NM" -u "$LIBC" 2>/dev/null | grep -B8 "extenddftf2" || echo "(libc.a 中无人引用 __extenddftf2)"
else
    echo "不存在: $LIBC"
fi

echo
echo "==================================================="
echo "== 4) t01 编译产物反汇编(验证代码生成修复)"
echo "==================================================="
echo "在 RmikuOS 内跑: tcc /codes/t01_basics.c -o /tmp/t01"
echo "若能成功链接但运行仍崩, 用宿主 objdump 对比分析:"
echo "  ${P}objdump -d /tmp/t01 | head -40"
echo "(/tmp 若在 ext4 上, 可用 debugfs 从 target/fs-loongarch64.img 抠出)"
echo
echo "把以上输出全部贴回来即可。"
