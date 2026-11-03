#!/usr/bin/env python3
"""从 user/include/net.h 抽出 setsockopt() 原文，供宿主回归编译。

不复制实现 —— 复制出来的测试只能证明"副本对"，改了真代码它照样绿。
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SRC = ROOT / "user" / "include" / "net.h"


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: extract_sockopt.py <out.inc>", file=sys.stderr)
        return 2

    text = SRC.read_text(encoding="utf-8")
    m = re.search(r"^static inline int setsockopt\(.*?^}\n", text, re.S | re.M)
    if not m:
        print("[sockopt] 在 net.h 里没找到 setsockopt 定义（签名变了？）", file=sys.stderr)
        return 1

    out = Path(sys.argv[1])
    out.write_text(
        "/* 自动生成，勿手改：python3 scripts/libtests/extract_sockopt.py <out> */\n"
        + m.group(0),
        encoding="utf-8",
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
