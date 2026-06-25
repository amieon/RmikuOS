#!/usr/bin/env python3

from pathlib import Path
import shutil
import argparse
import subprocess
import sys
import os
import re


ROOT = Path(__file__).resolve().parent.parent
USER_DIR = ROOT / "user"
SRC_DIR = USER_DIR / "src"
TESTS_DIR = USER_DIR / "tests"
BUILD_DIR = USER_DIR / "build"
INCLUDE_DIR = USER_DIR / "include"
LIB_DIR = USER_DIR / "lib"

# cargo workspace 所在目录(大工程 Rust,依赖 ulib)
RUST_DIR = USER_DIR / "rust"

GENERATED_RS = ROOT / "kernel" / "src" / "loader" / "generated.rs"

ARCH_CONFIG = {
    "riscv64": {
        "gcc": "riscv64-unknown-elf-gcc",
        "objcopy": "riscv64-unknown-elf-objcopy",
        "objdump": "riscv64-unknown-elf-objdump",
        "linker": USER_DIR / "linker-riscv64.ld",
        "runtime": LIB_DIR / "syscall_riscv64.S",
        "crt0": LIB_DIR / "crt0_riscv64.S",
        "rust_target": "riscv64gc-unknown-none-elf",
        # Rust 链接附加参数:riscv 走 rust-lld 直链,无需禁用 crt1/libc。
        "rust_link_args": [],
        "cflags": [
            "-march=rv64gc",
            "-mabi=lp64",
            "-mcmodel=medany",
            "-mno-relax",
            "-msmall-data-limit=0",
            "-DUSER_ARCH_RISCV64",
        ],
        "ldflags": [
            "-Wl,--no-relax",
        ],
    },
    "loongarch64": {
        "gcc": "loongarch64-unknown-linux-gnu-gcc",
        "objcopy": "loongarch64-unknown-linux-gnu-objcopy",
        "objdump": "loongarch64-unknown-linux-gnu-objdump",
        "linker": USER_DIR / "linker-loongarch64.ld",
        "crt0": LIB_DIR / "crt0_loongarch64.S",
        "runtime": LIB_DIR / "syscall_loongarch64.S",
        "rust_target": "loongarch64-unknown-none",
        # loongarch 经 gcc 链接,gcc 默认带 crt1.o 与 libc,会与 no_std 的
        # 自定义 _start 冲突,需禁用标准启动文件与标准库。
        "rust_link_args": ["-nostartfiles", "-nostdlib"],
        "cflags": [
            "-DUSER_ARCH_LOONGARCH64",
            "-G0",
            "-mno-relax",
        ],
        "ldflags": [
            "-Wl,--no-relax",
        ],
    },
}


def run(cmd):
    print("+", " ".join(str(x) for x in cmd))
    subprocess.run(cmd, check=True)


def run_env(cmd, cwd=None, env=None):
    """带 cwd / env 的 run(供 cargo 调用使用)。"""
    print("+", " ".join(str(x) for x in cmd))
    subprocess.run(cmd, check=True, cwd=cwd, env=env)


def sanitize_name(path: Path) -> str:
    name = path.stem
    name = re.sub(r"[^A-Za-z0-9_]", "_", name)
    if name and name[0].isdigit():
        name = "APP_" + name
    return name.upper()


def rust_bytes(data: bytes, indent: str = "    ") -> str:
    lines = []
    for i in range(0, len(data), 12):
        chunk = data[i:i + 12]
        line = indent + ", ".join(f"0x{b:02x}" for b in chunk) + ","
        lines.append(line)
    return "\n".join(lines)


def collect_sources():
    """扫描单文件源:.S / .c(C 程序)与 .rs(自包含单文件 Rust,不依赖 ulib)。

    依赖 ulib 的大工程 Rust 不在此处,它们由 cargo workspace
    (user/rust/programs/*)统一构建,见 build_rust_workspace。
    """
    sources = []
    for ext in ("*.S", "*.c", "*.rs"):
        for p in SRC_DIR.glob(ext):
            sources.append((p, "system"))    # src → 系统程序
        for p in TESTS_DIR.glob(ext):
            sources.append((p, "test"))       # tests → 测试程序
    sources.sort(key=lambda x: x[0].name)
    return sources


