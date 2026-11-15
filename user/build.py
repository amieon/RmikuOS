#!/usr/bin/env python3

from pathlib import Path
import shutil
import argparse
import subprocess
import sys
import os
import re
import hashlib
import json
import struct


ROOT = Path(__file__).resolve().parent.parent
USER_DIR = ROOT / "user"
SRC_DIR = USER_DIR / "src"
TESTS_DIR = USER_DIR / "tests"
SAMPLES_DIR = USER_DIR / "samples"
SCHED_DIR = USER_DIR / "sched"
DL_DIR = USER_DIR / "dl"
BUILD_DIR = USER_DIR / "build"
INCLUDE_DIR = USER_DIR / "include"
LIB_DIR = USER_DIR / "lib"

# cargo workspace 所在目录(大工程 Rust,依赖 ulib)
RUST_DIR = USER_DIR / "rust"

GENERATED_RS = ROOT / "kernel" / "src" / "loader" / "generated.rs"

CACHE_FILE = BUILD_DIR / ".build_cache.json"

ARCH_CONFIG = {
    "riscv64": {
        "gcc": "riscv64-unknown-elf-gcc",
        "gxx": "riscv64-unknown-elf-g++",
        "ar": "riscv64-unknown-elf-ar",
        "nm": "riscv64-unknown-elf-nm",
        "objcopy": "riscv64-unknown-elf-objcopy",
        "objdump": "riscv64-unknown-elf-objdump",
        "linker": USER_DIR / "linker-riscv64.ld",
        "runtime": LIB_DIR / "syscall_riscv64.S",
        "crt0": LIB_DIR / "crt0_riscv64.S",
        "rust_target": "riscv64gc-unknown-none-elf",
        "ld_emu": "elf64lriscv",
        "ld_machine": 243,
        "rust_link_args": [],
        "cflags": [
            "-march=rv64gc",
            "-mabi=lp64d",
            "-mcmodel=medany",
            "-mno-relax",
            "-msmall-data-limit=0",
            "-DUSER_ARCH_RISCV64",
        ],
        "cxxflags": [
            "-fno-exceptions",
            "-fno-rtti",
            "-std=c++17",
        ],
        "ldflags": [
            "-Wl,--no-relax",
        ],
    },
    "loongarch64": {
        "gcc": "loongarch64-unknown-linux-gnu-gcc",
        "gxx": "loongarch64-unknown-linux-gnu-g++",
        "ar": "loongarch64-unknown-linux-gnu-ar",
        "nm": "loongarch64-unknown-linux-gnu-nm",
        "objcopy": "loongarch64-unknown-linux-gnu-objcopy",
        "objdump": "loongarch64-unknown-linux-gnu-objdump",
        "linker": USER_DIR / "linker-loongarch64.ld",
        "crt0": LIB_DIR / "crt0_loongarch64.S",
        "runtime": LIB_DIR / "syscall_loongarch64.S",
        "rust_target": "loongarch64-unknown-none",
        "ld_emu": "elf64loongarch",
        "ld_machine": 258,
        "rust_link_args": ["-nostartfiles", "-nostdlib"],
        "cflags": [
            "-DUSER_ARCH_LOONGARCH64",
            "-G0",
            "-mno-relax",
        ],
        "cxxflags": [
            "-fno-exceptions",
            "-fno-rtti",
            "-std=c++17",
        ],
        "ldflags": [
            "-Wl,--no-relax",
        ],
    },
}


# ==================== 缓存工具 ====================

# libc ABI 指纹按 arch 记一份：同一次构建里文件不会变，而 build_one 每个源文件
# 都要问一次（几百次 × 上百个头文件），重算纯属浪费。
_ABI_MEMO: dict = {}


def shared_deps_hash(arch: str) -> str:
    """
    所有共享依赖的联合 hash，即"libc 的 ABI 指纹"。
    任何一个文件变动 → 所有用户态二进制 + git/openssl/curl 全部重建。

    覆盖:
      - include/ 下**递归**所有头文件（arpa/ net/ netinet/ sys/ stdimpl/ 也在内）
      - zlib/*.h（git 编译时 -I user/zlib）
      - lib/ 下 string.c, thread.c, mem.c, cpp_runtime.cpp, crt0, runtime syscall
      - 链接脚本
      - user/cpp_runtime/ 下头文件 (C++ 项目用)

    libc 是 header-only（static inline），改头等于改源码；结构体布局变了还复用
    旧 .o 就是"编译过、语义错"。所以这里一律全量失效，不做细粒度。
    """
    if arch in _ABI_MEMO:
        return _ABI_MEMO[arch]

    cfg = ARCH_CONFIG[arch]
    h = hashlib.sha256()

    dep_paths = []

    # 1) 公共头文件（必须递归：netinet/tcp.h 这种改了同样会编进 .o）
    if INCLUDE_DIR.exists():
        dep_paths += sorted(INCLUDE_DIR.rglob("*.h"))

    # 2) zlib 头
    zlib_dir = USER_DIR / "zlib"
    if zlib_dir.exists():
        dep_paths += sorted(zlib_dir.glob("*.h"))

    # 3) 公共 C/C++ 库源文件
    if LIB_DIR.exists():
        for pat in ("*.c", "*.cpp", "*.S", "*.h"):
            dep_paths += sorted(LIB_DIR.glob(pat))

    # 3) crt0 / runtime (架构相关,已在 ARCH_CONFIG 里)
    for key in ("crt0", "runtime"):
        p = cfg.get(key)
        if p and Path(p).exists():
            dep_paths.append(Path(p))

    # 4) 链接脚本
    linker = cfg.get("linker")
    if linker and Path(linker).exists():
        dep_paths.append(Path(linker))

    # 5) C++ runtime 头文件
    cpp_rt_dir = USER_DIR / "cpp_runtime"
    if cpp_rt_dir.exists():
        dep_paths += sorted(cpp_rt_dir.glob("*.h"))

    # 去重 + 排序,保证顺序稳定
    seen = set()
    for p in sorted(set(dep_paths)):
        rp = str(p.resolve())
        if rp not in seen:
            seen.add(rp)
            h.update(file_sha256(p).encode())

    _ABI_MEMO[arch] = h.hexdigest()
    return _ABI_MEMO[arch]

def load_cache():
    if CACHE_FILE.exists():
        try:
            with open(CACHE_FILE, "r", encoding="utf-8") as f:
                return json.load(f)
        except Exception:
            return {}
    return {}

def save_cache(data):
    BUILD_DIR.mkdir(parents=True, exist_ok=True)
    with open(CACHE_FILE, "w", encoding="utf-8") as f:
        json.dump(data, f, indent=2)

def file_sha256(path: Path) -> str:
    h = hashlib.sha256()
    with open(path, "rb") as f:
        while True:
            chunk = f.read(65536)
            if not chunk:
                break
            h.update(chunk)
    return h.hexdigest()

def get_arch_cache(arch: str):
    return load_cache().get(arch, {})

def put_arch_cache(arch: str, arch_cache: dict):
    all_cache = load_cache()
    all_cache[arch] = arch_cache
    save_cache(all_cache)


# ==================== 构建工具 ====================

def run(cmd):
    print("+", " ".join(str(x) for x in cmd))
    subprocess.run(cmd, check=True)


def run_env(cmd, cwd=None, env=None):
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
    sources = []
    for ext in ("*.S", "*.c", "*.rs", "*.cpp"):
        for p in SRC_DIR.glob(ext):
            sources.append((p, "system"))
        for p in TESTS_DIR.glob(ext):
            sources.append((p, "test"))
        for p in SAMPLES_DIR.glob(ext):
            sources.append((p, "samples"))
        for p in SCHED_DIR.glob(ext):
            sources.append((p, "sched"))
    sources.sort(key=lambda x: x[0].name)
    return sources


# ==================== .so 插件（mini loader 用） ====================

# 校验用的 ELF64 常量，数值和 user/include/rk_elf.h 一一对应。
_DT_NULL, _DT_NEEDED, _DT_PLTRELSZ, _DT_HASH = 0, 1, 2, 4
_DT_STRTAB, _DT_SYMTAB, _DT_RELA, _DT_RELASZ = 5, 6, 7, 8
_DT_RELAENT, _DT_STRSZ, _DT_SYMENT = 9, 10, 11
_DT_INIT, _DT_FINI = 12, 13
_DT_REL, _DT_RELSZ, _DT_RELENT, _DT_PLTREL, _DT_TEXTREL = 17, 18, 19, 20, 22
_DT_JMPREL, _DT_INIT_ARRAY, _DT_FINI_ARRAY = 23, 25, 26
_DT_INIT_ARRAYSZ, _DT_FINI_ARRAYSZ = 27, 28
_DT_GNU_HASH, _DT_VERSYM = 0x6ffffef5, 0x6ffffff0
_DT_VERDEF, _DT_VERNEED = 0x6ffffffc, 0x6ffffffe
_DT_RELRSZ, _DT_RELR, _DT_RELRENT = 35, 36, 37

# loader 的 rk_dl_kind_*() 只认这四种。别的类型真机会明确报
# "unsupported relocation type"，这里提前挡住，省得烧一遍镜像。
_SO_OK_RELOC = {0, 2, 3, 5}          # NONE / 64 / RELATIVE / JUMP_SLOT
_SO_NEED_SYMS = (
    "plugin_add", "plugin_call_host", "plugin_data_addr",
    "plugin_data_value", "plugin_ctor_seen", "plugin_dtor_seen",
    "plugin_name",
)


def _so_fail(path, why):
    print(f"[dl] {path.name} 产物校验失败: {why}", file=sys.stderr)
    sys.exit(1)


def _elf_hash(name: str) -> int:
    """SysV ELF hash，和 user/lib/dl.c 的 rk_dl_elf_hash 同一个算法。"""
    h = 0
    for c in name.encode():
        h = (h << 4) + c
        g = h & 0xf0000000
        if g:
            h ^= g >> 24
        h &= ~g
    return h


