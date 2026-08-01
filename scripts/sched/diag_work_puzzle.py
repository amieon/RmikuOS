#!/usr/bin/env python3
"""
diag_work_puzzle.py -- 为什么 aimd(全程α>0) 的 ai_work 比 fixed0(α=0) 还低?

矛盾: aimd 全程 α>0, ai eff_tickets 应该全程 > fixed0(α=0),
     ai 拿 CPU 应该更多, ai_work 不该比 fixed0 低。
但实际 aimd0 ai_work(86497) < fixed0(88823)。

对比 fixed0/25/50 vs aimd0/50/100:
  - wins: window 数(高α时监控进程被抢占, window 可能少)
  - ai_eff_avg: ai eff_tickets 全程平均
  - ai_run_sum: ai 实际拿的 CPU ticks 总和(W 行 run_delta)
  - ctrl_run_sum: ctrl 拿的 CPU 总和
  - ai_work: K 行 burn 迭代数
  - run_per_work: ai_run_sum/ai_work (每 burn 迭代占的 ticks, 高=burn 被拖慢)

定位:
  - 若 aimd ai_run_sum < fixed0: ai 实际拿 CPU 少(eff 高但没转化成 run?)
  - 若 aimd run_per_work > fixed0: ai 每 burn 迭代被拖慢(set_sched_alpha 干扰?)
"""
import sys
import re
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
                       "W": [], "K": [], "S": []}
                continue
            if in_warmup or cur is None:
                continue
            p = line.split(",")
            try:
                if p[0] == "W" and len(p) >= 8:
                    cur["W"].append({"win": int(p[1]), "name": p[4], "run": int(p[5]),
                                     "eff": int(p[6]), "ready": int(p[7])})
                elif p[0] == "K" and len(p) >= 5:
                    cur["K"].append({"name": p[2], "threads": int(p[3]), "work": int(p[4])})
                elif p[0] == "S" and len(p) >= 5:
                    cur["S"].append({"win": int(p[1])})
            except (IndexError, ValueError):
                continue
    if cur and not in_warmup:
        runs.append(cur)
    return runs


def main(path):
    runs = parse(path)
    print(f"[parsed] {len(runs)} runs\n")

    modes = ["fixed0", "fixed25", "fixed50", "aimd0", "aimd50", "aimd100"]

    print("=" * 118)
    print("PER-RUN: window / ai_eff / ai_run / ctrl_run / ai_work / run_per_work")
    print("=" * 118)
    hdr = (f"{'mode':>8} {'rep':>3}  {'wins':>4}  {'ai_eff':>7} {'ai_run':>8} "
           f"{'ctrl_run':>8} {'ai_work':>8}  {'run/work':>8}  {'a_eff>f0?':>9}")
    print(hdr)
    print("-" * 118)

    # 先算 fixed0 的 ai_eff 均值作基准
    f0_eff = []
    for r in runs:
        if r["mode"] == "fixed0":
            f0_eff += [w["eff"] for w in r["W"] if w["name"] == "ai"]
    f0_eff_avg = np.mean(f0_eff) if f0_eff else 1

    summary = {}
    for mode in modes:
        grp = [x for x in runs if x["mode"] == mode]
        if not grp:
            continue
        agg = {"wins": [], "ai_eff": [], "ai_run": [], "ctrl_run": [], "ai_work": [], "rpw": []}
        for r in grp:
            wins = len(r["S"])
            ai_effs = [w["eff"] for w in r["W"] if w["name"] == "ai"]
            ai_run = sum(w["run"] for w in r["W"] if w["name"] == "ai")
            ctrl_run = sum(w["run"] for w in r["W"] if w["name"] == "ctrl")
            ai_work = next((k["work"] for k in r["K"] if k["name"] == "ai"), 0)
            ai_eff_avg = np.mean(ai_effs) if ai_effs else 0
            rpw = ai_run / ai_work if ai_work > 0 else 0
            agg["wins"].append(wins)
            agg["ai_eff"].append(ai_eff_avg)
            agg["ai_run"].append(ai_run)
            agg["ctrl_run"].append(ctrl_run)
            agg["ai_work"].append(ai_work)
            agg["rpw"].append(rpw)
            eff_vs_f0 = "YES" if ai_eff_avg > f0_eff_avg else "no"
            print(f"{mode:>8} {r['rep']:>3}  {wins:>4}  {ai_eff_avg:>7.1f} {ai_run:>8} "
                  f"{ctrl_run:>8} {ai_work:>8}  {rpw:>8.4f}  {eff_vs_f0:>9}")
        # 组平均
        summary[mode] = {k: np.mean(v) for k, v in agg.items()}
        s = summary[mode]
        print(f"  >> {mode:>6} AVG: wins={s['wins']:.0f} ai_eff={s['ai_eff']:.1f} "
              f"ai_run={s['ai_run']:.0f} ctrl_run={s['ctrl_run']:.0f} "
              f"ai_work={s['ai_work']:.0f} run/work={s['rpw']:.4f}\n")

    # 关键对比
    print("=" * 118)
    print("KEY COMPARISON (avg)")
    print("=" * 118)
    print(f"{'mode':>10}  {'ai_eff':>8} {'ai_run':>9} {'ai_work':>9} {'run/work':>9}  {'eff排名':>6} {'work排名':>7}")
    print("-" * 80)
    eff_rank = sorted(summary, key=lambda m: -summary[m]['ai_eff'])
    work_rank = sorted(summary, key=lambda m: -summary[m]['ai_work'])
    for mode in modes:
        if mode in summary:
            s = summary[mode]
            print(f"{mode:>10}  {s['ai_eff']:>8.1f} {s['ai_run']:>9.0f} {s['ai_work']:>9.0f} "
                  f"{s['rpw']:>9.4f}  {eff_rank.index(mode)+1:>6} {work_rank.index(mode)+1:>7}")
    print()
    print("解读:")
    print("  - 若 aimd 的 ai_eff 排名靠前(高)但 ai_work 排名靠后(低) => eff 高但没转化成 work")
    print("  - 若 aimd 的 run/work 明显高于 fixed0 => ai 每 burn 迭代被拖慢(调α/set_sched_alpha 干扰)")
    print("  - 若 aimd 的 ai_run 低于 fixed0 => ai 实际拿到的 CPU 更少(尽管 eff 高)")


if __name__ == "__main__":
    if len(sys.argv) < 2:
        print(f"Usage: {sys.argv[0]} <sexp4_dyn.csv>")
        sys.exit(1)
    main(sys.argv[1])