def build_one(arch: str, source: Path, app_id: int, category):
    if source.suffix == ".rs":
        return build_rust(arch, source, app_id, category)

    cfg = ARCH_CONFIG[arch]

    if category == "system":
        out_dir = BUILD_DIR / arch / "bin"
    else:
        out_dir = BUILD_DIR / arch / "tests"
    out_dir.mkdir(parents=True, exist_ok=True)

    stem = source.stem
    obj = out_dir / f"{app_id}_{stem}.o"
    crt0_obj = out_dir / f"{app_id}_{stem}_crt0.o"
    runtime_obj = out_dir / f"{app_id}_{stem}_runtime.o"
    elf = out_dir / f"{app_id}_{stem}.elf"
    bin_path = out_dir / f"{app_id}_{stem}.bin"

    common_flags = [
        "-ffreestanding",
        "-fno-builtin",
        "-fno-stack-protector",
        "-fno-pic",
        "-fno-pie",
        "-nostdlib",
        "-nostartfiles",
        "-static",
        "-I", str(INCLUDE_DIR),
    ]
    run([
        cfg["gcc"],
        *cfg["cflags"],
        *common_flags,
        "-c",
        str(cfg["crt0"]),
        "-o",
        str(crt0_obj),
    ])

    compile_cmd = [
        cfg["gcc"],
        *cfg["cflags"],
        *common_flags,
        "-c",
        str(source),
        "-o",
        str(obj),
    ]
    run(compile_cmd)

    runtime_cmd = [
        cfg["gcc"],
        *cfg["cflags"],
        *common_flags,
        "-c",
        str(cfg["runtime"]),
        "-o",
        str(runtime_obj),
    ]
    run(runtime_cmd)

    link_objects = []

    if source.suffix == ".c":
        link_objects.append(str(crt0_obj))

    link_objects.append(str(obj))

    if source.suffix == ".c":
        link_objects.append(str(runtime_obj))
    link_cmd = [
        cfg["gcc"],
        *cfg["cflags"],
        "-nostdlib",
        "-nostartfiles",
        "-static",
        "-no-pie",
        "-Wl,--build-id=none",
        *cfg["ldflags"],
        "-T", str(cfg["linker"]),
        *link_objects,
        "-o",
        str(elf),
    ]
    run(link_cmd)

    objcopy_cmd = [
        cfg["objcopy"],
        "-O", "binary",
        "-j", ".text",
        str(elf),
        str(bin_path),
    ]
    run(objcopy_cmd)

    data = bin_path.read_bytes()
    if not data:
        raise RuntimeError(f"{source} produced empty binary")

    print(f"[user] built app{app_id}: {source.name}, {len(data)} bytes")
    return {
        "id": app_id,
        "source": source,
        "name": source.stem,
        "symbol": sanitize_name(source),
        "bin": bin_path,
        "data": data,
    }


def build_rust(arch: str, source: Path, app_id: int, category):
    """编译「自包含单文件 Rust」(不依赖 ulib,自己写 syscall + _start)。

    用 rustc 直接编单个 .rs。按架构追加链接参数:
      - riscv64:rust-lld 直链,无需额外参数
      - loongarch64:经 gcc 链接,需 -nostartfiles -nostdlib 禁用 crt1/libc
    """
    cfg = ARCH_CONFIG[arch]
    out_dir = BUILD_DIR / arch / ("bin" if category == "system" else "tests")
    out_dir.mkdir(parents=True, exist_ok=True)
    stem = source.stem
    elf = out_dir / f"{app_id}_{stem}.elf"

    cmd = [
        "rustc",
        "--target", cfg["rust_target"],
        "-C", "panic=abort",
        "-C", "relocation-model=static",
        "-C", f"link-arg=-T{cfg['linker']}",
        # 不加 rust_link_args —— 单文件 rustc 用 rust-lld 直链,
        # lld 不带 crt1/libc,也不认 -nostartfiles。
        "-o", str(elf), str(source),
    ]
    run(cmd)

    print(f"[user] built rust(single) app{app_id}: {source.name}")
    return {
        "id": app_id,
        "source": source,
        "name": stem,
        "category": category,
    }


