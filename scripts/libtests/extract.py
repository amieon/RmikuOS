#!/usr/bin/env python3
"""从真实源文件里抽出被改动的函数，供宿主回归编译。

不复制实现：抽的是原文，所以测试永远跟着源码走，不会出现"测试和实现对不上"。
唯一允许的改动是把和宿主 libc 撞名的标识符改名（BUFSIZ/EOF/FILE），逻辑一字不动。
"""
import re
import sys
from pathlib import Path

USER = Path(__file__).resolve().parent.parent.parent / "user"


def grab(src: str, start_marker: str) -> str:
    """从 start_marker 开始，按花括号配平截出整个函数。"""
    i = src.index(start_marker)
    depth = 0
    started = False
    for j in range(i, len(src)):
        c = src[j]
        if c == "{":
            depth += 1
            started = True
        elif c == "}":
            depth -= 1
            if started and depth == 0:
                return src[i:j + 1]
    raise SystemExit(f"unbalanced braces after {start_marker!r}")


def derename(t: str) -> str:
    t = re.sub(r"\bBUFSIZ\b", "RK_BUFSIZ", t)
    t = re.sub(r"\bEOF\b", "RK_EOF", t)
    t = re.sub(r"\bFILE\b", "RK_FILE", t)
    return t


def main(out: Path) -> None:
    string_c = (USER / "lib" / "string.c").read_text(encoding="utf-8")
    file_h = (USER / "include" / "file.h").read_text(encoding="utf-8")

    memset_fn = grab(string_c, "void *memset(void *s, int c, unsigned long n)")
    memcpy_fn = grab(string_c, "void *memcpy(void *dst, const void *src, unsigned long n)")

    # 宿主是 LLP64（unsigned long 只有 4 字节），目标机是 LP64（8 字节）。
    # 按字搬的代码必须用 8 字节类型在宿主上跑，否则测的是"4 字节步长"这个
    # 根本不存在的实现。unsigned long 在 riscv64/loongarch64 上就等于 uint64_t。


    # fread 需要 BUFSIZ / 标志位 / FILE 结构体：从 file.h 里原样截。
    head_s = file_h.index("#define EOF")
    head_e = file_h.index("} FILE;") + len("} FILE;")
    file_defs = derename(file_h[head_s:head_e])
    fread_fn = derename(grab(file_h, "static inline size_t fread(void* ptr, size_t size, size_t nmemb, FILE* fp)"))

    text = (
        "/* 自动生成，勿手改：python3 scripts/libtests/extract.py <out> */\n"
        "#include <stdint.h>\n"
        + file_defs
        + "\n\n"
        + memset_fn
        + "\n\n"
        + memcpy_fn
        + "\n\n"
        + fread_fn
        + "\n"
    )
    # 整体替换：宿主 LLP64 的 unsigned long 只有 4 字节，8 字节按字搬必须用
    # uint64_t 才能测出真实步长（目标机上 unsigned long 就等于 uint64_t）。
    text = re.sub(r"\bunsigned long\b", "uint64_t", text)
    out.write_text(text, encoding="utf-8")
    print(f"[extract] {out}: FILE 定义 + memset/memcpy/fread")


if __name__ == "__main__":
    main(Path(sys.argv[1]) if len(sys.argv) > 1 else Path("_extracted.inc"))
