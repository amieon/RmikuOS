#!/usr/bin/env python3
"""
diag_exp4_aimd.py -- 诊断 exp4 AIMD 的 probe up 触发情况。

统计每个 AIMD run 的:
  - actions 分布 (up/down/gray/cool/hold)
  - late_delta 分布 (从 D 行): late<=0 / 0<late<50 / late>=50
  - 分相位 (L1/H1/L2/H2) 的 late 分布和 actions

用于回答: 为什么 exp4 L 段 aimd0 只爬到 30, 而 exp3 light 能爬到 60?
probe up 条件: late<=safe_lateness(0) && safe_windows>=1
=> late<=0 才能触发 up, late<=0 的频率决定 AIMD 能否爬升

用法:
    python3 ./scripts/sched/diag_exp4_aimd.py ./logs/sched/dyn/sexp4_dyn.csv
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
            m = re.search(r'# RUN mode=(\w+) alpha0=(\d+) rep=(\d+)/\d+', line)
            if m:
                if cur and not in_warmup:
                    runs.append(cur)
                in_warmup = False
                cur = {
                    "mode": m.group(1), "alpha0": int(m.group(2)), "rep": int(m.group(3)),
                    "A": [], "D": [], "S": [],
                }
                continue
            if in_warmup or cur is None:
                continue
            p = line.split(",")
            tag = p[0]
            try:
                if tag == "A" and len(p) >= 5:
                    cur["A"].append({"win": int(p[1]), "before": int(p[2]),
                                     "after": int(p[3]), "action": p[4]})
                elif tag == "D" and len(p) >= 6:
                    cur["D"].append({"win": int(p[1]), "late": int(p[5])})
                elif tag == "S" and len(p) >= 5:
                    cur["S"].append({"win": int(p[1])})
            except (IndexError, ValueError):
                continue
    if cur and not in_warmup:
        runs.append(cur)
    return runs


def phase_of(win, bounds):
    """0=L1, 1=H1, 2=L2, 3=H2"""
    for i in range(4):
        if bounds[i] <= win < bounds[i + 1]:
            return i
    return 3


def main(path):
    runs = parse(path)
    print(f"[parsed] {len(runs)} runs (warmup discarded)")

    # 动态相位边界
    max_win = 0
    for r in runs:
        for s in r["S"]:
            if s["win"] > max_win:
                max_win = s["win"]
    bounds = [0, max_win // 4, max_win // 2, 3 * max_win // 4, max_win]
    print(f"[phase] max_win={max_win}, bounds={bounds}\n")

    aimd_runs = [r for r in runs if r["mode"].startswith("aimd")]
    phase_names = ["L1", "H1", "L2", "H2"]

    # 表 1: 每个 AIMD run 的 actions + late 分布
    print("=" * 105)
    print("TABLE 1: AIMD runs -- actions & late_delta distribution")
    print("  (late<=0 => 可触发 probe up; 0<late<50 => gray 干等; late>=50 => 退避)")
    print("=" * 105)
    hdr = (f"{'mode':>8} {'a0':>4} {'rep':>3}  "
           f"{'up':>4} {'down':>4} {'gray':>5} {'cool':>4} {'hold':>4}  "
           f"{'late<=0':>7} {'0<lt<50':>7} {'lt>=50':>6}  {'probe%':>6}  {'a_end':>5}")
    print(hdr)
    print("-" * 105)
    for r in sorted(aimd_runs, key=lambda x: (x["mode"], x["rep"])):
        acts = defaultdict(int)
        for a in r["A"]:
            acts[a["action"]] += 1
        le0 = sum(1 for d in r["D"] if d["late"] <= 0)
        mid = sum(1 for d in r["D"] if 0 < d["late"] < 50)
        ge50 = sum(1 for d in r["D"] if d["late"] >= 50)
        total_d = len(r["D"])
        probe_pct = f"{le0 / total_d * 100:.0f}%" if total_d > 0 else "N/A"
        a_end = r["A"][-1]["after"] if r["A"] else -1
        print(f"{r['mode']:>8} {r['alpha0']:>4} {r['rep']:>3}  "
              f"{acts.get('up', 0):>4} {acts.get('down', 0):>4} {acts.get('gray', 0):>5} "
              f"{acts.get('cool', 0):>4} {acts.get('hold', 0):>4}  "
              f"{le0:>7} {mid:>7} {ge50:>6}  {probe_pct:>6}  {a_end:>5}")

    # 表 2: 分相位的 late 分布 (所有 AIMD run 合计)
    print()
    print("=" * 105)
    print("TABLE 2: late_delta distribution by phase (all AIMD runs combined)")
    print("  L 段(轻负载) late<=0 应该多 => probe up 能触发; 否则 AIMD 爬不上去")
    print("=" * 105)
    print(f"{'phase':>6}  {'late<=0':>7} {'0<lt<50':>7} {'lt>=50':>6}  {'total':>6}  {'late<=0%':>8}  {'verdict':>10}")
    print("-" * 70)
    for ph in range(4):
        le0 = mid = ge50 = 0
        for r in aimd_runs:
            for d in r["D"]:
                if phase_of(d["win"], bounds) == ph:
                    if d["late"] <= 0:
                        le0 += 1
                    elif d["late"] < 50:
                        mid += 1
                    else:
                        ge50 += 1
        total = le0 + mid + ge50
        pct = f"{le0 / total * 100:.0f}%" if total > 0 else "N/A"
        verdict = "probe ok" if (total > 0 and le0 / total > 0.2) else "PROBE LOW!"
        print(f"{phase_names[ph]:>6}  {le0:>7} {mid:>7} {ge50:>6}  {total:>6}  {pct:>8}  {verdict:>10}")

    # 表 3: 分相位 actions (所有 AIMD run 合计)
    print()
    print("=" * 105)
    print("TABLE 3: actions by phase (all AIMD runs combined)")
    print("  L 段 up 应该多(爬升), H 段 down 应该多(退避)")
    print("=" * 105)
    print(f"{'phase':>6}  {'up':>5} {'down':>5} {'gray':>5} {'cool':>5} {'hold':>5}  {'total':>5}  {'up%':>5}  {'down%':>6}")
    print("-" * 65)
    for ph in range(4):
        acts = defaultdict(int)
        for r in aimd_runs:
            for a in r["A"]:
                if phase_of(a["win"], bounds) == ph:
                    acts[a["action"]] += 1
        total = sum(acts.values())
        up_pct = f"{acts.get('up', 0) / total * 100:.0f}%" if total > 0 else "N/A"
        dn_pct = f"{acts.get('down', 0) / total * 100:.0f}%" if total > 0 else "N/A"
        print(f"{phase_names[ph]:>6}  {acts.get('up', 0):>5} {acts.get('down', 0):>5} "
              f"{acts.get('gray', 0):>5} {acts.get('cool', 0):>5} {acts.get('hold', 0):>5}  "
              f"{total:>5}  {up_pct:>5}  {dn_pct:>6}")

    # 对比 exp3 light aimd0
    print()
    print("=" * 105)
    print("COMPARISON: exp4 aimd0 vs exp3 light aimd0 (up:84/rep, climbed to 60)")
    print("=" * 105)
    for r in sorted(aimd_runs, key=lambda x: (x["mode"], x["rep"])):
        if r["mode"] != "aimd0":
            continue
        acts = defaultdict(int)
        for a in r["A"]:
            acts[a["action"]] += 1
        le0 = sum(1 for d in r["D"] if d["late"] <= 0)
        total_d = len(r["D"])
        probe_pct = le0 / total_d * 100 if total_d > 0 else 0
        # 分 L 段统计
        l_up = 0
        for a in r["A"]:
            if phase_of(a["win"], bounds) in (0, 2) and a["action"] == "up":
                l_up += 1
        l_le0 = 0
        l_total = 0
        for d in r["D"]:
            if phase_of(d["win"], bounds) in (0, 2):
                l_total += 1
                if d["late"] <= 0:
                    l_le0 += 1
        l_pct = l_le0 / l_total * 100 if l_total > 0 else 0
        print(f"  aimd0 rep{r['rep']}: up={acts.get('up', 0):>3} (L段 up={l_up:>3}) | "
              f"late<=0: {le0}/{total_d} ({probe_pct:.0f}%) | "
              f"L段 late<=0: {l_le0}/{l_total} ({l_pct:.0f}%)")


if __name__ == "__main__":
    if len(sys.argv) < 2:
        print(f"Usage: {sys.argv[0]} <sexp4_dyn.csv>")
        sys.exit(1)
    main(sys.argv[1])
