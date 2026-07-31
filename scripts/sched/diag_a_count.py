#!/usr/bin/env python3
"""
diag_a_count.py -- 诊断：每个 run 的 A 行数 vs S 行数。

S 行每 window 末尾固定输出一条（sl_run 循环末尾 printf S）。
如果 sl_policy_aimd 真的每 window 无条件输出 A，则 A 行数应 ≈ S 行数。
若 A << S，说明实际并未每 window 输出 A（编译版本与源码不符，或逻辑有漏）。

用法:
    python3 ./scripts/sched/diag_a_count.py ./logs/sched/aimd/sexp3_aimd.csv
"""
import sys
import re
from collections import defaultdict


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

            m = re.search(r'# RUN config=(\w+) mode=(\w+)(?: alpha0=(\d+)| alpha=(\d+)) rep=(\d+)/\d+', line)
            if m:
                if cur and not in_warmup:
                    runs.append(cur)
                in_warmup = False
                config = m.group(1)
                mode = m.group(2)
                alpha = int(m.group(3)) if m.group(3) else int(m.group(4))
                rep = int(m.group(5))
                mode_key = f"aimd{alpha}" if mode == "aimd" else f"fixed{alpha}"
                cur = {
                    "config": config, "mode": mode_key, "alpha0": alpha, "rep": rep,
                    "A": 0, "S": 0, "W": 0, "D": 0,
                    "actions": defaultdict(int),
                    "A_wins": [], "S_wins": [],
                }
                continue

            if in_warmup or cur is None:
                continue

            p = line.split(",")
            tag = p[0]
            if tag == "A" and len(p) >= 5:
                cur["A"] += 1
                cur["actions"][p[4]] += 1
                cur["A_wins"].append(int(p[1]))
            elif tag == "S" and len(p) >= 5:
                cur["S"] += 1
                cur["S_wins"].append(int(p[1]))
            elif tag == "W" and len(p) >= 8:
                cur["W"] += 1
            elif tag == "D" and len(p) >= 6:
                cur["D"] += 1

    if cur and not in_warmup:
        runs.append(cur)
    return runs


def main(path):
    runs = parse(path)
    print(f"[parsed] {len(runs)} formal runs (warmup discarded)\n")

    # 表 1：每个 run 的 A/S/W 计数 + A/S 比例 + actions
    print("=" * 110)
    print("TABLE 1: A vs S count per run  (S = one per window, A should match if every-window output)")
    print("=" * 110)
    hdr = (f"{'config':>8} {'mode':>8} {'a0':>4} {'rep':>3}  "
           f"{'A':>5} {'S':>5} {'W':>5}  {'A/S':>6}  {'actions':>40}")
    print(hdr)
    print("-" * 110)
    for r in sorted(runs, key=lambda x: (x["config"], x["mode"], x["rep"])):
        ratio = f"{r['A']/r['S']:.2f}" if r["S"] > 0 else "N/A"
        acts = "/".join(f"{k}:{v}" for k, v in sorted(r["actions"].items())) if r["actions"] else "-"
        print(f"{r['config']:>8} {r['mode']:>8} {r['alpha0']:>4} {r['rep']:>3}  "
              f"{r['A']:>5} {r['S']:>5} {r['W']:>5}  {ratio:>6}  {acts:>40}")

    # 表 2：AIMD run 的 A/S window 范围对比 + 缺失 window 数
    print()
    print("=" * 110)
    print("TABLE 2: AIMD runs -- A window range vs S window range, missing-A windows")
    print("=" * 110)
    hdr2 = (f"{'config':>8} {'mode':>8} {'rep':>3}  "
            f"{'A_cnt':>5} {'A_min':>5} {'A_max':>5}  "
            f"{'S_cnt':>5} {'S_min':>5} {'S_max':>5}  "
            f"{'miss_win':>8}  {'verdict':>10}")
    print(hdr2)
    print("-" * 110)
    for r in sorted(runs, key=lambda x: (x["config"], x["mode"], x["rep"])):
        if not r["mode"].startswith("aimd"):
            continue
        a_set = set(r["A_wins"])
        s_set = set(r["S_wins"])
        a_min = min(a_set) if a_set else -1
        a_max = max(a_set) if a_set else -1
        s_min = min(s_set) if s_set else -1
        s_max = max(s_set) if s_set else -1
        missing = s_set - a_set   # S 有但 A 没有的 window
        verdict = "EVERY-WIN" if not missing else "GAPS!"
        print(f"{r['config']:>8} {r['mode']:>8} {r['rep']:>3}  "
              f"{r['A']:>5} {a_min:>5} {a_max:>5}  "
              f"{r['S']:>5} {s_min:>5} {s_max:>5}  "
              f"{len(missing):>8}  {verdict:>10}")

        # 如果有 gap，打印前 10 个缺失的 window
        if missing and len(missing) <= 200:
            miss_sorted = sorted(missing)
            show = miss_sorted[:10]
            tail = f" ... (+{len(miss_sorted)-10} more)" if len(miss_sorted) > 10 else ""
            print(f"{'':>8} {'':>8} {'':>3}  missing A at windows: {show}{tail}")

    # 汇总
    print()
    print("=" * 110)
    print("SUMMARY")
    print("=" * 110)
    aimd_runs = [r for r in runs if r["mode"].startswith("aimd")]
    all_match = all(r["A"] == r["S"] for r in aimd_runs)
    print(f"AIMD runs: {len(aimd_runs)}")
    print(f"A == S (every-window output): {'YES' if all_match else 'NO'}")
    if not all_match:
        mism = [(r["config"], r["mode"], r["rep"], r["A"], r["S"]) for r in aimd_runs if r["A"] != r["S"]]
        print(f"  mismatched runs: {len(mism)}")
        for cfg, mode, rep, a, s in mism[:10]:
            print(f"    {cfg:>8} {mode:>8} rep{rep}: A={a} S={s} (diff={s-a})")


if __name__ == "__main__":
    if len(sys.argv) < 2:
        print(f"Usage: {sys.argv[0]} <sexp3_aimd.csv>")
        sys.exit(1)
    main(sys.argv[1])
