# Dockerfile —— RmikuOS 构建与运行环境
FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive \
    RUSTUP_HOME=/opt/rustup \
    CARGO_HOME=/opt/cargo \
    PATH=/opt/cross-tools/bin:/opt/cargo/bin:$PATH

# 1. 系统依赖(qemu 双架构 + riscv 交叉 gcc + 镜像工具 + python)
RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential git curl python3 xz-utils ca-certificates \
        qemu-system-misc gcc-riscv64-unknown-elf \
        e2fsprogs dosfstools \
    # 新版拆分包里补 riscv qemu;有的版本在 misc 里,装不上就跳过
    && (apt-get install -y qemu-system-riscv || true) \
    && rm -rf /var/lib/apt/lists/*

# 2. Rust(stable 即可,内核无 nightly 特性)
RUN curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs \
        | sh -s -- -y --no-modify-path --default-toolchain stable \
    && rustup target add riscv64gc-unknown-none-elf \
    && rustup target add loongarch64-unknown-none

# 3. loongarch64 交叉工具链(GitHub API 找最新 release,x86_64 宿主)
RUN URL=$(curl -fsSL https://api.github.com/repos/loong64/cross-tools/releases/latest \
        | grep -o 'https://[^"]*\.tar\.xz' \
        | grep 'loongarch64-unknown-linux-gnu' \
        | grep -i 'x86_64' | head -1) \
    && test -n "$URL" \
    && curl -fSL "$URL" -o /tmp/tc.tar.xz \
    && mkdir -p /opt/cross-tools \
    && tar -xf /tmp/tc.tar.xz -C /opt/cross-tools --strip-components=1 \
    && rm /tmp/tc.tar.xz \
    # 顺手验证 cc1 结构,装错了在 build 阶段就失败,不留给运行时
    && loongarch64-unknown-linux-gnu-gcc -print-prog-name=cc1 | grep -q /

WORKDIR /work
CMD ["bash"]