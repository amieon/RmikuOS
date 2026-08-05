#!/usr/bin/env python3
"""diag_merge_libgcc.py —— 模拟 build.py 的 _tcc_merge_libgcc 逻辑,
找出 libgcc.a 中定义了 __*tf* 符号的成员为什么没有进入 libtcc1.a。

在构建机(Docker)仓库根目录运行:
    python3 user/tcc/diag_merge_libgcc.py
"""
import os
import subprocess
import sys
import tempfile

P = os.environ.get("LA_PREFIX", "/opt/cross-tools/bin/loongarch64-unknown-linux-gnu-")
GCC, NM, AR = P + "gcc", P + "nm", P + "ar"
LIBTCC1_O = sys.argv[1] if len(sys.argv) > 1 else \
    "user/build/loongarch64/tcc-sys/usr/lib/tcc/libtcc1.o"

libgcc = subprocess.check_output([GCC, "-print-libgcc-file-name"], text=True).strip()
print(f"libgcc: {libgcc}")
print(f"libtcc1.o: {LIBTCC1_O} (exists={os.path.exists(LIBTCC1_O)})")

# ---- 复现 build.py 的 have 集合 ----
have = {"memcpy", "memset"}
r = subprocess.run([NM, "--defined-only", LIBTCC1_O], capture_output=True, text=True)
for ln in r.stdout.splitlines():
    if ln.strip():
        have.add(ln.split()[-1])
print(f"\nlibtcc1.o 定义的符号数({len(have)}):")
print(" ", " ".join(sorted(have)))

# ---- 遍历 libgcc.a 成员, 找 tf 符号所在成员的命运 ----
TARGETS = {"__addtf3", "__subtf3", "__multf3", "__divtf3",
           "__extenddftf2", "__extendsftf2", "__trunctfdf2", "__trunctfsf2",
           "__gttf2", "__lttf2", "__eqtf2", "__netf2", "__getf2", "__letf2",
           "__fixtfdi", "__fixunstfdi", "__floatditf", "__floatunditf",
           "__clear_cache"}

tmp = tempfile.mkdtemp(prefix="diag-libgcc-")
subprocess.run([AR, "x", libgcc], cwd=tmp, check=True)
members = sorted(os.listdir(tmp))
print(f"\nlibgcc.a 解压出 {len(members)} 个成员")

found = 0
for o in members:
    if not o.endswith(".o"):
        continue
    r = subprocess.run([NM, "--defined-only", os.path.join(tmp, o)],
                       capture_output=True, text=True)
    syms = {ln.split()[-1] for ln in r.stdout.splitlines() if ln.strip()}
    hit = syms & TARGETS
    if not hit:
        continue
    found += 1
    clash = syms & have
    verdict = f"!! 被跳过(与 libtcc1.o 冲突: {sorted(clash)})" if clash else "OK 应被合并"
    print(f"  {o}: {sorted(hit)} -> {verdict}")

if found == 0:
    print("!! 解压后的成员里完全找不到 tf 符号 -> 成员名可能不以 .o 结尾")
    print("   全部成员名(前 30):", members[:30])

print("\n把以上输出贴回来。")
