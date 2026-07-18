#!/usr/bin/env bash
# 用法: bash first_run.sh
set -euo pipefail

echo "==> [1/5] apt 依赖"
sudo apt update
sudo apt install -y build-essential git curl python3 xz-utils \
    qemu-system-misc gcc-riscv64-unknown-elf \
    e2fsprogs dosfstools
# 新版 Ubuntu 把 riscv 的 QEMU 拆成了独立包,misc 里没有就补
command -v qemu-system-riscv64 >/dev/null || \
    sudo apt install -y qemu-system-riscv || true
command -v qemu-system-riscv64 >/dev/null \
    || { echo "!! 找不到 qemu-system-riscv64,请手动排查"; exit 1; }

echo "==> [2/5] Rust"
if ! command -v rustup >/dev/null; then
    curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh -s -- -y
    source "$HOME/.cargo/env"
fi
rustup target add riscv64gc-unknown-none-elf
rustup target add loongarch64-unknown-none

echo "==> [3/5] loongarch64 交叉工具链"
TC_DIR=/opt/cross-tools
TC_BIN="$TC_DIR/bin/loongarch64-unknown-linux-gnu-gcc"
if [ -x "$TC_BIN" ]; then
    echo "    已存在,跳过: $TC_BIN"
else
    # 从 GitHub API 拿 loong64/cross-tools 最新 release 中
    # x86_64 宿主 + loongarch64-unknown-linux-gnu 目标的压缩包
    echo "    查询 loong64/cross-tools 最新 release..."
    URL="${TOOLCHAIN_URL:-$(curl -fsSL \
        https://api.github.com/repos/loong64/cross-tools/releases/latest \
        | grep -o 'https://[^"]*\.tar\.xz' \
        | grep 'loongarch64-unknown-linux-gnu' \
        | grep -i 'x86_64' \
        | head -1)}"
    [ -n "$URL" ] || { echo "!! 未找到工具链下载地址,可用 TOOLCHAIN_URL=... 手动指定"; exit 1; }
    echo "    下载: $URL"
    curl -fSL --progress-bar "$URL" -o /tmp/loongarch-tc.tar.xz
    sudo rm -rf "$TC_DIR" && sudo mkdir -p "$TC_DIR"
    # 剥掉顶层目录,但保持 bin/lib/libexec 的兄弟关系(否则会炸 cc1)
    sudo tar -xf /tmp/loongarch-tc.tar.xz -C "$TC_DIR" --strip-components=1
    rm /tmp/loongarch-tc.tar.xz
fi

echo "==> [4/5] PATH"
if ! grep -q '/opt/cross-tools/bin' ~/.bashrc; then
    echo 'export PATH=/opt/cross-tools/bin:$PATH' >> ~/.bashrc
fi
export PATH=/opt/cross-tools/bin:$PATH

echo "==> [5/5] 验收"
fail=0
check() { command -v "$1" >/dev/null && echo "  ok  $1" || { echo "  !!  $1 缺失"; fail=1; }; }
check qemu-system-riscv64
check qemu-system-loongarch64
check riscv64-unknown-elf-gcc
check loongarch64-unknown-linux-gnu-gcc
check loongarch64-unknown-linux-gnu-g++
check mkfs.ext4
check mkfs.fat
rustup target list --installed | grep -q riscv64gc-unknown-none-elf \
    && echo "  ok  rust target riscv64" || { echo "  !!  rust target riscv64"; fail=1; }
rustup target list --installed | grep -q loongarch64-unknown-none \
    && echo "  ok  rust target loongarch64" || { echo "  !!  rust target loongarch64"; fail=1; }
# cc1 结构检查(防止只搬了 bin 的经典事故)
loongarch64-unknown-linux-gnu-gcc -print-prog-name=cc1 | grep -q / \
    && echo "  ok  cc1 路径正常" || { echo "  !!  cc1 找不到,工具链目录结构不完整"; fail=1; }

[ "$fail" = 0 ] && echo && echo "==> 环境就绪! ./run.sh riscv64 跑起来" \
                || { echo "==> 有缺失项,见上"; exit 1; }