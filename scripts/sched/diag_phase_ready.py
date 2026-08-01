#!/usr/bin/env python3
"""
diag_phase_ready.py -- 验证 phased 机制是否生效。

现象: aimd50/100 在 L1(轻负载段)就下降, 且 L/H 的 late 分布几乎一样。
疑点: W 行里 ai 的 ready_threads=26, 但 light_active=2 时 L 段应该只有 2 个活跃。
=> 如果 L 段 ready≈26(没降), 说明 phased 没生效, L/H 负载一样, 这就是 L 段不轻的根因。

统计每个相位 ai/log/ctrl 的 ready_threads 和 run_delta 平均值:
  - ai ready: 若 phased 生效, L 段≈light_active, H 段≈全线程
  - 若 L/H ready 一样, phased 没生效

用法:
    python3 diag_phase_ready.py ./logs/sched/dyn/sexp4_dyn.csv
"""
import sys
import re
from collections import defaultdict
import numpy as np


def parse(path):
    runs = []
    cur = None
    in_warmup = False
    with open(path) as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            if line.startswith("# WARMUP"):
                if cur and not in_warmup:
                    runs.append(cur)
                in_warmup = True
                cur = None
                continue
            m = re.search(r'# RUN mode=(\w+) alpha0=(\d+) rep=(\d+)/\d+', line)
            if m:
                if cur and not in_warmup:
                    runs.append(cur)
                in_warmup = False
                cur = {"mode": m.group(1), "W": []}
                continue
            if in_warmup or cur is None:
                continue
            p = line.split(",")
            if p[0] == "W" and len(p) >= 8:
                try:
                    cur["W"].append({"win": int(p[1]), "name": p[4],
                                     "run": int(p[5]), "eff": int(p[6]), "ready": int(p[7])})
                except (IndexError, ValueError):
                    continue
    if cur and not in_warmup:
        runs.append(cur)
    return runs


def main(path):
    runs = parse(path)
    print(f"[parsed] {len(runs)} runs\n")

    max_win = 0
    for r in runs:
        for w in r["W"]:
            if w["win"] > max_win:
                max_win = w["win"]
    bounds = [0, max_win // 4, max_win // 2, 3 * max_win // 4, max_win]
    print(f"[phase] max_win={max_win}, bounds={bounds}\n")

    phase_names = ["L1", "H1", "L2", "H2"]

    # 每个相位 每个组 的 ready/run 平均值 (所有 run 合计)
    print("=" * 100)
    print("READY_THREADS & RUN_DELTA by phase x group (all runs)")
    print("  ai: phased 生效则 L 段 ready 小(≈light_active), H 段 ready 大(≈全线程)")
    print("  若 L/H 的 ai ready 都≈26, 说明 phased 没生效, L 段不轻")
    print("=" * 100)
    print(f"{'phase':>6} {'group':>6}  {'n':>5}  {'ready_mean':>10} {'ready_min':>9} {'ready_max':>9}  {'run_mean':>8}")
    print("-" * 100)
    for ph in range(4):
        for name in ["ctrl", "ai", "log"]:
            readys = []
            runs_d = []
            for r in runs:
                for w in r["W"]:
                    if w["name"] == name and bounds[ph] <= w["win"] < bounds[ph + 1]:
                        readys.append(w["ready"])
                        runs_d.append(w["run"])
            if not readys:
                continue
            arr = np.array(readys)
            rarr = np.array(runs_d)
            print(f"{phase_names[ph]:>6} {name:>6}  {len(arr):>5}  {arr.mean():>10.1f} "
                  f"{arr.min():>9} {arr.max():>9}  {rarr.mean():>8.1f}")
        print()

    # 逐 window 看 ai ready 在 L1->H1 边界的变化 (取 aimd0 rep1)
    print("=" * 100)
    print("ai ready_threads 逐 window (aimd0 rep1, L1末尾->H1开头, win 215-235)")
    print("  若 phased 生效: L1 末尾 ready≈light_active, H1 开头 ready 跳到≈全线程")
    print("=" * 100)
    for r in runs:
        if r["mode"] == "aimd0":
            b1 = bounds[1]
            lo, hi = b1 - 10, b1 + 12
            ai_w = {w["win"]: w for w in r["W"] if w["name"] == "ai"}
            print(f"{'win':>5} {'ready':>5} {'run':>5} {'eff':>5}  {'phase':>5}")
            for win in range(lo, hi):
                w = ai_w.get(win)
                if w:
                    ph = "L1" if win < b1 else "H1"
                    print(f"{win:>5} {w['ready']:>5} {w['run']:>5} {w['eff']:>5}  {ph:>5}")
            break


if __name__ == "__main__":
    if len(sys.argv) < 2:
        print(f"Usage: {sys.argv[0]} <sexp4_dyn.csv>")
        sys.exit(1)
    main(sys.argv[1])
