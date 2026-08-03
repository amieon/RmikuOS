#!/usr/bin/env python3
"""build_tcc.py —— 交叉编译 TCC 0.9.28 → RmikuOS 可执行的 /programs/tcc

在宿主机(Linux, 有 riscv64-unknown-elf-gcc)运行：
    python3 user/tcc/build_tcc.py [riscv64]

策略（先跑起来, 缺啥补啥）：
  - 编译 TCC 主程序 10 个 .c（tcc.c tcctools.c libtcc.c tccpp.c tccgen.c
    tccdbg.c tccelf.c tccasm.c riscv64-gen.c riscv64-link.c riscv64-asm.c）
  - 不编 tccrun.c：-run(JIT) 依赖 mmap, RmikuOS 没有; 若 tcc.c 链接报
    tcc_run 未定义, 再 patch tcc.c 去掉 -run 分支
  - 链接用 crt0 + syscall.S + 标准 ldscript（与其它用户程序一致）
"""
import os
import subprocess
import sys
from pathlib import Path

USER_DIR = Path(__file__).resolve().parent.parent
TCC_SRC = Path(__file__).resolve().parent / "tcc-src"
TCC_DIR = Path(__file__).resolve().parent
ARCH = sys.argv[1] if len(sys.argv) > 1 else "riscv64"

CFG = {
    "riscv64": {
        "gcc": "riscv64-unknown-elf-gcc",
        "ar": "riscv64-unknown-elf-ar",
        "nm": "riscv64-unknown-elf-nm",
        "arch": "-march=rv64gc -mabi=lp64d -mcmodel=medany -mno-relax -msmall-data-limit=0 -DUSER_ARCH_RISCV64",
        "linker": USER_DIR / "linker-riscv64.ld",
        "crt0": USER_DIR / "lib" / "crt0_riscv64.S",
        "syscall": USER_DIR / "lib" / "syscall_riscv64.S",
    },
}[ARCH]

# TCC 主程序源文件（riscv64_FILES = CORE_FILES + riscv64 三件套）
# 注意: 不编 tcctools.c(tcc.c 已 #include 它); tccrun.c 已启用(-run JIT, mprotect 支持)
FILES = [
    "tcc.c", "libtcc.c", "tccpp.c", "tccgen.c",
    "tccdbg.c", "tccelf.c", "tccasm.c", "tccrun.c",
    "riscv64-gen.c", "riscv64-link.c", "riscv64-asm.c",
]

OUT = USER_DIR / "build" / ARCH / "tcc"
OUT.mkdir(parents=True, exist_ok=True)


def run(cmd, **kw):
    print("+", " ".join(str(x) for x in cmd))
    r = subprocess.run([str(x) for x in cmd], **kw)
    if r.returncode != 0:
        print(f"!! exit {r.returncode}")
        sys.exit(1)


