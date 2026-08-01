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


ROOT = Path(__file__).resolve().parent.parent
USER_DIR = ROOT / "user"
SRC_DIR = USER_DIR / "src"
TESTS_DIR = USER_DIR / "tests"
SAMPLES_DIR = USER_DIR / "samples"
SCHED_DIR = USER_DIR / "sched"
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
        "objcopy": "riscv64-unknown-elf-objcopy",
        "objdump": "riscv64-unknown-elf-objdump",
        "linker": USER_DIR / "linker-riscv64.ld",
        "runtime": LIB_DIR / "syscall_riscv64.S",
        "crt0": LIB_DIR / "crt0_riscv64.S",
        "rust_target": "riscv64gc-unknown-none-elf",
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
        "objcopy": "loongarch64-unknown-linux-gnu-objcopy",
        "objdump": "loongarch64-unknown-linux-gnu-objdump",
        "linker": USER_DIR / "linker-loongarch64.ld",
        "crt0": LIB_DIR / "crt0_loongarch64.S",
        "runtime": LIB_DIR / "syscall_loongarch64.S",
        "rust_target": "loongarch64-unknown-none",
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

# ==================== 缓存工具 ====================

def shared_deps_hash(arch: str) -> str:
    """
    计算所有共享依赖的联合 hash。
    任何一个文件变动 → 所有用户态二进制缓存失效。
    覆盖:
      - include/ 下所有头文件 (user.h 等)
      - lib/ 下 string.c, cpp_runtime.cpp, crt0, runtime syscall
      - 链接脚本
      - user/cpp_runtime/ 下头文件 (C++ 项目用)
    """
    cfg = ARCH_CONFIG[arch]
    h = hashlib.sha256()

    dep_paths = []

    # 1) 公共头文件
    if INCLUDE_DIR.exists():
        dep_paths += sorted(INCLUDE_DIR.glob("*.h"))

    # 2) 公共 C/C++ 库源文件
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

    return h.hexdigest()

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

    link_objects = []
    if source.suffix == ".c":
        link_objects.append(str(crt0_obj))
    link_objects.append(str(obj))
    if source.suffix == ".c":
        link_objects.append(str(runtime_obj))
    if has_string:
        link_objects.append(str(string_obj))

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

    link_cmd = [
        cfg["gxx"], *cfg["cflags"],
        "-nostdlib", "-nostartfiles", "-static", "-no-pie",
        "-Wl,--build-id=none", *cfg["ldflags"],
        "-T", str(cfg["linker"]),
        str(crt0_obj), str(obj), str(runtime_obj), str(cpprt_obj),
    ]
    if has_string:
        link_cmd.append(str(string_obj))
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
        if cpprt_obj and cpprt_obj.exists():
            link_objs.append(str(cpprt_obj))

        linker = cfg["gxx"] if is_cpp else cfg["gcc"]
        run([linker, *cfg["cflags"],
             "-nostdlib", "-nostartfiles", "-static", "-no-pie",
             "-Wl,--build-id=none", *cfg["ldflags"],
             "-T", str(cfg["linker"]),
             *link_objs,
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

    注意：本函数编译 sqlite3.c + shell.c + rmiku_vfs.c + rmiku_shims.c，
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
        "rmiku_shims": src_dir / "rmiku_shims.c",
        "sqlite_main": src_dir / "sqlite_main.c",
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

    objs = {}
    for name, src in sources.items():
        if not src.exists():
            continue
        obj = out_dir / f"{name}.o"
        run([cfg["gcc"], *cfg["cflags"], *sqlite_cflags, *common_flags, "-c",
             str(src), "-o", str(obj)])
        objs[name] = str(obj)

    def link(elf_name, entry_obj):
        """链接一个可执行文件: crt0 + sqlite3.o + 入口 + vfs + shims + 运行时"""
        link_objs = [str(crt0_obj), objs["sqlite3"], entry_obj,
                     objs["rmiku_vfs"], objs["rmiku_shims"], str(runtime_obj)]
        if string_obj:
            link_objs.append(str(string_obj))
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

    # sqlite3.elf = 交互式 shell; sqlite3_probe.elf = 原落盘探针
    link("sqlite3.elf", objs["shell"])
    link("sqlite3_probe.elf", objs["sqlite_main"])

    arch_cache = get_arch_cache(arch)
    arch_cache["sqlite3"] = files_hash
    put_arch_cache(arch, arch_cache)


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
    args = parser.parse_args()

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

    apps = []
    for app_id, (source, category) in enumerate(sources):
        app = build_one(args.arch, source, app_id, category)
        apps.append(app)

    build_rust_workspace(args.arch)
    build_cpp_projects(args.arch)
    build_c_projects(args.arch)
    build_sqlite3(args.arch)
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