def verify_shared_object(path: Path, arch: str, probe_contract=None):
    """
    纯 Python 复刻 user/lib/dl.c 的 dlopen 前置检查。

    为什么要有这一层：mini loader 是用户态自己写的，链接器只要悄悄换一种
    输出形态（.gnu.hash / DT_RELR / DT_NEEDED / 新重定位类型），编译照样通过、
    .so 照样进镜像，真机 dlopen 才红。与其每次烧镜像排查，不如在构建期
    用和 loader 完全相同的判据把不合格产物挡住。

    probe_contract=None 时按文件名判断：只有 dlopen_plugin.so 才额外验
    dlopen_test 依赖的构造/析构与符号解析探针；user/dl/ 里别的 .so 只过
    通用能力检查。宿主自检脚本可以显式传 True 覆盖这段逻辑。
    """
    if probe_contract is None:
        probe_contract = (path.stem == "dlopen_plugin")
    b = path.read_bytes()
    u16 = lambda o: struct.unpack_from("<H", b, o)[0]
    u32 = lambda o: struct.unpack_from("<I", b, o)[0]
    u64 = lambda o: struct.unpack_from("<Q", b, o)[0]
    i64 = lambda o: struct.unpack_from("<q", b, o)[0]

    if len(b) < 64 or b[:4] != b"\x7fELF" or b[4] != 2 or b[5] != 1 or b[6] != 1:
        _so_fail(path, "不是小端 ELF64 / EV_CURRENT")
    e_type, e_machine = u16(16), u16(18)
    e_phoff, e_phentsize, e_phnum = u64(32), u16(54), u16(56)
    want_machine = ARCH_CONFIG[arch]["ld_machine"]
    if e_type != 3:
        _so_fail(path, f"e_type={e_type}，loader 只接 ET_DYN(3)")
    if e_machine != want_machine:
        _so_fail(path, f"e_machine={e_machine}，本架构应为 {want_machine}")
    if e_phnum == 0 or e_phentsize < 56 or \
            e_phoff + e_phnum * e_phentsize > len(b):
        _so_fail(path, "program header 表越界")

    loads, dyn_ph, interp = [], None, False
    for i in range(e_phnum):
        p = e_phoff + i * e_phentsize
        ptype, _flags = u32(p), u32(p + 4)
        poff, pvaddr = u64(p + 8), u64(p + 16)
        pfilesz, pmemsz = u64(p + 32), u64(p + 40)
        if ptype == 3:
            interp = True
        elif ptype == 1:
            if pfilesz > pmemsz or poff + pfilesz > len(b):
                _so_fail(path, f"PT_LOAD[{i}] 越界 / filesz>memsz")
            loads.append((pvaddr, poff, pfilesz, pmemsz))
        elif ptype == 2:
            dyn_ph = (pvaddr, poff, pfilesz, pmemsz)
    if interp:
        _so_fail(path, "有 PT_INTERP（那是主程序，不是 .so）")
    if not loads:
        _so_fail(path, "没有 PT_LOAD")
    if dyn_ph is None or dyn_ph[0] == 0:
        _so_fail(path, "缺 PT_DYNAMIC / 其 p_vaddr 为 0（loader 会当成缺失）")

    def v2o(v, size=1):
        """vaddr -> 文件偏移。loader 是按 p_vaddr 映射、按 p_offset 读文件。"""
        for va, off, filesz, _mz in loads:
            if va <= v and v + size <= va + filesz:
                return off + (v - va)
        return None

    if v2o(dyn_ph[0], dyn_ph[3]) is None:
        _so_fail(path, "PT_DYNAMIC 不在任何 PT_LOAD 的文件范围内")

    tags = {}
    dyn_off = dyn_ph[1]
    for i in range(dyn_ph[2] // 16):
        tag, val = i64(dyn_off + i * 16), u64(dyn_off + i * 16 + 8)
        if tag == _DT_NULL:
            break
        tags.setdefault(tag, val)

    # loader 显式拒绝的 tag：跳过一个就是「查不到符号」或静默算错地址。
    forbidden = {
        _DT_NEEDED: "DT_NEEDED（loader 不支持 .so 依赖另一个 .so）",
        _DT_GNU_HASH: "DT_GNU_HASH（需要 --hash-style=sysv）",
        _DT_REL: "DT_REL", _DT_RELSZ: "DT_RELSZ", _DT_RELENT: "DT_RELENT",
        _DT_VERSYM: "DT_VERSYM", _DT_VERDEF: "DT_VERDEF",
        _DT_VERNEED: "DT_VERNEED", _DT_TEXTREL: "DT_TEXTREL",
        _DT_RELR: "DT_RELR", _DT_RELRSZ: "DT_RELRSZ",
        _DT_RELRENT: "DT_RELRENT",
    }
    for tag, why in forbidden.items():
        if tag in tags:
            _so_fail(path, f"含 {why}")
    for tag, name in ((_DT_HASH, "DT_HASH"), (_DT_SYMTAB, "DT_SYMTAB"),
                      (_DT_STRTAB, "DT_STRTAB"), (_DT_STRSZ, "DT_STRSZ"),
                      (_DT_SYMENT, "DT_SYMENT")):
        if tag not in tags:
            _so_fail(path, f"缺 {name}")
    if tags[_DT_SYMENT] != 24:
        _so_fail(path, f"DT_SYMENT={tags[_DT_SYMENT]}，应为 24")
    if _DT_RELAENT in tags and tags[_DT_RELAENT] != 24:
        _so_fail(path, f"DT_RELAENT={tags[_DT_RELAENT]}，应为 24")
    if _DT_PLTREL in tags and tags[_DT_PLTREL] != _DT_RELA:
        _so_fail(path, f"DT_PLTREL={tags[_DT_PLTREL]}，应为 DT_RELA(7)")

    # DT_HASH 的 nchain 是 .dynsym 条目数的唯一来源（loader 的 nsyms）。
    hash_off = v2o(tags[_DT_HASH], 8)
    if hash_off is None:
        _so_fail(path, "DT_HASH 不在 PT_LOAD 内")
    nbucket, nchain = u32(hash_off), u32(hash_off + 4)
    if not nbucket or not nchain:
        _so_fail(path, "DT_HASH 的 nbucket/nchain 为 0")
    hash_bytes = 8 + 4 * (nbucket + nchain)
    if v2o(tags[_DT_HASH], hash_bytes) is None:
        _so_fail(path, "DT_HASH 的 bucket/chain 越界")
    sym_off = v2o(tags[_DT_SYMTAB], nchain * 24)
    str_off = v2o(tags[_DT_STRTAB], tags[_DT_STRSZ])
    if sym_off is None or str_off is None:
        _so_fail(path, "DT_SYMTAB / DT_STRTAB 越界")

    def sym_name(off):
        n = u32(off)
        if n >= tags[_DT_STRSZ]:
            return ""
        end = b.find(b"\0", str_off + n, str_off + tags[_DT_STRSZ])
        return b[str_off + n: end if end >= 0 else str_off + tags[_DT_STRSZ]].decode(
            "ascii", "replace")

    # 区分符号归属：loader 的 rk_dl_resolve() 对 UNDEF 查全局表、对 ABS
    # 直接取 st_value、对其它已定义符号算 bias+st_value。
    # 0 = SHN_UNDEF, 0xfff1 = SHN_ABS（见 rk_elf.h）
    def sym_shndx(idx):
        return u16(sym_off + idx * 24 + 6) if idx < nchain else 0

    # 重定位表：类型和符号下标都要在 loader 的能力范围内。
    kinds = set()
    needs_rebase = 0   # 依赖 load_bias 才能算对的地址（数据指针 / ctor 指针）
    needs_global = 0   # 需要 loader 查 rk_dl_register 全局表的外部符号
    for base, size_tag, label in ((_DT_RELA, _DT_RELASZ, "DT_RELA"),
                                  (_DT_JMPREL, _DT_PLTRELSZ, "DT_JMPREL")):
        size = tags.get(size_tag, 0)
        if not size:
            continue
        if base not in tags or size % 24:
            _so_fail(path, f"{label} 缺表地址 / 大小非 24 倍数")
        r_off = v2o(tags[base], size)
        if r_off is None:
            _so_fail(path, f"{label} 表越界")
        for j in range(size // 24):
            info = u64(r_off + j * 24 + 8)
            rtype, sym = info & 0xffffffff, info >> 32
            if rtype not in _SO_OK_RELOC:
                _so_fail(path, f"{label}[{j}] 重定位类型 {rtype} 不在 "
                               f"loader 白名单 {sorted(_SO_OK_RELOC)} 内")
            if sym >= nchain:
                _so_fail(path, f"{label}[{j}] 符号下标 {sym} >= nchain {nchain}")
            kinds.add(rtype)
            if rtype == 3:
                needs_rebase += 1
            elif rtype in (2, 5) and sym:
                # lld 可能把 .init_array 的函数指针保留成 64(指向已定义符号)，
                # 也可能压成 RELATIVE；对 loader 两者都是「加 bias」。
                shndx = sym_shndx(sym)
                if shndx == 0:
                    needs_global += 1
                elif shndx != 0xfff1:
                    needs_rebase += 1

    # ---- 下面这些是 dlopen_test 的探针契约，只对那个插件生效 ----
    # user/dl/ 以后加的别的 .so 不必导出 plugin_*，只过上面的通用能力检查。
    if not probe_contract:
        return True
    if not needs_rebase:
        _so_fail(path, f"没有任何依赖 load_bias 的重定位（实际类型 {sorted(kinds)}）；"
                       ".init_array 的构造/析构指针与静态数据地址不会被搬正")
    if not needs_global:
        _so_fail(path, "没有指向 undefined 符号的 64/JUMP_SLOT 重定位；"
                       "plugin_call_host 的宿主符号解析探针会失去意义")
    # 构造/析构：dlopen_test 靠 .init_array / .fini_array 留痕。
    has_init = _DT_INIT in tags or (_DT_INIT_ARRAY in tags and _DT_INIT_ARRAYSZ in tags)
    has_fini = _DT_FINI in tags or (_DT_FINI_ARRAY in tags and _DT_FINI_ARRAYSZ in tags)
    if not has_init or not has_fini:
        _so_fail(path, "缺 DT_INIT(_ARRAY) / DT_FINI(_ARRAY)，构造析构跑不起来")

    # 必需符号，且必须能从 SysV hash 链查到（loader 的 dlsym 走的就是这条链）。
    bucket_off = hash_off + 8
    chain_off = bucket_off + 4 * nbucket
    found = set()
    for i in range(nchain):
        name = sym_name(sym_off + i * 24)
        if not name or u16(sym_off + i * 24 + 6) == 0:  # SHN_UNDEF
            continue
        idx = u32(bucket_off + 4 * (_elf_hash(name) % nbucket))
        guard = 0
        while idx and idx < nchain and guard <= nchain:
            if sym_name(sym_off + idx * 24) == name:
                found.add(name)
                break
            idx = u32(chain_off + 4 * idx)
            guard += 1
    missing = [s for s in _SO_NEED_SYMS if s not in found]
    if missing:
        _so_fail(path, "dlsym 查不到导出符号: " + ", ".join(missing))
    return True


def _find_lld():
    """返回可直接调用的 lld 命令行前缀；找不到返回 None。"""
    exe = shutil.which("ld.lld")
    if exe:
        return [exe]
    rustc = shutil.which("rustc")
    if rustc:
        try:
            sysroot = subprocess.check_output(
                [rustc, "--print", "sysroot"], text=True).strip()
            for pat in ("lib/rustlib/*/bin/rust-lld",
                        "lib/rustlib/*/bin/rust-lld.exe"):
                hits = sorted(Path(sysroot).glob(pat))
                if hits:
                    # rust-lld 这个名字不在 lld 的 flavor 自动识别表里，
                    # 必须显式 -flavor gnu 才会按 GNU ld 的参数解析。
                    return [str(hits[0]), "-flavor", "gnu"]
        except (OSError, subprocess.SubprocessError):
            pass
    return None


def build_shared_objects(arch: str):
    """
    把 user/dl/*.c 编成 ET_DYN 的 .so，产物 build/<arch>/dl/<stem>.so。

    和主程序的差别只有编译选项：-fPIC 取代 -fno-pic -fno-pie -static -no-pie。
    链接要商用 -shared + **--hash-style=sysv**：GNU ld 默认只产
    .gnu.hash(DT_GNU_HASH)，mini loader 第一版只认 SysV .hash(DT_HASH)，
    漏了不会编译失败，只会让真机 dlopen 明确报 "no DT_HASH"。

    链接器不能写死 gcc 驱动：binutils 对 riscv*-elf / loongarch*-elf 这两个
    **裸机** target 在 emulparams 里刻意不生成 shared/PIE 脚本，ld.bfd 一律
    回 "-shared not supported"（2026-09 才有人提 patch 放开）。first_run.sh 装的
    riscv64-unknown-elf-gcc 正是这种。上游给的替代是 hosted 的 *-linux ld 或
    LLVM lld —— 本项目选 lld：Rust 工具链自带 rust-lld，不用额外装 apt 包。
    流程是「先试 gcc，只有确认为这条报错才降级到 lld」，别的编译错误原样抛，
    不许拿换链接器把真错误盖过去。

    -nostdlib 保证不产生 DT_NEEDED（loader 不支持 .so 依赖另一个 .so）。
    链完必跑 verify_shared_object()：产物形态不合格就 exit 1，别烧镜像才发现。

    故意不进增量缓存：这里只有一两个小文件，每次全量编几毫秒；而它们会被
    include/**/*.h 的改动影响，搭错缓存就是「改了头、旧 .so 还在跑」。
    """
    if not DL_DIR.exists():
        return
    cfg = ARCH_CONFIG[arch]
    out_dir = BUILD_DIR / arch / "dl"
    out_dir.mkdir(parents=True, exist_ok=True)

    common = [
        "-ffreestanding",
        "-fno-builtin",
        "-fno-stack-protector",
        "-fPIC",
        "-nostdlib",
        "-nostartfiles",
        "-I", str(INCLUDE_DIR),
    ]
    for src in sorted(DL_DIR.glob("*.c")):
        obj = out_dir / f"{src.stem}.o"
        so = out_dir / f"{src.stem}.so"
        run([cfg["gcc"], *cfg["cflags"], *common, "-c", str(src), "-o", str(obj)])
        # 先删旧产物：否则链接器失败时校验器会拿上次的 .so 验出 PASS，
        # 把「这次没编出来」盖成「产物合格」。
        so.unlink(missing_ok=True)
        gnu_cmd = [
            cfg["gcc"], *cfg["cflags"],
            "-shared", "-fPIC", "-nostdlib", "-nostartfiles",
            "-Wl,--hash-style=sysv", "-Wl,--build-id=none",
            *cfg["ldflags"],
            "-o", str(so), str(obj),
        ]
        probe = subprocess.run(gnu_cmd, stdout=subprocess.PIPE,
                               stderr=subprocess.PIPE, text=True)
        linked_out = probe.stderr + probe.stdout
        if probe.returncode == 0:
            print("+", " ".join(gnu_cmd))
        elif "shared not supported" in linked_out:
            print(f"[user] GNU ld 不支持 -shared（裸机 {cfg['gcc']}），改用 LLVM lld")
            lld = _find_lld()
            if not lld:
                print("[user] 找不到 ld.lld / rust-lld；装 lld 或确认 rustup 已装好",
                      file=sys.stderr)
                sys.exit(1)
            run([*lld, "-m", cfg["ld_emu"], "-shared",
                 "--hash-style=sysv", "--build-id=none", "--no-relax",
                 "--pack-dyn-relocs=none",
                 "-o", str(so), str(obj)])
        else:
            print("+", " ".join(gnu_cmd))
            sys.stdout.write(probe.stdout)
            sys.stderr.write(probe.stderr)
            print("[user] .so 链接失败（不是 'shared not supported'，不降级）",
                  file=sys.stderr)
            sys.exit(probe.returncode or 1)
        if not so.exists() or so.stat().st_size < 64:
            print(f"[user] {so.name} 未生成或过小，链接器可能静默失败",
                  file=sys.stderr)
            sys.exit(1)
        verify_shared_object(so, arch)
        print(f"[user] built shared object: {so.name}, {so.stat().st_size} bytes "
              f"(ELF 校验通过)")


# ==================== 单文件编译（带缓存） ====================

def build_one(arch: str, source: Path, app_id: int, category):
    stem = source.stem
    if category == "system":
        out_dir = BUILD_DIR / arch / "bin"
    elif category == "test":
        out_dir = BUILD_DIR / arch / "tests"
    elif category == "samples":
        out_dir = BUILD_DIR / arch / "samples"
    elif category == "sched":
        out_dir = BUILD_DIR / arch / "sched"
    out_dir.mkdir(parents=True, exist_ok=True)


    # ---- 增量编译检查 ----
    arch_cache = get_arch_cache(arch)
    single_cache = arch_cache.get("single", {})
    src_key = str(source.resolve())

    h = hashlib.sha256()
    h.update(file_sha256(source).encode())
    for hdr in sorted(source.parent.glob("*.h")):
        h.update(file_sha256(hdr).encode())
    h.update(shared_deps_hash(arch).encode())
    # 编译器和编译开关也算输入。上一版只签源文件，于是改 ARCH_CONFIG 里的
    # cflags（比如给某个架构加 -O2）或换编译器路径时，全部程序静默复用旧 ELF
    # —— 跟"头文件不进 key"是同一类坑，只是更隐蔽：源文件一个字节都没变。
    cfg = ARCH_CONFIG[arch]
    h.update(" ".join([cfg["gcc"], cfg["gxx"],
                       *cfg["cflags"], *cfg["cxxflags"]]).encode())
    current_hash = h.hexdigest()


    elf_path = out_dir / f"{app_id}_{stem}.elf"
    if single_cache.get(src_key) == current_hash and elf_path.exists():
        data = elf_path.read_bytes()
        print(f"[user] cache hit app{app_id}: {source.name}, {len(data)} bytes (ELF)")
        return {
            "id": app_id,
            "source": source,
            "name": stem,
            "symbol": sanitize_name(source),
            "bin": elf_path,        
            "data": data,         
            "category": category,
        }

    # ---- 真正编译 ----
    if source.suffix == ".rs":
        result = _build_rust_real(arch, source, app_id, category, out_dir, stem, elf_path)
    elif source.suffix == ".cpp":
        result = _build_cpp_real(arch, source, app_id, category, out_dir, stem, elf_path)
    else:
        result = _build_c_asm_real(arch, source, app_id, category, out_dir, stem, elf_path)

    # ---- 更新缓存 ----
    arch_cache = get_arch_cache(arch)  # 重新读取，防止并发覆盖
    arch_cache.setdefault("single", {})[src_key] = current_hash
    put_arch_cache(arch, arch_cache)
    return result


def _build_c_asm_real(arch, source, app_id, category, out_dir, stem, elf_path):
    cfg = ARCH_CONFIG[arch]
    obj = out_dir / f"{app_id}_{stem}.o"
    crt0_obj = out_dir / f"{app_id}_{stem}_crt0.o"
    runtime_obj = out_dir / f"{app_id}_{stem}_runtime.o"
    elf = out_dir / f"{app_id}_{stem}.elf"

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
        "-I", str(USER_DIR / "zlib"),
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

    string_src = LIB_DIR / "string.c"
    string_obj = out_dir / f"{app_id}_{stem}_string.o"
    has_string = string_src.exists()
    if has_string:
        run([
            cfg["gcc"],
            *cfg["cflags"],
            *common_flags,
            "-c",
            str(string_src),
            "-o",
            str(string_obj),
        ])

    thread_src = LIB_DIR / "thread.c"
    thread_obj = out_dir / f"{app_id}_{stem}_thread.o"
    has_thread = thread_src.exists()
    if has_thread:
        run([
            cfg["gcc"],
            *cfg["cflags"],
            *common_flags,
            "-c",
            str(thread_src),
            "-o",
            str(thread_obj),
        ])

    mem_src = LIB_DIR / "mem.c"
    mem_obj = out_dir / f"{app_id}_{stem}_mem.o"
    has_mem = mem_src.exists()
    if has_mem:
        run([
            cfg["gcc"],
            *cfg["cflags"],
            *common_flags,
            "-c",
            str(mem_src),
            "-o",
            str(mem_obj),
        ])

    dl_src = LIB_DIR / "dl.c"
    dl_obj = out_dir / f"{app_id}_{stem}_dl.o"
    has_dl = dl_src.exists()
    if has_dl:
        run([
            cfg["gcc"],
            *cfg["cflags"],
            *common_flags,
            "-c",
            str(dl_src),
            "-o",
            str(dl_obj),
        ])

    link_objects = []
    if source.suffix == ".c":
        link_objects.append(str(crt0_obj))
    link_objects.append(str(obj))
    # runtime(syscall.o) 所有程序都链: string.o 现在依赖 syscall3(exit flush 链),
    # .S 纯汇编程序(如 02_raw) 也要有 syscall 运行时
    link_objects.append(str(runtime_obj))
    if has_string:
        link_objects.append(str(string_obj))
    if has_thread:
        link_objects.append(str(thread_obj))
    if has_mem:
        link_objects.append(str(mem_obj))
    if has_dl:
        link_objects.append(str(dl_obj))

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
        *zlib_libs(arch),   # 静态库按需抽取: 不引用 zlib 符号则一个字节都不增加
        "-o",
        str(elf),
    ]
    run(link_cmd)

    data = elf.read_bytes()
    if not data:
        raise RuntimeError(f"{source} produced empty ELF")

    print(f"[user] built app{app_id}: {source.name}, {len(data)} bytes (ELF)")
    return {
        "id": app_id,
        "source": source,
        "name": stem,
        "symbol": sanitize_name(source),
        "bin": elf,            
        "data": data,         
        "category": category,
    }



def _build_cpp_real(arch, source, app_id, category, out_dir, stem, elf_path):
    cfg = ARCH_CONFIG[arch]
    obj = out_dir / f"{app_id}_{stem}.o"
    crt0_obj = out_dir / f"{app_id}_{stem}_crt0.o"
    runtime_obj = out_dir / f"{app_id}_{stem}_syscall.o"
    cpprt_obj = out_dir / f"{app_id}_{stem}_cpprt.o"
    elf = out_dir / f"{app_id}_{stem}.elf"

    common_flags = [
        "-ffreestanding", "-fno-builtin", "-fno-stack-protector",
        "-fno-pic", "-fno-pie", "-nostdlib", "-nostartfiles", "-static",
        "-I", str(INCLUDE_DIR),
    ]

    run([cfg["gcc"], *cfg["cflags"], *common_flags, "-c",
         str(cfg["crt0"]), "-o", str(crt0_obj)])
    run([cfg["gcc"], *cfg["cflags"], *common_flags, "-c",
         str(cfg["runtime"]), "-o", str(runtime_obj)])

    cpp_runtime_src = LIB_DIR / "cpp_runtime.cpp"
    run([cfg["gxx"], *cfg["cflags"], *cfg["cxxflags"], *common_flags, "-c",
         str(cpp_runtime_src), "-o", str(cpprt_obj)])
    run([cfg["gxx"], *cfg["cflags"], *cfg["cxxflags"], *common_flags, "-c",
         str(source), "-o", str(obj)])

    string_src = LIB_DIR / "string.c"
    string_obj = out_dir / f"{app_id}_{stem}_string.o"
    has_string = string_src.exists()
    if has_string:
        run([cfg["gcc"], *cfg["cflags"], *common_flags, "-c",
             str(string_src), "-o", str(string_obj)])

    thread_src = LIB_DIR / "thread.c"
    thread_obj = out_dir / f"{app_id}_{stem}_thread.o"
    has_thread = thread_src.exists()
    if has_thread:
        run([cfg["gcc"], *cfg["cflags"], *common_flags, "-c",
             str(thread_src), "-o", str(thread_obj)])

    mem_src = LIB_DIR / "mem.c"
    mem_obj = out_dir / f"{app_id}_{stem}_mem.o"
    has_mem = mem_src.exists()
    if has_mem:
        run([cfg["gcc"], *cfg["cflags"], *common_flags, "-c",
             str(mem_src), "-o", str(mem_obj)])

    link_cmd = [
        cfg["gxx"], *cfg["cflags"],
        "-nostdlib", "-nostartfiles", "-static", "-no-pie",
        "-Wl,--build-id=none", *cfg["ldflags"],
        "-T", str(cfg["linker"]),
        str(crt0_obj), str(obj), str(runtime_obj), str(cpprt_obj),
    ]
    if has_string:
        link_cmd.append(str(string_obj))
    if has_thread:
        link_cmd.append(str(thread_obj))
    if has_mem:
        link_cmd.append(str(mem_obj))
    link_cmd.append("-lgcc")
    link_cmd.extend(["-o", str(elf)])
    run(link_cmd)
    data = elf.read_bytes()
    if not data:
        raise RuntimeError(f"{source} produced empty ELF")

    print(f"[user] built cpp app{app_id}: {source.name}, {len(data)} bytes (ELF)")
    return {
        "id": app_id, "source": source, "name": stem,
        "symbol": sanitize_name(source), "bin": elf,  
        "data": data, "category": category,          
    }


def _build_rust_real(arch, source, app_id, category, out_dir, stem, elf_path):
    cfg = ARCH_CONFIG[arch]
    elf = out_dir / f"{app_id}_{stem}.elf"

    cmd = [
        "rustc",
        "--target", cfg["rust_target"],
        "-C", "panic=abort",
        "-C", "relocation-model=static",
        "-C", f"link-arg=-T{cfg['linker']}",
        "-o", str(elf), str(source),
    ]
    run(cmd)
    data = elf.read_bytes()
    if not data:
        raise RuntimeError(f"{source} produced empty ELF")

    print(f"[user] built rust(single) app{app_id}: {source.name}, {len(data)} bytes (ELF)")
    return {
        "id": app_id,
        "source": source,
        "name": stem,
        "symbol": sanitize_name(source),
        "bin": elf,              # ← 改
        "data": data,            # ← 改
        "category": category,
    }


# ==================== 项目目录编译（带缓存） ====================

def build_project_dir(arch: str, src_dir: Path, out_dir: Path, is_cpp: bool = False):
    cfg = ARCH_CONFIG[arch]
    out_dir.mkdir(parents=True, exist_ok=True)

    if is_cpp:
        sources = sorted(src_dir.glob("*.cpp"))
    else:
        sources = sorted(src_dir.glob("*.c"))

    if not sources:
        print(f"[user] {src_dir}: no {'*.cpp' if is_cpp else '*.c'} files, skip")
        return

    # ---- 增量编译检查：计算目录下所有源文件的 hash ----
    arch_cache = get_arch_cache(arch)
    proj_cache = arch_cache.get("project", {})
    proj_key = str(src_dir.resolve())

    files_hash = {}
    for s in sources:
        files_hash[str(s.resolve())] = file_sha256(s)

    # 也检查头文件变化（include 目录下常用头文件）
    # 保守策略：如果 src_dir 自身有 .h 也计入
    for h in sorted(src_dir.glob("*.h")):
        files_hash[str(h.resolve())] = file_sha256(h)

    files_hash["__shared_deps__"] = shared_deps_hash(arch)

    cached_entry = proj_cache.get(proj_key)
    if cached_entry and cached_entry.get("files") == files_hash:
        all_exist = True
        for s in sources:
            content = s.read_text()
            if "int main(" in content or 'extern "C" int main(' in content:
                if not (out_dir / f"{s.stem}.elf").exists():
                    all_exist = False
                    break
        if all_exist:
            print(f"[user] cache hit project: {src_dir.name}")
            return

    # ---- 真正编译 ----
    common_flags = [
        "-ffreestanding", "-fno-builtin", "-fno-stack-protector",
        "-fno-pic", "-fno-pie", "-nostdlib", "-nostartfiles", "-static",
        "-I", str(INCLUDE_DIR),
        "-I", str(USER_DIR / "cpp_runtime"),
        "-I", str(src_dir),
    ]

    crt0_obj = out_dir / "_crt0.o"
    runtime_obj = out_dir / "_syscall.o"

    string_obj = None
    string_src = LIB_DIR / "string.c"
    if string_src.exists():
        string_obj = out_dir / "_string.o"
        run([cfg["gcc"], *cfg["cflags"], *common_flags, "-c",
             str(string_src), "-o", str(string_obj)])

    thread_obj = None
    mem_obj = None
    thread_src = LIB_DIR / "thread.c"
    if thread_src.exists():
        thread_obj = out_dir / "_thread.o"
        run([cfg["gcc"], *cfg["cflags"], *common_flags, "-c",
             str(thread_src), "-o", str(thread_obj)])

    mem_src = LIB_DIR / "mem.c"
    if mem_src.exists():
        mem_obj = out_dir / "_mem.o"
        run([cfg["gcc"], *cfg["cflags"], *common_flags, "-c",
             str(mem_src), "-o", str(mem_obj)])

    run([cfg["gcc"], *cfg["cflags"], *common_flags, "-c",
         str(cfg["crt0"]), "-o", str(crt0_obj)])
    run([cfg["gcc"], *cfg["cflags"], *common_flags, "-c",
         str(cfg["runtime"]), "-o", str(runtime_obj)])

    cpprt_obj = None
    if is_cpp:
        cpp_runtime_src = LIB_DIR / "cpp_runtime.cpp"
        if cpp_runtime_src.exists():
            cpprt_obj = out_dir / "_cpprt.o"
            run([cfg["gxx"], *cfg["cflags"], *cfg["cxxflags"], *common_flags, "-c",
                 str(cpp_runtime_src), "-o", str(cpprt_obj)])

    objs = []
    entry_sources = []

    for source in sources:
        stem = source.stem
        obj = out_dir / f"{stem}.o"

        compiler = cfg["gxx"] if is_cpp else cfg["gcc"]
        cxx_flags = cfg["cxxflags"] if is_cpp else []

        run([compiler, *cfg["cflags"], *cxx_flags, *common_flags, "-c",
             str(source), "-o", str(obj)])

        objs.append(obj)

        content = source.read_text()
        if "int main(" in content or 'extern "C" int main(' in content:
            entry_sources.append(source)

    if not entry_sources:
        print(f"[user] {src_dir}: no entry point (main) found, skip linking")
        return

    for entry_src in entry_sources:
        entry_stem = entry_src.stem
        entry_obj = out_dir / f"{entry_stem}.o"

        other_objs = []
        for o in objs:
            if o == entry_obj:
                continue
            other_src = src_dir / (o.stem + (".cpp" if is_cpp else ".c"))
            if other_src.exists():
                other_content = other_src.read_text()
                if "int main(" not in other_content and "extern \"C\" int main(" not in other_content:
                    other_objs.append(str(o))

        elf = out_dir / f"{entry_stem}.elf"

        link_objs = [str(crt0_obj), str(entry_obj), str(runtime_obj)] + other_objs
        if string_obj:
            link_objs.append(str(string_obj))
        if thread_obj:
            link_objs.append(str(thread_obj))
        if mem_obj:
            link_objs.append(str(mem_obj))
        if cpprt_obj and cpprt_obj.exists():
            link_objs.append(str(cpprt_obj))

        linker = cfg["gxx"] if is_cpp else cfg["gcc"]
        run([linker, *cfg["cflags"],
             "-nostdlib", "-nostartfiles", "-static", "-no-pie",
             "-Wl,--build-id=none", *cfg["ldflags"],
             "-T", str(cfg["linker"]),
             *link_objs,
             *(["-lgcc"] if is_cpp else []), 
             "-o", str(elf)])

        data = elf.read_bytes()
        print(f"[user] {src_dir.name}/{entry_stem}: {len(data)} bytes (ELF)")

    # ---- 更新缓存 ----
    arch_cache = get_arch_cache(arch)
    arch_cache.setdefault("project", {})[proj_key] = {"files": files_hash}
    put_arch_cache(arch, arch_cache)


def build_sqlite3(arch: str):
    """
    专门构建 SQLite3。

    源码放在 user/sqlite3/（已移出 user/c/，因此不会被 build_c_projects 扫到）。
    SQLite 是巨型可移植库，必须靠 -D 宏关掉宿主 OS 后端，否则会去编
    os_unix.c 撞上 pthread.h / dlfcn.h。

    注意：本函数编译 sqlite3.c + shell.c + rmiku_vfs.c，
    链出交互式 shell（sqlite3.elf）；另保留原探针 sqlite_main.c，
    链出 sqlite3_probe.elf 作回归验证。
    """
    cfg = ARCH_CONFIG[arch]
    src_dir = USER_DIR / "sqlite3"
    if not src_dir.exists():
        print(f"[user] no sqlite3 dir at {src_dir}, skip")
        return

    out_dir = BUILD_DIR / arch / "sqlite3"
    out_dir.mkdir(parents=True, exist_ok=True)

    # SQLite 专用编译宏：
    #  - SQLITE_OS_OTHER=1          : 不编 os_unix/os_win，改用应用提供的 sqlite3_os_init/os_end
    #  - SQLITE_THREADSAFE=0        : 关掉线程/互斥，避免 pthread.h
    #  - SQLITE_OMIT_LOAD_EXTENSION : 关掉 dlopen/dlsym，避免 dlfcn.h
    #  - SQLITE_OMIT_DEPRECATED     : 去掉废弃接口，减小体积
    #  - SQLITE_OMIT_DATETIME_FUNCS : 关掉 localtime/gmtime/strftime（date.c），避免宿主时间函数
    #  - SQLITE_OMIT_POPEN          : 去掉 popen/pclose（无管道，.import "|cmd" 报错提示）
    #  - SQLITE_NOHAVE_SYSTEM       : 去掉 system()（无 /bin/sh，.shell/.system/edit() 不编入）
    #  - -Os                        : 9.5MB 单编译单元，默认 -O0 体积爆炸
    sqlite_cflags = [
        "-DSQLITE_OS_OTHER=1",
        "-DSQLITE_THREADSAFE=0",
        "-DSQLITE_OMIT_LOAD_EXTENSION",
        "-DSQLITE_OMIT_DEPRECATED",
        "-DSQLITE_OMIT_DATETIME_FUNCS",
        "-DSQLITE_OMIT_POPEN",
        "-DSQLITE_NOHAVE_SYSTEM",
        "-Os",
    ]

    common_flags = [
        "-ffreestanding", "-fno-builtin", "-fno-stack-protector",
        "-fno-pic", "-fno-pie", "-nostdlib", "-nostartfiles", "-static",
        "-I", str(INCLUDE_DIR),
        "-I", str(src_dir),
    ]

    # ---- 增量编译检查：把 cflags 也并进缓存键（flags 变了必须重建）----
    arch_cache = get_arch_cache(arch)
    sql_cache = arch_cache.get("sqlite3", {})

    sources = {
        "sqlite3": src_dir / "sqlite3.c",
        "shell": src_dir / "shell.c",
        "rmiku_vfs": src_dir / "rmiku_vfs.c",
        "sqlite_main": src_dir / "sqlite_main.c",
        "sqlite_test": src_dir / "sqlite_test.c",
    }
    files_hash = {k: file_sha256(v) for k, v in sources.items() if v.exists()}
    files_hash["__shared_deps__"] = shared_deps_hash(arch)
    files_hash["__cflags__"] = repr(sqlite_cflags)

    elf = out_dir / "sqlite3.elf"
    if sql_cache == files_hash and elf.exists():
        print(f"[user] cache hit sqlite3")
        return

    # ---- 真正编译 ----
    crt0_obj = out_dir / "_crt0.o"
    runtime_obj = out_dir / "_syscall.o"
    run([cfg["gcc"], *cfg["cflags"], *common_flags, "-c",
         str(cfg["crt0"]), "-o", str(crt0_obj)])
    run([cfg["gcc"], *cfg["cflags"], *common_flags, "-c",
         str(cfg["runtime"]), "-o", str(runtime_obj)])

    string_obj = None
    string_src = LIB_DIR / "string.c"
    if string_src.exists():
        string_obj = out_dir / "_string.o"
        run([cfg["gcc"], *cfg["cflags"], *common_flags, "-c",
             str(string_src), "-o", str(string_obj)])

    thread_obj = None
    mem_obj = None
    thread_src = LIB_DIR / "thread.c"
    if thread_src.exists():
        thread_obj = out_dir / "_thread.o"
        run([cfg["gcc"], *cfg["cflags"], *common_flags, "-c",
             str(thread_src), "-o", str(thread_obj)])

    mem_src = LIB_DIR / "mem.c"
    if mem_src.exists():
        mem_obj = out_dir / "_mem.o"
        run([cfg["gcc"], *cfg["cflags"], *common_flags, "-c",
             str(mem_src), "-o", str(mem_obj)])

    objs = {}
    for name, src in sources.items():
        if not src.exists():
            continue
        obj = out_dir / f"{name}.o"
        run([cfg["gcc"], *cfg["cflags"], *sqlite_cflags, *common_flags, "-c",
             str(src), "-o", str(obj)])
        objs[name] = str(obj)

    def link(elf_name, entry_obj):
        """链接一个可执行文件: crt0 + sqlite3.o + 入口 + vfs + 运行时"""
        link_objs = [str(crt0_obj), objs["sqlite3"], entry_obj,
                     objs["rmiku_vfs"], str(runtime_obj)]
        if string_obj:
            link_objs.append(str(string_obj))
        if thread_obj:
            link_objs.append(str(thread_obj))
        if mem_obj:
            link_objs.append(str(mem_obj))
        out_elf = out_dir / elf_name
        run([cfg["gcc"], *cfg["cflags"],
             "-nostdlib", "-nostartfiles", "-static", "-no-pie",
             "-Wl,--build-id=none", *cfg["ldflags"],
             "-T", str(cfg["linker"]),
             *link_objs,
             "-lgcc",   # __bswapsi2 / __clzdi2 等编译器内建, nostdlib 下需手动链
             "-o", str(out_elf)])
        data = out_elf.read_bytes()
        if not data:
            raise RuntimeError(f"{elf_name} produced empty ELF")
        print(f"[user] sqlite3: {elf_name} {len(data)} bytes (ELF)")

    # sqlite3.elf = 交互式 shell; sqlite3_probe.elf = 原落盘探针; sqlite_test.elf = 规范化回归测试
    link("sqlite3.elf", objs["shell"])
    link("sqlite3_probe.elf", objs["sqlite_main"])
    link("sqlite_test.elf", objs["sqlite_test"])

    arch_cache = get_arch_cache(arch)
    arch_cache["sqlite3"] = files_hash
    put_arch_cache(arch, arch_cache)



def build_zlib(arch: str):
    """
    构建 zlib 1.3.1 —— 本地 git 的硬前置。

    git 的 object 存储(loose object / packfile / index 扩展)全部走 zlib 的
    deflate/inflate, 没有它编译时会出现成片的 "zlib.h: No such file"。

    编全部 15 个 .c: 核心 11 个(deflate/inflate 算法 + 校验和) +
    gz* 4 个(gzFile 文件级 API, 读写 .gz 文件)。git 本身只用裸流, 但保留 gz
    是为了这个系统里的 zlib 是**完整的** —— 以后写 gzip/gunzip 命令、
    给 httpd 加 gzip 编码都能直接用, 不会出现"以为有其实没有"的坑。

    关于原子 / 多线程:
      zutil.h 的 z_once() 整段包在 `#ifdef Z_ONCE` 里, 而 Z_ONCE 只在
      crc32.c 的 DYNAMIC_CRC_TABLE 或 inftrees.c 的 BUILDFIXED/MAKEFIXED
      下才 #define。我们两个宏都没传 —— CRC 表和 fixed Huffman 表都是
      编译期静态常量, 多线程并发 inflate/deflate 天然安全。
      所以**不再**喂 -D__STDC_NO_ATOMICS__: 旧注释说它"单线程下等价",
      实际是空操作, 而且留着反而有害 —— 将来谁误开 DYNAMIC_CRC_TABLE,
      zutil.h 会静默走非原子 test_and_set 那版(自带
      `#warning zlib not thread-safe`), 在多线程 git 里变成真竞态。
      去掉之后那种情况会直接 `#include <stdatomic.h>` 失败(我们没有这个头),
      编译期就拦住, 不会静默给出一个不线程安全的库。
      真要开动态表, 得先补 stdatomic.h 或把 z_once 换成 pthread_once。

    产物: build/<arch>/zlib/zlib_test.elf  ->  mkfs 拷到 /tests/zlib_test
    """
    cfg = ARCH_CONFIG[arch]
    src_dir = USER_DIR / "zlib"
    if not src_dir.exists():
        print(f"[user] no zlib dir at {src_dir}, skip")
        return

    out_dir = BUILD_DIR / arch / "zlib"
    out_dir.mkdir(parents=True, exist_ok=True)

    zlib_cflags = ["-Os"]
    common_flags = [
        "-ffreestanding", "-fno-builtin", "-fno-stack-protector",
        "-fno-pic", "-fno-pie", "-nostdlib", "-nostartfiles", "-static",
        "-I", str(INCLUDE_DIR),
        "-I", str(src_dir),
    ]

    core = ["adler32", "compress", "crc32", "deflate", "infback", "inffast",
            "inflate", "inftrees", "trees", "uncompr", "zutil",
            "gzclose", "gzlib", "gzread", "gzwrite"]

    arch_cache = get_arch_cache(arch)
    cache = arch_cache.get("zlib", {})
    files_hash = {n: file_sha256(src_dir / f"{n}.c")
                  for n in core if (src_dir / f"{n}.c").exists()}
    test_src = src_dir / "zlib_test.c"
    has_test = test_src.exists()
    if has_test:
        files_hash["__test__"] = file_sha256(test_src)
    else:
        print("[user] zlib: 警告: 缺少 zlib_test.c(本项目自带测试, 上游包里没有)")
        print("[user] zlib:       本次只编库(.o), 不产出 /tests/zlib_test")
    files_hash["__shared_deps__"] = shared_deps_hash(arch)
    files_hash["__cflags__"] = repr(zlib_cflags)

    elf = out_dir / "zlib_test.elf"
    stamp = elf if has_test else (out_dir / "zutil.o")
    if cache == files_hash and stamp.exists():
        print("[user] cache hit zlib")
        deploy_libz(arch, out_dir / "libz.a")   # 缓存命中也要保证 tcc 那边同步
        return

    crt0_obj = out_dir / "_crt0.o"
    runtime_obj = out_dir / "_syscall.o"
    run([cfg["gcc"], *cfg["cflags"], *common_flags, "-c",
         str(cfg["crt0"]), "-o", str(crt0_obj)])
    run([cfg["gcc"], *cfg["cflags"], *common_flags, "-c",
         str(cfg["runtime"]), "-o", str(runtime_obj)])

    lib_objs = []
    for name in core:
        src = src_dir / f"{name}.c"
        if not src.exists():
            continue
        obj = out_dir / f"{name}.o"
        run([cfg["gcc"], *cfg["cflags"], *zlib_cflags, *common_flags, "-c",
             str(src), "-o", str(obj)])
        lib_objs.append(str(obj))

    libz = out_dir / "libz.a"
    if libz.exists():
        libz.unlink()
    run([cfg["ar"], "rcs", str(libz), *lib_objs])
    print(f"[user] zlib: libz.a ({len(lib_objs)} 个对象)")

    link_objs = [str(crt0_obj)]

    if has_test:
        test_obj = out_dir / "zlib_test.o"
        run([cfg["gcc"], *cfg["cflags"], *zlib_cflags, *common_flags, "-c",
             str(test_src), "-o", str(test_obj)])
        link_objs.append(str(test_obj))
        # 注意用 libz.a 而不是散装 .o —— 测试的链接方式就是用户以后的使用方式，
    # 顺带验证静态库能被正常解析符号

    string_src = LIB_DIR / "string.c"
    if string_src.exists():
        string_obj = out_dir / "_string.o"
        run([cfg["gcc"], *cfg["cflags"], *common_flags, "-c",
             str(string_src), "-o", str(string_obj)])
        link_objs.append(str(string_obj))

    thread_src = LIB_DIR / "thread.c"
    if thread_src.exists():
        thread_obj = out_dir / "_thread.o"
        run([cfg["gcc"], *cfg["cflags"], *common_flags, "-c",
             str(thread_src), "-o", str(thread_obj)])
        link_objs.append(str(thread_obj))

    mem_src = LIB_DIR / "mem.c"
    if mem_src.exists():
        mem_obj = out_dir / "_mem.o"
        run([cfg["gcc"], *cfg["cflags"], *common_flags, "-c",
             str(mem_src), "-o", str(mem_obj)])
        link_objs.append(str(mem_obj))

    link_objs.append(str(runtime_obj))
    link_objs.append(str(libz))      # 库放最后：静态库的符号解析是顺序的

    if has_test:
        run([cfg["gcc"], *cfg["cflags"],
             "-nostdlib", "-nostartfiles", "-static", "-no-pie",
             "-Wl,--build-id=none", *cfg["ldflags"],
             "-T", str(cfg["linker"]),
             *link_objs,
             "-lgcc",   # 编译器内建(如 __clzdi2), nostdlib 下需手动链
             "-o", str(elf)])
        if not elf.exists() or elf.stat().st_size == 0:
            raise RuntimeError("zlib_test.elf produced empty ELF")
        print(f"[user] zlib: zlib_test.elf {elf.stat().st_size} bytes (ELF)")
    else:
        print(f"[user] zlib: 已生成 {len([o for o in link_objs if o.endswith('.o')])} 个 .o(供 git 链接用)")

    deploy_libz(arch, libz)

    arch_cache["zlib"] = files_hash
    put_arch_cache(arch, arch_cache)


def deploy_libz(arch: str, libz: Path):
    """把 libz.a 放进 tcc 的库搜索目录 —— 系统内 `tcc foo.c -lz` 就能直接用。

    这一层才是"自举"的关键：库躺在系统里，而不是只存在宿主机的 build 目录。
    tcc 链接库时不需要 ranlib 索引（它自己扫 ar），所以 rcs 的 s 已经够了。
    """
    tcc_lib = BUILD_DIR / arch / "tcc-sys" / "usr" / "lib" / "tcc"
    if not tcc_lib.is_dir():
        return                      # tcc 没构建时跳过
    shutil.copy2(libz, tcc_lib / "libz.a")
    # 光有库不够: 系统内 `tcc foo.c -lz` 还要能 #include <zlib.h>。
    # tcc 的头搜索路径是 /usr/lib/tcc/include（build_tcc_sys_files 从 user/include 同步）
    inc = tcc_lib / "include"
    copied = []
    if inc.is_dir():
        for h in ("zlib.h", "zconf.h"):
            src = USER_DIR / "zlib" / h
            if src.exists():
                shutil.copy2(src, inc / h)
                copied.append(h)
    print("[user] zlib: libz.a -> /usr/lib/tcc/libz.a (系统内 `tcc -lz` 可用)"
          + (f", 头: {', '.join(copied)}" if copied else ""))


def zlib_libs(arch: str):
    """给别的构建函数用：在链接命令里展开 `*zlib_libs(arch)` 即可引入 zlib。

    返回 list 而不是单个路径，是为了 zlib 不存在时能安静地展开成空
    （比如别人只想编个不带 zlib 的程序，不该因为这个函数直接炸）。
    """
    libz = BUILD_DIR / arch / "zlib" / "libz.a"
    return [str(libz)] if libz.exists() else []


# ==================== vendored 大件: git / openssl / curl ====================

# 三者的共性：上游源码 + 我们自己的构建脚本，产物落在 build/<arch>/<name>/。
# 编译期把 user/include 里的 inline 实现和**结构体布局**抄进了自己的 .o，
# 所以 libc 头一改三件全得重编（openssl 那次 FILE.buf 错位就是漏了这步）。
#
# 增量分两层，都不省：
#   1) 这里：libc ABI 哈希 + 构建脚本自身哈希 都没变、产物在 → 跳过脚本。
#      脚本也是"会影响产物的输入"，改了 NO_* 列表却不重编同样是静默复用旧 .o。
#   2) 脚本内部：各自还有更细的签名（Configure 参数 / CFLAGS / 桩库符号名单），
#      变了就 rm -rf 自己的 obj 目录整份重建。
# 上层跳过只省时间，正确性由下层保底。

def _external_lib(arch: str, name: str, marker: str, script_name: str,
                  elf_name: str, log_name: str, deps=(), also=(),
                  stages=("bin",)) -> str:
    """编一个 vendored 大件，返回本次缓存键（供下游当依赖传进来）。

    失败**不中断整体构建**：三件都是可选的，其余程序不该陪葬。失败时同时清掉
    缓存条目 —— 否则旧 ELF 会被下次 cache hit 当成新的用。

    stages 是脚本的阶段列表（脚本自己按 lib / bin 分入口）。历史上这里写死
    "bin"，而 openssl 的 libcrypto.a / libssl.a 只有 lib 阶段保证落盘 ——
    于是"openssl 编好了、curl 却找不到静态库"会互相卡死。所以阶段要能传进来；
    默认 ("bin",) 保持原有行为，只有 openssl 用 ("lib", "bin")。

    列表里**最后**一个阶段是判成功的那一步（要产出 elf_name），前面的阶段
    只看返回码：lib 阶段不产 elf，失败也让 bin 去报真正的错，别在这里抢戏。
    """
    src_dir = USER_DIR / name / "src"
    if not (src_dir / marker).exists():
        print(f"[user] {name}: no source at {src_dir}, skip")
        return ""

    script = ROOT / "scripts" / script_name
    if not script.exists():
        print(f"[user] {name}: 缺构建脚本 {script}, skip")
        return ""

    out_dir = BUILD_DIR / arch / name
    out_dir.mkdir(parents=True, exist_ok=True)
    elf = out_dir / elf_name

    # 阶段列表先定型，cache 命中与成功判定都要看"最后一步是不是 bin"：
    # 只有 bin 阶段该产 elf，--stages=lib（只要 .a）时没有 elf 是正常的。
    stages = tuple(stages) or ("bin",)
    last = stages[-1]
    expects_elf = last == "bin"

    abi = shared_deps_hash(arch)
    # stages 必须进签名：--stages=lib 只刷新 .a、不动 elf，若和 lib,bin 共用 key，
    # 下一次带 bin 跑就会 cache hit 用上**旧的** openssl.elf —— "改了没生效"。
    key = hashlib.sha256("\n".join(
        [arch, name, abi, file_sha256(script), *stages, *deps]).encode()).hexdigest()

    prev = get_arch_cache(arch).get("external", {}).get(name, {})
    # also=() 是"同一个脚本还会产出别的文件"的必检清单。典型是 git 的
    # git-remote-https.elf：脚本判定 https 开不开看的是 libcurl.a 在不在，
    # 而 libcurl.a 可能晚于 git 出现 —— 只看 git.elf 的话，那个回合编出来的
    # NO_CURL 版本会被永久 cache hit，https 永远"配了但不生效"。
    extra_ok = all((out_dir / x).exists() and (out_dir / x).stat().st_size > 0
                   for x in also)
    elf_ok = (not expects_elf) or (elf.exists() and elf.stat().st_size > 0)
    if prev.get("key") == key and elf_ok and extra_ok:
        tail = f"，{elf.stat().st_size} bytes" if expects_elf else ""
        print(f"[user] {name}: cache hit（ABI/脚本未变{tail}）")
        return key

    if prev.get("abi") and prev["abi"] != abi:
        print(f"[user] {name}: libc ABI 变了 → 全量重编")
    elif prev:
        print(f"[user] {name}: 脚本或上游依赖变了 → 全量重编")

    # RMIKU_LIBC_ABI 让脚本内部的签名与这里认同一份 ABI，免得两套算法各算各的
    env = os.environ.copy()
    env["RMIKU_LIBC_ABI"] = abi

    for st in stages[:-1]:
        # 中间阶段不看返回码，但也不吞输出 —— 缺口扫描、缺头告警都在它的 stdout 里。
        print(f"[user] {name}: 调用 scripts/{script_name} {arch} {st}")
        subprocess.run(["bash", str(script), arch, st], cwd=str(ROOT), env=env)

    print(f"[user] {name}: 调用 scripts/{script_name} {arch} {last}")
    proc = subprocess.run(["bash", str(script), arch, last], cwd=str(ROOT), env=env)

    # 只有最后一步是 bin 才该有 elf。--stages=lib（只要 .a，不链程序）是合法
    # 用法，那种情况下"没 elf"不是失败。
    # 注意不能用上面那个 elf_ok：那是**跑之前**的快照，首次构建时必然为 False，
    # 编译成功了也会被误判成失败。
    if proc.returncode != 0 or (expects_elf and not (
            elf.exists() and elf.stat().st_size > 0)):
        print(f"[user] {name}: ✗ 没编出来（其余程序照常构建）")
        print(f"[user] {name}:   完整日志 {out_dir / log_name}")
        print(f"[user] {name}:   单独重试: bash scripts/{script_name} {arch} {last}")
        if len(stages) > 1:
            print(f"[user] {name}:   完整阶段: {', '.join(stages)}"
                  f"（前几个阶段的输出在上面，日志各自的 *.log）")
        arch_cache = get_arch_cache(arch)
        arch_cache.setdefault("external", {}).pop(name, None)
        put_arch_cache(arch, arch_cache)
        return key

    # 主产物编出来了，但 also 里的副产物没有 —— 脚本自己只在告警后继续（比如
    # git 的 helper 链接失败时 git 本体仍可用），绝不能当成功缓存下来：
    # 一缓存，下次 cache hit，副产物永远不会再尝试生成，"配了 https 但永远
    # 不生效"。症状是 `git clone https://` 报 'remote-https' is not a git command。
    missing = [x for x in also if not (out_dir / x).is_file()
               or (out_dir / x).stat().st_size == 0]
    if missing:
        print(f"[user] {name}: ⚠ 主产物在，但缺副产物 {' '.join(missing)}")
        print(f"[user] {name}:   完整日志 {out_dir / log_name}"
              f"（git 的 helper 另有 make_helper.log）")
        print(f"[user] {name}:   不写缓存，下次构建会重试")
        arch_cache = get_arch_cache(arch)
        arch_cache.setdefault("external", {}).pop(name, None)
        put_arch_cache(arch, arch_cache)
        return key

    if expects_elf:
        print(f"[user] {name}: {elf_name} {elf.stat().st_size} bytes -> /bin/{name}")
    else:
        # --stages=lib：没有 elf 可报，就把落下来的 .a 报出来，否则这一段全静默。
        got = [x for x in also if (out_dir / x).is_file()]
        print(f"[user] {name}: 只跑到 {last}，产物 {' '.join(got) if got else '（无）'}")
    arch_cache = get_arch_cache(arch)
    arch_cache.setdefault("external", {})[name] = {"key": key, "abi": abi}
    put_arch_cache(arch, arch_cache)
    return key


def build_git(arch: str, curl_key: str = "") -> str:
    """git → /bin/git，有 libcurl.a 时连带 /bin/git-remote-https。

    https 是把 libcurl.a **静态链进** git 的，所以 curl 一重建（改 CA 路径、
    换 OpenSSL、改头文件），git 里那份就必须跟着重链 —— 否则同一台机器上
    curl 和 git 对同一份信任锚会给出不同行为，还极难查。把 curl 的缓存键
    当依赖塞进签名就是为这个。

    also= 里那个 helper 是另一半：脚本按"libcurl.a 在不在"决定开不开 https，
    而 libcurl.a 可能比 git.elf 晚出现。只认 git.elf 的话，先编出来的 NO_CURL
    版本会一直 cache hit，"配了 https 但永远不生效"。
    """
    libcurl = BUILD_DIR / arch / "curl" / "libcurl.a"
    return _external_lib(arch, "git", "git.c", "git_probe.sh",
                         "git.elf", "make_git.log",
                         deps=(curl_key,) if curl_key else (),
                         also=("git-remote-https.elf",) if libcurl.exists() else ())


def build_openssl(arch: str, stages=("lib", "bin")) -> str:
    """openssl → /bin/openssl，连带产出 libcrypto.a / libssl.a（curl 的硬前置）。

    also= 里那两个 .a 是**下游的硬前置**，不能只看 openssl.elf：只跑 bin 阶段
    时它们曾经不被落到 OUT（只有 lib 阶段落），于是 openssl cache hit、curl 却
    报"找不到 OpenSSL 静态库"，两者互相卡住。现在 bin 阶段也会落，这里再用
    cache-hit 条件兜一层。

    stages 默认 ("lib", "bin") 是双保险：lib 阶段保证 .a 一定落盘（哪怕脚本的
    bin 阶段是旧版、不落 .a），bin 阶段负责链 openssl.elf 并报真正的链接错误。
    嫌 lib 的缺口扫描吵就 --stages=bin，但那时 .a 只能靠 bin 阶段落。
    """
    return _external_lib(arch, "openssl", "Configure", "openssl_build.sh",
                         "openssl.elf", "make_bin.log",
                         also=("libcrypto.a", "libssl.a"),
                         stages=stages)


def build_curl(arch: str, openssl_key: str = "") -> str:
    """curl → /bin/curl。

    静态链接 openssl 的 libcrypto.a / libssl.a，所以 openssl 一重建就必须重链 ——
    把它的缓存键当依赖塞进签名里，openssl 变了 curl 自动跟着重来。
    """
    return _external_lib(arch, "curl", "configure", "curl_build.sh",
                         "curl.elf", "link.log", deps=(openssl_key,),
                         also=("libcurl.a",))


# ==================== TCC 构建(从 user/tcc/build_tcc.py 融合) ====================

def gen_tccdefs(src: Path, dst: Path):
    """等价于 TCC 官方 c2str: 把 include/tccdefs.h 逐字节转义为 C 字符串字面量序列。

    转义规则(与 TCC conftest.c 的 C2STR 分支一致):
      \\n  ->  \\\\n\"\\n\"    (换行结束当前字符串字面量, 下一行重新开引号)
      \"   ->  \\\\"
      \\\\  ->  \\\\\\\\
    其他字节原样(含非 ASCII, 按字节处理保证 UTF-8 注释不被破坏)。
    输出无前缀/后缀, tccpp.c 里 #include 后紧跟 ", -1);", 相邻字符串字面量
    自动拼接成一个 char*, 正好是 cstr_cat 的第二个参数。
    """
    data = src.read_bytes()
    out = bytearray(b'"')          # 开引号: 第一个字符串从这里开始(c2str 同款)
    for c in data:
        if c == 0x0A:            # \n
            out += b'\\n"\n"'
        elif c == 0x22:          # "
            out += b'\\"'
        elif c == 0x5C:          # backslash
            out += b'\\\\'
        else:
            out.append(c)
    out += b'"'                  # 补闭合引号: 最后一个字符串完整闭合(不依赖调用方)
    dst.write_bytes(out)


def build_tcc(arch: str):
    """交叉编译 TCC 0.9.28 → /programs/tcc(镜像 /bin/tcc)。

    策略(与 user/tcc/build_tcc.py 一致):
      - 编 TCC 主程序(tcc.c libtcc.c tccpp.c tccgen.c tccdbg.c tccelf.c
        tccasm.c tccrun.c + <arch>-gen/link/asm.c)
      - 链接用 crt0 + syscall.S + 标准 ldscript(与其它用户程序一致)
      - 部署 tcc.elf 到 build/<arch>/bin/ 并生成 tcc-sys 系统文件
        (crt1/crti/crtn + libc.a + libtcc1.a + runmain.o + include)
    TCC 0.9.28 源码只带 riscv64 后端(无 loongarch64-gen.c), loongarch64 跳过。
    """
    cfg = ARCH_CONFIG[arch]
    tcc_dir = USER_DIR / "tcc"
    tcc_src = tcc_dir / "tcc-src"

    arch_backend = {
        "riscv64": ["riscv64-gen.c", "riscv64-link.c", "riscv64-asm.c"],
        "loongarch64": ["loongarch64-gen.c", "loongarch64-link.c", "loongarch64-asm.c"],
    }
    if arch not in arch_backend:
        print(f"[user] tcc: TCC 0.9.28 has no {arch} backend (no {arch}-gen.c), skip")
        return

    files = [
        "tcc.c", "libtcc.c", "tccpp.c", "tccgen.c",
        "tccdbg.c", "tccelf.c", "tccasm.c", "tccrun.c",
        *arch_backend[arch],
    ]

    out = BUILD_DIR / arch / "tcc"
    out.mkdir(parents=True, exist_ok=True)

    # 0) 生成 tccdefs_.h(tccpp.c 编译必需)
    #    TCC 官方用 conftest.c(c2str 宿主程序)做文本级转换; tcc-src 里
    #    conftest.c 可能缺失(仓库未含), 用 python 等价实现, 逐字节转义,
    #    输出与 c2str 完全一致(tccpp.c #include 后作为 cstr_cat 字符串参数)。
    #    缓存: 仅在不存在时生成(增量构建)。注意——修改 include/tccdefs.h
    #    后必须删除 tccdefs_.h 强制重新生成, 否则旧版会一直嵌进 tcc.elf。
    defs_h = tcc_src / "tccdefs_.h"
    if not defs_h.exists():
        gen_tccdefs(tcc_src / "include" / "tccdefs.h", defs_h)
        print(f"[user][tcc] generated {defs_h}")

    objs = []
    # 1) crt0 / syscall 汇编
    for name, src in (("crt0", cfg["crt0"]), ("syscall", cfg["runtime"])):
        o = out / f"{name}.o"
        run([cfg["gcc"], *cfg["cflags"], "-c", src, "-o", o])
        objs.append(o)

    # 2) libc 运行时(string.c: memset/memcpy 等)
    string_o = out / "string.o"
    run([cfg["gcc"], *cfg["cflags"], "-fno-builtin", "-c", LIB_DIR / "string.c", "-o", string_o])
    objs.append(string_o)

    # 2b) 线程栈记录表(lib/thread.c): 跨 TU 必须唯一, 不能留在头里
    thread_o = out / "thread.o"
    run([cfg["gcc"], *cfg["cflags"], "-fno-builtin", "-c", LIB_DIR / "thread.c", "-o", thread_o])
    objs.append(thread_o)

    # 2c) 堆状态(lib/mem.c): 同上, malloc 在 A.o、free 在 B.o 时不能各有一份
    mem_o = out / "mem.o"
    run([cfg["gcc"], *cfg["cflags"], "-fno-builtin", "-c", LIB_DIR / "mem.c", "-o", mem_o])
    objs.append(mem_o)

    # 3) TCC 源码
    # 显式指定 TCC target(不依赖 __loongarch__/__riscv 自动检测,
    # 防止 tcc.h 分支缺失或工具链宏异常导致编成错误架构)
    tcc_target_def = {
        "riscv64": "-DTCC_TARGET_RISCV64",
        "loongarch64": "-DTCC_TARGET_LOONGARCH64",
    }.get(arch)
    cflags = [
        *cfg["cflags"],
        "-nostdlib", "-nostartfiles", "-static", "-no-pie",
        "-O2", "-fno-strict-aliasing", "-Wno-unused-result",
        "-fno-builtin",   # RmikuOS 的 strlen 等是头内联, 禁 gcc builtin 生成外部调用
        "-DONE_SOURCE=0",
    ]
    if tcc_target_def:
        cflags.append(tcc_target_def)
    cflags += [
        f"-I{INCLUDE_DIR}",   # RmikuOS libc 头
        f"-I{tcc_src}",       # TCC 自身头(tcc.h 等)
        f"-I{tcc_dir}",       # 手工 config.h
    ]

    # ---- 增量缓存:所有源文件 + cflags + tccdefs.h 的联合 SHA256 ----
    # 任何依赖变化 → 缓存失效全量重编;否则跳过 12 个 .o 的编译和链接
    h = hashlib.sha256()
    for f in files:
        p = tcc_src / f
        if p.exists():
            h.update(file_sha256(p).encode())
    for src in (cfg["crt0"], cfg["runtime"], LIB_DIR / "string.c",
                LIB_DIR / "thread.c", LIB_DIR / "mem.c",
                tcc_src / "include" / "tccdefs.h"):
        if Path(src).exists():
            h.update(file_sha256(src).encode())
    h.update(repr(cflags).encode())
    current_hash = h.hexdigest()

    arch_cache = get_arch_cache(arch)
    tcc_cache = arch_cache.get("tcc", {})
    elf = out / "tcc.elf"
    if tcc_cache.get("hash") == current_hash and elf.exists():
        print(f"[user][tcc] cache hit (SHA256 无变化), 跳过编译")
        # 确保镜像 bin/ 里有 tcc.elf(若被清理则补复制)
        bin_dir = BUILD_DIR / arch / "bin"
        bin_dir.mkdir(parents=True, exist_ok=True)
        if not (bin_dir / "tcc.elf").exists():
            shutil.copy(elf, bin_dir / "tcc.elf")
        # 系统文件被清时补生成
        sys_root = BUILD_DIR / arch / "tcc-sys" / "usr" / "lib" / "tcc"
        if not ((sys_root / "libc.a").exists() and (sys_root / "libtcc1.a").exists()):
            print(f"[user][tcc] sys 文件缺失, 补生成")
            build_tcc_sys_files(arch, tcc_dir, tcc_src)
        return

    for f in files:
        o = out / (Path(f).stem + ".o")
        run([cfg["gcc"], *cflags, "-c", tcc_src / f, "-o", o])
        objs.append(o)

    # 4) 链接
    elf = out / "tcc.elf"
    run([
        cfg["gcc"], *cfg["cflags"],
        "-nostdlib", "-nostartfiles", "-static", "-no-pie",
        "-Wl,--build-id=none", "-Wl,--no-relax",
        "-T", cfg["linker"], *objs, "-lgcc", "-o", elf,
    ])
    print(f"[user][tcc] OK: {elf}")

    # 5) 进镜像: mkfs_ext4.sh 收 build/<arch>/bin/*.elf -> /bin/tcc
    bin_dir = BUILD_DIR / arch / "bin"
    bin_dir.mkdir(parents=True, exist_ok=True)
    shutil.copy(elf, bin_dir / "tcc.elf")
    print(f"[user][tcc] installed -> {bin_dir / 'tcc.elf'}  (镜像 /bin/tcc)")

    # 6) TCC 系统文件(镜像 /usr/lib/tcc)
    build_tcc_sys_files(arch, tcc_dir, tcc_src)

    # 保存 SHA256 缓存(仅在全量编译成功后)
    arch_cache = get_arch_cache(arch)
    arch_cache["tcc"] = {"hash": current_hash}
    put_arch_cache(arch, arch_cache)


def build_tcc_sys_files(arch: str, tcc_dir: Path, tcc_src: Path):
    """生成 /usr/lib/tcc: crt1/crti/crtn + libc.a + libtcc1.a + runmain.o + include。"""
    cfg = ARCH_CONFIG[arch]
    sys_root = BUILD_DIR / arch / "tcc-sys" / "usr" / "lib" / "tcc"
    sys_root.mkdir(parents=True, exist_ok=True)

    # crt1.o = RmikuOS crt0(_start 调 main + 存 environ)
    run([cfg["gcc"], *cfg["cflags"], "-c", cfg["crt0"], "-o", sys_root / "crt1.o"])

    # crti.o / crtn.o: 空 init/fini 段对象(TCC 链接需要存在)
    empty_s = sys_root / "empty.S"
    empty_s.write_text(".section .init\n.section .fini\n", encoding="utf-8")
    for name in ("crti.o", "crtn.o"):
        run([cfg["gcc"], *cfg["cflags"], "-c", empty_s, "-o", sys_root / name])

    # libc.a = syscall.S + string.c + libc_extern.c
    # (printf 等是头内联; libc_extern 把公共 API 具现为真实符号,
    #  TCC 编译用户程序时对 static inline 生成外部引用, 靠它解析)
    for name, src in (("_syscall.o", cfg["runtime"]), ("_string.o", LIB_DIR / "string.c"),
                      ("_thread.o", LIB_DIR / "thread.c"), ("_mem.o", LIB_DIR / "mem.c")):
        run([cfg["gcc"], *cfg["cflags"], "-fno-builtin", "-c", src, "-o", sys_root / name])
    run([cfg["gcc"], *cfg["cflags"], "-fno-builtin", "-I", INCLUDE_DIR,
         "-c", tcc_dir / "libc_extern.c", "-o", sys_root / "_libc_extern.o"])
    run([cfg["ar"], "rcs", sys_root / "libc.a",
         sys_root / "_syscall.o", sys_root / "_string.o", sys_root / "_thread.o",
         sys_root / "_libc_extern.o", sys_root / "_mem.o"])

    # libtcc1.a: TCC 自身运行时(TCC 链接用户程序时用)
    libtcc1_o = sys_root / "libtcc1.o"
    run([cfg["gcc"], *cfg["cflags"], "-O2", "-fno-builtin", "-c",
         tcc_src / "lib" / "libtcc1.c", "-o", libtcc1_o])
    run([cfg["ar"], "rcs", sys_root / "libtcc1.a", libtcc1_o])
    # 合并 libgcc 的 long double(128位 quad)软浮点 + __clear_cache 进 libtcc1.a
    # (TCC 静态链接不自动链 libgcc, 而 _libc_extern.o / TCC 生成的代码会引用
    #  __extenddftf2/__multf3 等 -> 由 libtcc1.a 提供, 一次解决所有程序)
    _tcc_merge_libgcc(cfg, sys_root / "libtcc1.a", libtcc1_o)

    # runmain.o: tcc -run(JIT) 的启动 stub
    run([cfg["gcc"], *cfg["cflags"], "-O2", "-fno-builtin", "-c",
         tcc_src / "lib" / "runmain.c", "-o", sys_root / "runmain.o"])

    # include: RmikuOS 头(用户程序 include <stdio.h> 等)
    # dirs_exist_ok=True: 每次都同步, 否则头文件修改(如 math.h)不会进镜像
    inc = sys_root / "include"
    shutil.copytree(INCLUDE_DIR, inc, dirs_exist_ok=True)
    # TCC 编译器配套头: stddef/stdarg/stdbool/stdalign/tgmath/tccdefs 等
    # 缓存: 仅在不存在时拷贝(增量)。若改了 tcc-src/include 下的头,
    # 需删掉 sys_root/include 下对应文件才会同步进镜像。
    for h in sorted((tcc_src / "include").glob("*.h")):
        if not (inc / h.name).exists():
            shutil.copy(h, inc)
    # TCC 专用 stdint.h(裸机工具链无 libc 版)
    if not (inc / "stdint.h").exists():
        shutil.copy(tcc_dir / "stdint.h", inc)
    print(f"[user][tcc] system files -> {sys_root}")


def _tcc_merge_libgcc(cfg: dict, libtcc1_a: Path, libtcc1_o: Path):
    """把 host libgcc.a 中与 libtcc1.o/string.o 不重复的成员合并进 libtcc1.a。
    按符号清单去重, 全量提取多放无害。

    注意: nm 解析必须只收【全局符号】(类型字母大写: T/D/B/W/R...)。
    本地标签(如 libtcc1.o 的 'L0\\x01' / '.LC0', 类型小写 t)几乎每个
    libgcc 成员都定义, 若计入 have 会把所有成员误判为冲突而全部跳过
    (实测: libtcc1.a 56 成员但 tf 符号为 0)。"""
    import tempfile

    def global_syms(path):
        """nm --defined-only 输出中只保留全局符号(类型字母大写)"""
        r = subprocess.run([cfg["nm"], "--defined-only", str(path)],
                           capture_output=True, text=True)
        syms = set()
        for ln in r.stdout.splitlines():
            parts = ln.split()
            if len(parts) >= 3 and parts[-2][-1:].isupper():
                syms.add(parts[-1])
        return syms

    try:
        libgcc = subprocess.check_output(
            [cfg["gcc"], "-print-libgcc-file-name"], text=True).strip()
        if not os.path.exists(libgcc):
            print(f"[user][tcc] !! libgcc.a not found: {libgcc}")
            return
    except Exception as e:
        print(f"[user][tcc] !! cannot locate libgcc: {e}")
        return
    have = {"memcpy", "memset"}   # libtcc1.o 已有 + string.o 提供的
    have |= global_syms(libtcc1_o)
    tmp = tempfile.mkdtemp(prefix="rmiku-libgcc-")
    subprocess.run([cfg["ar"], "x", libgcc], cwd=tmp, check=True)
    picked = []
    for o in sorted(os.listdir(tmp)):
        if not o.endswith(".o"):
            continue
        syms = global_syms(os.path.join(tmp, o))
        if syms & have:      # 与 libtcc1.o / string.o 重复 -> 跳过(defined twice)
            continue
        picked.append(os.path.join(tmp, o))
    if picked:
        subprocess.run([cfg["ar"], "rcs", str(libtcc1_a), *picked], check=True)
        print(f"[user][tcc] libgcc merged into libtcc1.a: {len(picked)} objects")


def build_cpp_projects(arch: str):
    cpp_root = USER_DIR / "cpp"
    if not cpp_root.exists():
        print(f"[user] no cpp projects dir at {cpp_root}, skip")
        return
    for project_dir in sorted(cpp_root.iterdir()):
        if not project_dir.is_dir():
            continue
        out_dir = BUILD_DIR / arch / "cpp" / project_dir.name
        build_project_dir(arch, project_dir, out_dir, is_cpp=True)


def build_c_projects(arch: str):
    c_root = USER_DIR / "c"
    if not c_root.exists():
        print(f"[user] no c projects dir at {c_root}, skip")
        return
    for project_dir in sorted(c_root.iterdir()):
        if not project_dir.is_dir():
            continue
        out_dir = BUILD_DIR / arch / "c" / project_dir.name
        build_project_dir(arch, project_dir, out_dir, is_cpp=False)


def build_gcn(arch: str):
    gcn_dir = USER_DIR / "gcn"
    if not gcn_dir.exists():
        print(f"[user] no gcn dir at {gcn_dir}, skip")
        return
    out_dir = BUILD_DIR / arch / "gcn"
    build_project_dir(arch, gcn_dir, out_dir, is_cpp=True)


# ==================== Java 编译（带缓存） ====================

def build_java_projects(arch: str):
    java_root = USER_DIR / "java"
    if not java_root.exists():
        print(f"[user] no java projects dir at {java_root}, skip")
        return

    lib_dir = java_root / "rmiku"
    lib_files = sorted(lib_dir.glob("*.java")) if lib_dir.exists() else []

    arch_cache = get_arch_cache(arch)
    java_cache = arch_cache.get("java", {})

    for proj_dir in sorted(java_root.iterdir()):
        if not proj_dir.is_dir():
            continue
        if proj_dir == lib_dir:
            continue

        proj_name = proj_dir.name
        java_files = sorted(proj_dir.glob("*.java"))
        if not java_files:
            continue

        # 计算 hash：项目自身 + 公共库
        files_hash = {}
        for f in sorted(java_files + lib_files):
            files_hash[str(f.resolve())] = file_sha256(f)

        proj_key = str(proj_dir.resolve())
        cached = java_cache.get(proj_key)
        if cached and cached == files_hash:
            # 检查是否有 .class 产物
            if list(proj_dir.glob("*.class")):
                print(f"[user] cache hit java: {proj_name}")
                continue

        print(f"[user] javac {proj_name} ({len(java_files)} file(s)) ...")
        cmd = ["javac", "-d", str(proj_dir)]
        for f in lib_files + java_files:
            cmd.append(str(f))
        run(cmd)
        print(f"[user] java {proj_name} compiled")

        # 更新缓存
        arch_cache = get_arch_cache(arch)
        arch_cache.setdefault("java", {})[proj_key] = files_hash
        put_arch_cache(arch, arch_cache)


# ==================== Rust Workspace（cargo 自带增量） ====================

def build_rust_workspace(arch: str):
    if not RUST_DIR.exists():
        print(f"[user] no rust workspace at {RUST_DIR}, skip")
        return

    cfg = ARCH_CONFIG[arch]
    rust_target = cfg["rust_target"]
    linker = cfg["linker"].resolve()

    flags = [
        "-C", "relocation-model=static",
        "-C", f"link-arg=-T{linker}",
    ]
    for la in cfg.get("rust_link_args", []):
        flags += ["-C", f"link-arg={la}"]

    env = os.environ.copy()
    env["RUSTFLAGS"] = " ".join(flags) + " -A warnings"

    print(f"[user] building rust workspace ({arch}) ...")
    run_env(
        ["cargo", "build", "--release", "--target", rust_target],
        cwd=RUST_DIR,
        env=env,
    )

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
        name = prog_dir.name
        elf = rust_out / name
        if elf.exists():
            shutil.copy(elf, dst_dir / f"{name}.elf")
            print(f"[user] rust workspace program -> {name}")
            count += 1
        else:
            print(f"[user] warning: expected rust program not found: {elf}")

    print(f"[user] rust workspace: {count} program(s) built")


# ==================== main ====================

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("arch", choices=ARCH_CONFIG.keys())
    parser.add_argument("--objdump", action="store_true", help="print objdump for each app")
    parser.add_argument("--clean", action="store_true", help="force full rebuild (delete cache)")
    parser.add_argument("--stages", default="lib,bin",
                        help="vendored 大件（目前只有 openssl）跑哪些脚本阶段，"
                             "逗号分隔，可选 lib,bin / bin （默认 lib,bin）")
    args = parser.parse_args()

    # 阶段列表在别处也要用（只对 openssl 生效，curl/git 的脚本只有 bin 有意义）。
    # 打错一个字母如果静默变成"什么都不跑"，表现就是"编过了但产物没了"，最难查。
    stages = tuple(s.strip() for s in args.stages.split(",") if s.strip())
    bad = [s for s in stages if s not in ("lib", "bin")]
    if bad or not stages:
        print(f"[user] --stages 不认识: {args.stages}（可选 lib,bin / bin）",
              file=sys.stderr)
        sys.exit(2)

    if args.clean:
        if CACHE_FILE.exists():
            print(f"[user] removing cache: {CACHE_FILE}")
            CACHE_FILE.unlink()
        # 也可以选择性清空 build/<arch>，但保留 cache 文件本身已被删

    if not SRC_DIR.exists():
        print(f"missing {SRC_DIR}", file=sys.stderr)
        sys.exit(1)

    arch_build_dir = BUILD_DIR / args.arch
    if arch_build_dir.exists() and args.clean:
        print(f"[user] cleaning old build dir: {arch_build_dir}")
        shutil.rmtree(arch_build_dir)

    sources = collect_sources()
    if not sources:
        print(f"no .rs, .S or .c files found in {SRC_DIR}", file=sys.stderr)
        sys.exit(1)

    # zlib 必须先构建: build_one 链接时会 *zlib_libs(arch)
    build_zlib(args.arch)
    # .so 插件产物给 mkfs 拷进 /usr/lib（不能进 /tests，run_all 会执行它）
    build_shared_objects(args.arch)

    apps = []
    for app_id, (source, category) in enumerate(sources):
        app = build_one(args.arch, source, app_id, category)
        apps.append(app)

    build_rust_workspace(args.arch)
    build_cpp_projects(args.arch)
    build_c_projects(args.arch)
    build_tcc(args.arch)
    # tcc 的库目录刚建好, 这时才能把 libz.a 部署进去（系统内 `tcc foo.c -lz`）。
    # build_zlib 跑在最前面, 那时 tcc-sys 还不存在, deploy_libz 会直接返回。
    _libz = BUILD_DIR / args.arch / "zlib" / "libz.a"
    if _libz.exists():
        deploy_libz(args.arch, _libz)
    build_sqlite3(args.arch)
    # 顺序是有依赖的：openssl → curl → git。
    # git 要 https 就得链 libcurl.a，libcurl.a 又静态依赖 libssl/libcrypto；
    # 而 git_probe.sh 判断"curl 能不能用"就是看 libcurl.a 在不在。
    # 反过来先编 git，干净树上第一次必然编成 NO_CURL 版，要跑第二遍才生效 ——
    # 这种"配好了但没生效"以前在别的地方踩过，宁可把顺序钉死。
    _ssl_key = build_openssl(args.arch, stages=stages)
    _curl_key = build_curl(args.arch, _ssl_key)
    build_git(args.arch, _curl_key)
    build_gcn(args.arch)
    build_java_projects(args.arch)

    if args.objdump:
        cfg = ARCH_CONFIG[args.arch]
        for app in apps:
            elf = BUILD_DIR / args.arch / f"{app['id']}_{app['source'].stem}.elf"
            if elf.exists():
                run([cfg["objdump"], "-d", str(elf)])


if __name__ == "__main__":
    main()