def build():
    # 0) 生成 tccdefs_.h（tccpp.c 编译必需, 由 TCC 自带 c2str 转换 include/tccdefs.h）
    #    c2str 是宿主程序(文本级替换), 用宿主 cc 编译后跑一次即可。
    defs_h = TCC_SRC / "tccdefs_.h"
    if not defs_h.exists():
        c2str = TCC_SRC / "c2str"
        run(["cc", "-DC2STR", TCC_SRC / "conftest.c", "-o", c2str])
        run([c2str, TCC_SRC / "include" / "tccdefs.h", defs_h])
        print(f"[tcc] generated {defs_h}")

    objs = []
    # 1) crt0 / syscall 汇编
    for name, src in (("crt0", CFG["crt0"]), ("syscall", CFG["syscall"])):
        o = OUT / f"{name}.o"
        run([CFG["gcc"], *CFG["arch"].split(), "-c", src, "-o", o])
        objs.append(o)

    # 2) libc 运行时(lib/string.c: memset/memcpy 等)
    string_o = OUT / "string.o"
    run([CFG["gcc"], *CFG["arch"].split(), "-fno-builtin", "-c", USER_DIR / "lib" / "string.c", "-o", string_o])
    objs.append(string_o)

    # 3) TCC 源码
    cflags = [
        *CFG["arch"].split(),
        "-nostdlib", "-nostartfiles", "-static", "-no-pie",
        "-O2", "-fno-strict-aliasing", "-Wno-unused-result",
        "-fno-builtin",   # RmikuOS 的 strlen 等是头内联, 禁 gcc builtin 生成外部调用
        "-DONE_SOURCE=0",
        f"-I{USER_DIR / 'include'}",   # RmikuOS libc 头
        f"-I{TCC_SRC}",                 # TCC 自身头(tcc.h 等)
        f"-I{TCC_DIR}",                 # 手工 config.h
    ]
    for f in FILES:
        o = OUT / (Path(f).stem + ".o")
        run([CFG["gcc"], *cflags, "-c", TCC_SRC / f, "-o", o])
        objs.append(o)

    # 3) 链接
    elf = OUT / "tcc.elf"
    run([
        CFG["gcc"], *CFG["arch"].split(),
        "-nostdlib", "-nostartfiles", "-static", "-no-pie",
        "-Wl,--build-id=none", "-Wl,--no-relax",
        "-T", CFG["linker"], *objs, "-lgcc", "-o", elf,
    ])
    print(f"\nOK: {elf}")

    # 4) 进镜像: mkfs_ext4.sh 收 user/build/<arch>/bin/*.elf -> /bin/tcc
    import shutil
    bin_dir = USER_DIR / "build" / ARCH / "bin"
    bin_dir.mkdir(parents=True, exist_ok=True)
    shutil.copy(elf, bin_dir / "tcc.elf")
    print(f"installed -> {bin_dir / 'tcc.elf'}  (镜像里 /bin/tcc)")

    # 5) TCC 的系统文件(镜像 /usr/lib/tcc): crt1/crti/crtn + libtcc1.a + libc.a + runmain.o + include
    build_sys_files()


def add_libgcc_tf(libtcc1_a, libtcc1_o):
    """把 host libgcc.a 中与 libtcc1.o 不重复的成员全部合并进 libtcc1.a。
    TCC 静态链接不自动链 libgcc; 惰性提取只取被引用成员, 多放无害。
    按符号清单筛选(tf/__clear_cache)会漏 __clzdi2 等通用辅助, 故全量去重。"""
    import os, tempfile
    try:
        libgcc = subprocess.check_output(
            [CFG["gcc"], "-print-libgcc-file-name"], text=True).strip()
        if not os.path.exists(libgcc):
            print(f"!! libgcc.a not found: {libgcc}")
            return
    except Exception as e:
        print(f"!! cannot locate libgcc: {e}")
        return
    # libtcc1.o 已有符号 + string.o 提供的(memcpy/memset) —— 跳过这些成员的 libgcc 版本
    have = {"memcpy", "memset"}
    r = subprocess.run([CFG["nm"], "--defined-only", str(libtcc1_o)],
                       capture_output=True, text=True)
    for ln in r.stdout.splitlines():
        if ln.strip():
            have.add(ln.split()[-1])
    tmp = tempfile.mkdtemp(prefix="rmiku-libgcc-")
    subprocess.run([CFG["ar"], "x", libgcc], cwd=tmp, check=True)
    picked = []
    for o in sorted(os.listdir(tmp)):
        if not o.endswith(".o"):
            continue
        r = subprocess.run([CFG["nm"], "--defined-only", os.path.join(tmp, o)],
                           capture_output=True, text=True)
        syms = {ln.split()[-1] for ln in r.stdout.splitlines() if ln.strip()}
        if syms & have:      # 与 libtcc1.o / string.o 重复 -> 跳过(避免 defined twice)
            continue
        picked.append(os.path.join(tmp, o))
    if picked:
        subprocess.run([CFG["ar"], "rcs", str(libtcc1_a), *picked], check=True)
        print(f"libgcc merged into libtcc1.a: {len(picked)} objects")

