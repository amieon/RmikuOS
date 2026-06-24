#!/usr/bin/env python3

from pathlib import Path
import argparse
import subprocess
import sys
import re


ROOT = Path(__file__).resolve().parent.parent
USER_DIR = ROOT / "user"
SRC_DIR = USER_DIR / "src"
TESTS_DIR = USER_DIR / "tests"
BUILD_DIR = USER_DIR / "build"
INCLUDE_DIR = USER_DIR / "include"
LIB_DIR = USER_DIR / "lib"

GENERATED_RS = ROOT / "kernel" / "src" / "loader" / "generated.rs"

ARCH_CONFIG = {
    "riscv64": {
        "gcc": "riscv64-unknown-elf-gcc",
        "objcopy": "riscv64-unknown-elf-objcopy",
        "objdump": "riscv64-unknown-elf-objdump",
        "linker": USER_DIR / "linker-riscv64.ld",
        "runtime": LIB_DIR / "syscall_riscv64.S",
        "crt0": LIB_DIR / "crt0_riscv64.S",
        "runtime": LIB_DIR / "syscall_riscv64.S",
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
    sources = []
    # (源文件, 类别)  类别用来决定装进 /bin 还是 /tests
    for ext in ("*.S", "*.c"):
        for p in SRC_DIR.glob(ext):
            sources.append((p, "system"))    # src → 系统程序
        for p in TESTS_DIR.glob(ext):
            sources.append((p, "test"))       # tests → 测试程序
    sources.sort(key=lambda x: x[0].name)
    return sources


def build_one(arch: str, source: Path, app_id: int, category):
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



def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("arch", choices=ARCH_CONFIG.keys())
    parser.add_argument("--objdump", action="store_true", help="print objdump for each app")
    args = parser.parse_args()

    if not SRC_DIR.exists():
        print(f"missing {SRC_DIR}", file=sys.stderr)
        sys.exit(1)

    sources = collect_sources()
    if not sources:
        print(f"no .S or .c files found in {SRC_DIR}", file=sys.stderr)
        sys.exit(1)

    apps = []
    for app_id, (source, category) in enumerate(sources):
        app = build_one(args.arch, source, app_id, category)
        app["category"] = category
        apps.append(app)



    if args.objdump:
        cfg = ARCH_CONFIG[args.arch]
        for app in apps:
            elf = BUILD_DIR / args.arch / f"{app['id']}_{app['source'].stem}.elf"
            run([cfg["objdump"], "-d", str(elf)])


if __name__ == "__main__":
    main()