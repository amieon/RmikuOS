#!/usr/bin/env python3
"""
diag_miss_dist.py -- 验证 miss/late 分布形状（集中 vs 分散）。

假设: exp3 light 的 miss 集中在少数窗口(多数窗口 miss=0), exp4 的 miss
分散在多数窗口(每窗口零星 miss)。probe up 需 late_delta=0(窗口 miss=0),
所以 miss=0 窗口比例决定 AIMD 能否爬升。

统计每个 aimd run 的 D 行:
  - miss_delta 分布: miss=0 窗口比例 + miss 直方图(0,1,2,3,4,5+)
  - late_delta 分布: late=0 窗口比例 + late 分位数

用法:
    python3 diag_miss_dist.py ./logs/sched/aimd/sexp3_aimd.csv
    python3 diag_miss_dist.py ./logs/sched/dyn/sexp4_dyn.csv
    python3 diag_miss_dist.py exp3.csv exp4.csv   # 一次对比
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
            # 同时支持 exp3(有 config=) 和 exp4(无 config=) 的 # RUN 格式
            m = re.search(r'# RUN (?:config=(\w+) )?mode=(\w+)(?: alpha0=(\d+)| alpha=(\d+))? rep=(\d+)/\d+', line)
            if m:
                if cur and not in_warmup:
                    runs.append(cur)
                in_warmup = False
                config = m.group(1) or ""
                mode = m.group(2)
                alpha = int(m.group(3)) if m.group(3) else (int(m.group(4)) if m.group(4) else -1)
                rep = int(m.group(5))
                if mode == "aimd":
                    mode_key = f"aimd{alpha}"
                elif mode == "fixed":
                    mode_key = f"fixed{alpha}"
                else:
                    mode_key = mode  # exp4 已是 aimd0/fixed0 形式
                label = f"{config}/{mode_key}" if config else mode_key
                cur = {
                    "label": label, "config": config, "mode": mode_key,
                    "alpha0": alpha, "rep": rep, "D": [],
                }
                continue
            if in_warmup or cur is None:
                continue
            p = line.split(",")
            if p[0] == "D" and len(p) >= 6:
                try:
                    cur["D"].append({"win": int(p[1]), "jobs": int(p[3]),
                                     "miss": int(p[4]), "late": int(p[5])})
                except (IndexError, ValueError):
                    continue
    if cur and not in_warmup:
        runs.append(cur)
    return runs


def miss_hist(d_list):
    """miss_delta 直方图: [miss=0, =1, =2, =3, =4, >=5]"""
    h = [0] * 6
    for d in d_list:
        m = d["miss"]
        if m <= 0:
            h[0] += 1
        elif m == 1:
            h[1] += 1
        elif m == 2:
            h[2] += 1
        elif m == 3:
            h[3] += 1
        elif m == 4:
            h[4] += 1
        else:
            h[5] += 1
    return h


def main(path):
    runs = parse(path)
    print(f"[parsed] {len(runs)} runs from {path}\n")

    aimd_runs = [r for r in runs if r["mode"].startswith("aimd")]
    if not aimd_runs:
        print("no aimd runs found")
        return

    print("=" * 112)
    print("MISS DISTRIBUTION per aimd run  (miss=0 窗口多 => probe up 频繁)")
    print("=" * 112)
    hdr = (f"{'label':>16} {'rep':>3}  {'wins':>4}  {'miss=0':>6} {'m0%':>4}  "
           f"{'m=1':>4} {'m=2':>4} {'m=3':>4} {'m=4':>4} {'m>=5':>4}  "
           f"{'totmiss':>7} {'m/win>0':>7}")
    print(hdr)
    print("-" * 112)
    for r in sorted(aimd_runs, key=lambda x: (x["label"], x["rep"])):
        d = r["D"]
        nw = len(d)
        if nw == 0:
            continue
        h = miss_hist(d)
        m0_pct = h[0] / nw * 100
        tot_miss = sum(x["miss"] for x in d)
        wins_with_miss = nw - h[0]
        avg_miss_when_pos = tot_miss / wins_with_miss if wins_with_miss > 0 else 0
        print(f"{r['label']:>16} {r['rep']:>3}  {nw:>4}  {h[0]:>6} {m0_pct:>4.0f}  "
              f"{h[1]:>4} {h[2]:>4} {h[3]:>4} {h[4]:>4} {h[5]:>4}  "
              f"{tot_miss:>7} {avg_miss_when_pos:>7.1f}")

    print()
    print("=" * 112)
    print("LATE DISTRIBUTION per aimd run  (late=0 窗口比例 => probe up 前提)")
    print("=" * 112)
    hdr2 = (f"{'label':>16} {'rep':>3}  {'wins':>4}  {'late=0':>6} {'l0%':>4}  "
            f"{'mean':>6} {'p25':>5} {'p50':>5} {'p75':>5} {'p90':>5} {'max':>5}")
    print(hdr2)
    print("-" * 112)
    for r in sorted(aimd_runs, key=lambda x: (x["label"], x["rep"])):
        d = r["D"]
        nw = len(d)
        if nw == 0:
            continue
        lates = np.array([x["late"] for x in d])
        l0 = int((lates <= 0).sum())
        l0_pct = l0 / nw * 100
        print(f"{r['label']:>16} {r['rep']:>3}  {nw:>4}  {l0:>6} {l0_pct:>4.0f}  "
              f"{lates.mean():>6.1f} {np.percentile(lates, 25):>5.0f} "
              f"{np.percentile(lates, 50):>5.0f} {np.percentile(lates, 75):>5.0f} "
              f"{np.percentile(lates, 90):>5.0f} {lates.max():>5}")

    # 按 label 分组汇总 (跨 rep)
    print()
    print("=" * 112)
    print("SUMMARY by config/mode (all reps combined)")
    print("=" * 112)
    print(f"{'label':>16}  {'miss=0%':>7}  {'late=0%':>7}  {'late_p50':>8}  {'miss/win>0':>10}  {'verdict':>16}")
    print("-" * 80)
    by_label = defaultdict(list)
    for r in aimd_runs:
        by_label[r["label"]].append(r)
    for label in sorted(by_label):
        reps = by_label[label]
        all_d = [x for r in reps for x in r["D"]]
        if not all_d:
            continue
        nw = len(all_d)
        m0 = sum(1 for x in all_d if x["miss"] <= 0)
        l0 = sum(1 for x in all_d if x["late"] <= 0)
        lates = np.array([x["late"] for x in all_d])
        tot_miss = sum(x["miss"] for x in all_d)
        wins_with_miss = nw - m0
        avg_miss = tot_miss / wins_with_miss if wins_with_miss > 0 else 0
        m0_pct = m0 / nw * 100
        l0_pct = l0 / nw * 100
        verdict = "集中(probe好)" if m0_pct > 50 else "分散(probe差)"
        print(f"{label:>16}  {m0_pct:>7.0f}  {l0_pct:>7.0f}  {np.percentile(lates, 50):>8.0f}  "
              f"{avg_miss:>10.1f}  {verdict:>16}")


if __name__ == "__main__":
    if len(sys.argv) < 2:
        print(f"Usage: {sys.argv[0]} <csv> [<csv2> ...]")
        sys.exit(1)
    for p in sys.argv[1:]:
        main(p)
        print("\n")