def build_rust_workspace(arch: str):
    """编译「cargo workspace Rust」(大工程,依赖 ulib)。

    用 cargo 一次性构建 user/rust 下整个 workspace,产物拷入
    build/<arch>/programs/,随后由 mkfs 装进镜像 /programs。

    用 RUSTFLAGS 环境变量传链接参数,彻底覆盖(而非追加)各级
    .cargo/config.toml 的 rustflags —— 避免内核 config 经 cargo
    层叠继承污染用户程序构建(两者共用 loongarch64-unknown-none target)。
    """
    if not RUST_DIR.exists():
        print(f"[user] no rust workspace at {RUST_DIR}, skip")
        return

    cfg = ARCH_CONFIG[arch]
    rust_target = cfg["rust_target"]
    linker = cfg["linker"].resolve()   # 绝对路径,避免相对路径在 cargo cwd 下失效

    flags = [
        "-C", "relocation-model=static",
        "-C", f"link-arg=-T{linker}",
    ]
    for la in cfg.get("rust_link_args", []):
        flags += ["-C", f"link-arg={la}"]

    env = os.environ.copy()
    env["RUSTFLAGS"] = " ".join(flags)

    print(f"[user] building rust workspace ({arch}) ...")
    run_env(
        ["cargo", "build", "--release", "--target", rust_target],
        cwd=RUST_DIR,
        env=env,
    )

    # 收集产物:release/ 下每个 program crate 的可执行 ELF
    rust_out = RUST_DIR / "target" / rust_target / "release"
    dst_dir = BUILD_DIR / arch / "programs"
    dst_dir.mkdir(parents=True, exist_ok=True)

    programs_dir = RUST_DIR / "programs"
    if not programs_dir.exists():
        print(f"[user] no rust programs dir, skip copy")
        return

    count = 0
    for prog_dir in sorted(programs_dir.iterdir()):
        if not prog_dir.is_dir():
            continue
        name = prog_dir.name              # crate 名 = 程序名
        elf = rust_out / name
        if elf.exists():
            shutil.copy(elf, dst_dir / f"{name}.elf")
            print(f"[user] rust workspace program -> {name}")
            count += 1
        else:
            print(f"[user] warning: expected rust program not found: {elf}")

    print(f"[user] rust workspace: {count} program(s) built")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("arch", choices=ARCH_CONFIG.keys())
    parser.add_argument("--objdump", action="store_true", help="print objdump for each app")
    args = parser.parse_args()

    if not SRC_DIR.exists():
        print(f"missing {SRC_DIR}", file=sys.stderr)
        sys.exit(1)

    arch_build_dir = BUILD_DIR / args.arch
    if arch_build_dir.exists():
        print(f"[user] cleaning old build dir: {arch_build_dir}")
        shutil.rmtree(arch_build_dir)

    sources = collect_sources()
    if not sources:
        print(f"no .rs, .S or .c files found in {SRC_DIR}", file=sys.stderr)
        sys.exit(1)

    apps = []
    for app_id, (source, category) in enumerate(sources):
        app = build_one(args.arch, source, app_id, category)
        app["category"] = category
        apps.append(app)

    # 单文件源编完后,构建 cargo workspace(大工程 Rust)
    build_rust_workspace(args.arch)

    if args.objdump:
        cfg = ARCH_CONFIG[args.arch]
        for app in apps:
            elf = BUILD_DIR / args.arch / f"{app['id']}_{app['source'].stem}.elf"
            run([cfg["objdump"], "-d", str(elf)])


if __name__ == "__main__":
    main()