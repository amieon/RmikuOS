#!/usr/bin/env python3
"""
diag_alpha_traj.py -- 打印指定 run 的 alpha_traj 实际值。

验证矛盾: exp3 light aimd0 summary 说 α_steady=60, 但 diag_a_count 显示
单 rep up 只有 27 次(INC=1 最多爬到 27)。看 alpha_traj 的 after 值到底
是真爬到 60, 还是 stat 脚本算错 / up 统计漏了。

用法:
    python3 diag_alpha_traj.py ./logs/sched/aimd/sexp3_aimd.csv light 0
    python3 diag_alpha_traj.py ./logs/sched/aimd/sexp3_aimd.csv heavy 0
"""
import sys
import re
import numpy as np


def main():
    if len(sys.argv) < 4:
        print(f"Usage: {sys.argv[0]} <csv> <config> <alpha0>")
        print(f"  e.g: {sys.argv[0]} sexp3_aimd.csv light 0")
        sys.exit(1)
    path, want_cfg, want_a0 = sys.argv[1], sys.argv[2], int(sys.argv[3])

    runs = []
    cur = None
    in_warmup = False
    with open(path) as f:
        for line in f:
            line = line.strip()
            if line.startswith("# WARMUP"):
                if cur and not in_warmup:
                    runs.append(cur)
                in_warmup = True
                cur = None
                continue
            m = re.search(r'# RUN config=(\w+) mode=(\w+) alpha0=(\d+) rep=(\d+)/\d+', line)
            if m:
                if cur and not in_warmup:
                    runs.append(cur)
                in_warmup = False
                cur = {"config": m.group(1), "mode": m.group(2),
                       "a0": int(m.group(3)), "rep": int(m.group(4)), "A": []}
                continue
            if in_warmup or cur is None:
                continue
            p = line.split(",")
            if p[0] == "A" and len(p) >= 5:
                try:
                    cur["A"].append({"win": int(p[1]), "before": int(p[2]),
                                     "after": int(p[3]), "action": p[4]})
                except (IndexError, ValueError):
                    continue
    if cur and not in_warmup:
        runs.append(cur)

    for r in runs:
        if r["config"] == want_cfg and r["mode"] == "aimd" and r["a0"] == want_a0:
            afters = [a["after"] for a in r["A"]]
            if not afters:
                continue
            n = len(afters)
            half = n // 2
            ups = sum(1 for a in r["A"] if a["action"] == "up")
            downs = sum(1 for a in r["A"] if a["action"] == "down")
            print(f"\n=== {want_cfg} aimd{want_a0} rep{r['rep']} ===")
            print(f"  A lines={n}  up={ups} down={downs}")
            print(f"  after: min={min(afters)} max={max(afters)} last={afters[-1]}")
            print(f"  前半段mean={np.mean(afters[:half]):.1f}  后半段mean(=α_steady)={np.mean(afters[half:]):.1f}")
            print(f"  first 30 after: {afters[:30]}")
            print(f"  last 30 after:  {afters[-30:]}")


if __name__ == "__main__":
    main()
