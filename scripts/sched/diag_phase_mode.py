#!/usr/bin/env python3
"""
diag_phase_mode.py -- 分相位 x 分 mode 看 aimd 在 L 段为什么下降。

现象: aimd50/100 在 L1(轻负载段)就开始下降, 不是猜的"L 段轻所以爬升"。
假设: 高 α 时即使负载轻, ai 通过 eff_tickets 机制占优 => ctrl late 高 => down。

打印:
  1. 分相位 x 分 mode 的 late 分布 + down 次数
  2. L1 段 aimd100 rep1 逐 window: α / action / late / ctrl_eff / ai_eff / ctrl_run / ai_run
     (看 α=100 时 ai 是否占优、ctrl 是否被抢占、late 是否高)

用法:
    python3 diag_phase_mode.py ./logs/sched/dyn/sexp4_dyn.csv
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
                cur = {"mode": m.group(1), "a0": int(m.group(2)), "rep": int(m.group(3)),
                       "A": [], "D": [], "W": []}
                continue
            if in_warmup or cur is None:
                continue
            p = line.split(",")
            try:
                if p[0] == "A" and len(p) >= 5:
                    cur["A"].append({"win": int(p[1]), "after": int(p[3]), "action": p[4]})
                elif p[0] == "D" and len(p) >= 6:
                    cur["D"].append({"win": int(p[1]), "late": int(p[5]), "miss": int(p[4])})
                elif p[0] == "W" and len(p) >= 8:
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
        for a in r["A"]:
            if a["win"] > max_win:
                max_win = a["win"]
    bounds = [0, max_win // 4, max_win // 2, 3 * max_win // 4, max_win]
    print(f"[phase] max_win={max_win}, bounds={bounds}\n")

    phase_names = ["L1", "H1", "L2", "H2"]
    modes = ["aimd0", "aimd50", "aimd100"]

    # 分相位 x 分 mode 的 late 分布 + down
    print("=" * 100)
    print("LATE by phase x mode (all reps)  --  aimd50/100 在 L1 的 late 高吗?")
    print("=" * 100)
    print(f"{'phase':>6} {'mode':>8}  {'n':>4}  {'mean':>6} {'p50':>5} {'p90':>5} {'>=25':>5} {'down':>4} {'up':>4}")
    print("-" * 100)
    for ph in range(4):
        for mode in modes:
            lates = []
            downs = ups = 0
            for r in runs:
                if r["mode"] != mode:
                    continue
                for d in r["D"]:
                    if bounds[ph] <= d["win"] < bounds[ph + 1]:
                        lates.append(d["late"])
                for a in r["A"]:
                    if bounds[ph] <= a["win"] < bounds[ph + 1]:
                        if a["action"] == "down":
                            downs += 1
                        elif a["action"] == "up":
                            ups += 1
            if not lates:
                continue
            arr = np.array(lates)
            ge25 = int((arr >= 25).sum())
            print(f"{phase_names[ph]:>6} {mode:>8}  {len(arr):>4}  {arr.mean():>6.1f} "
                  f"{np.percentile(arr, 50):>5.0f} {np.percentile(arr, 90):>5.0f} "
                  f"{ge25:>5} {downs:>4} {ups:>4}")
        print()

    # L1 段 aimd100 rep1 逐 window
    print("=" * 100)
    print("L1 aimd100 rep1 逐 window (前 40): α/action/late/eff/run")
    print("  看 α=100 时 ai eff 是否>ctrl (ai占优), ctrl run 是否被压低, late 是否高")
    print("=" * 100)
    for r in runs:
        if r["mode"] == "aimd100" and r["rep"] == 1:
            a_map = {a["win"]: a for a in r["A"]}
            d_map = {d["win"]: d for d in r["D"]}
            # W 行按 win 分组: name -> (eff, run)
            w_map = defaultdict(dict)
            for w in r["W"]:
                w_map[w["win"]][w["name"]] = (w["eff"], w["run"], w["ready"])
            l1_wins = sorted([w for w in a_map if w < bounds[1]])[:40]
            print(f"{'win':>4} {'α':>4} {'act':>5} {'late':>5} | {'c_eff':>5} {'a_eff':>5} {'c_run':>5} {'a_run':>5} {'a_rdy':>5}")
            print("-" * 70)
            for w in l1_wins:
                a = a_map.get(w, {})
                d = d_map.get(w, {})
                cw = w_map.get(w, {}).get("ctrl", (-1, -1, -1))
                aw = w_map.get(w, {}).get("ai", (-1, -1, -1))
                print(f"{w:>4} {a.get('after', -1):>4} {a.get('action', '?'):>5} {d.get('late', -1):>5} | "
                      f"{cw[0]:>5} {aw[0]:>5} {cw[1]:>5} {aw[1]:>5} {aw[2]:>5}")
            break


if __name__ == "__main__":
    if len(sys.argv) < 2:
        print(f"Usage: {sys.argv[0]} <sexp4_dyn.csv>")
        sys.exit(1)
    main(sys.argv[1])