def build_sys_files():
    import shutil
    sys_root = USER_DIR / "build" / ARCH / "tcc-sys" / "usr" / "lib" / "tcc"
    sys_root.mkdir(parents=True, exist_ok=True)

    # crt1.o = RmikuOS crt0（_start 调 main + 存 environ）
    crt1 = sys_root / "crt1.o"
    run([CFG["gcc"], *CFG["arch"].split(), "-c", CFG["crt0"], "-o", crt1])

    # crti.o / crtn.o: 空 init/fini 段对象(TCC 链接需要存在)
    empty_s = sys_root / "empty.S"
    empty_s.write_text(".section .init\n.section .fini\n", encoding="utf-8")
    for name in ("crti.o", "crtn.o"):
        run([CFG["gcc"], *CFG["arch"].split(), "-c", empty_s, "-o", sys_root / name])

    # libc.a = syscall.S + string.c + libc_extern.c
    # （printf 等是头内联; libc_extern 把公共 API 具现化为真实符号,
    #   TCC 编译用户程序时对 static inline 生成外部引用, 靠它解析）
    for name, src in (("_syscall.o", CFG["syscall"]), ("_string.o", USER_DIR / "lib" / "string.c")):
        run([CFG["gcc"], *CFG["arch"].split(), "-fno-builtin", "-c", src, "-o", sys_root / name])
    _extern = USER_DIR / "tcc" / "libc_extern.c"
    run([CFG["gcc"], *CFG["arch"].split(), "-fno-builtin", "-I", USER_DIR / "include",
         "-c", _extern, "-o", sys_root / "_libc_extern.o"])
    run([CFG["ar"], "rcs", sys_root / "libc.a",
         sys_root / "_syscall.o", sys_root / "_string.o", sys_root / "_libc_extern.o"])

    # libtcc1.a: TCC 自身运行时(TCC 链接用户程序时用)
    libtcc1_o = sys_root / "libtcc1.o"
    run([CFG["gcc"], *CFG["arch"].split(), "-O2", "-fno-builtin", "-c",
         TCC_SRC / "lib" / "libtcc1.c", "-o", libtcc1_o])
    run([CFG["ar"], "rcs", sys_root / "libtcc1.a", libtcc1_o])
    # 合并 libgcc 的 long double(128位 quad)软浮点 + __clear_cache 进 libtcc1.a
    # （TCC 静态链接不自动链 libgcc, 而 _libc_extern.o / TCC 生成的代码会引用
    #   __extenddftf2/__multf3 等 -> 由 libtcc1.a 提供, 一次解决所有程序）

    add_libgcc_tf(sys_root / "libtcc1.a", libtcc1_o)

    # runmain.o: tcc -run(JIT) 的启动 stub
    run([CFG["gcc"], *CFG["arch"].split(), "-O2", "-fno-builtin", "-c",
         TCC_SRC / "lib" / "runmain.c", "-o", sys_root / "runmain.o"])

    # include: RmikuOS 头(用户程序 include <stdio.h> 等)
    inc = sys_root / "include"
    if not inc.exists():
        shutil.copytree(USER_DIR / "include", inc)
    # TCC 编译器配套头: stddef/stdarg/stdbool/stdalign/tgmath 等(gcc 内置提供,
    # TCC 需要从源码 include/ 拷——否则 user.h->types.h include <stddef.h> 找不到)
    for h in sorted((TCC_SRC / "include").glob("*.h")):
        if not (inc / h.name).exists():
            shutil.copy(h, inc)
    # TCC 专用 stdint.h(裸机工具链无 libc 版; gcc 用内置, 不放 user/include)
    if not (inc / "stdint.h").exists():
        shutil.copy(USER_DIR / "tcc" / "stdint.h", inc)
    print(f"tcc system files -> {sys_root}")


if __name__ == "__main__":
    build()
