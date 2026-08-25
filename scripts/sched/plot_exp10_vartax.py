#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""plot_exp10_vartax.py -- Exp10: 方差税检验(miss vs std(α))

对应 report.md §6.6「方差税: 同样的均值, 三倍的 miss——但只在悬崖的凸侧」:
  - 散点图 exp10_vartax_miss_vs_jitter.png: 16 个自适应模式 × 3 ratios,
    x = 全程 α 标准差, y = miss%。
    报告断言: 跨 16 个自适应模式(25/25)相关 r ≈ 0.18(近乎零)——
    「方差税不是抖多少的函数, 而是在哪里抖的函数」;
  - 打印 adamw50 @25/25 的窗口级分桶 Jensen 分解(report §6.6 表):
    逐窗按生效 α 分桶(α<15 / 15~40 / α>=40), 看各桶窗口占比与 miss;
  - 打印单点对照: fixed25 (均值 25.0, std 0, miss 2.4) vs
    adamw50 (均值 25.7, std 17.6, miss 7.5)。

用法: python3 plot_exp10_vartax.py <sexp10_all.csv>
"""
import os
import sys

import numpy as np
import matplotlib.pyplot as plt

from schedlab_stat import (
    RATIOS, RATIO_LABELS, compute_file, aggregate_runs,
    parse_csv, RATIO_L_PCT,
)

FAMILIES = [
    ("aimd",  ["aimd0", "aimd50", "aimd100"]),
    ("cubic", ["cubic0", "cubic50", "cubic100"]),
    ("optim", ["sgdm", "rmsprop", "adagrad"]),
    ("adamw", ["adamw0", "adamw50", "adamw100"]),
    ("pid",   ["pid0", "pid50", "pid100"]),
    ("ucb",   ["ucb"]),
]
ADAPTIVE_MODES = [m for _, ms in FAMILIES for m in ms]  # 16 个自适应模式
MODE2FAM = {m: fam for fam, ms in FAMILIES for m in ms}
FAM_COLOR = {
    "aimd": "#2563eb", "cubic": "#db2777", "optim": "#dc2626",
    "adamw": "#059669", "pid": "#0891b2", "ucb": "#7c3aed",
}
RATIO_MARKER = {500: "o", 800: "s", 200: "^"}


def alpha_std(run):
    """全程 α 标准差。

    注意必须用全程而非测量段(后半段): 全程 std 下 25/25 的 16 模式相关
    r≈0.18(与 report.md §6.6 一致), 后半段 std 会把 AIMD 的冲高期削掉,
    相关掉到 ~0。
    """
    tr = run.get("alpha_traj")
    if tr is None or len(tr) <= 1:
        return 0.0
    return float(np.nanstd(tr))


def main():
    if len(sys.argv) < 2:
        print(f"Usage: {sys.argv[0]} <sexp10_all.csv>")
        sys.exit(1)
    path = sys.argv[1]
    outdir = os.path.dirname(path) or "."

    print(f"[streaming] parsing + computing {path} ...")
    computed = compute_file(path)
    print(f"[computed] {len(computed)} runs")
    stats = aggregate_runs(computed)

    # ------------------------------------------------ 散点: miss vs std(α)
    pts = []  # (ratio, mode, std_mean, miss_mean)
    for (ratio, mode), s in stats.items():
        if mode not in ADAPTIVE_MODES:
            continue
        reps = [r for r in computed
                if r["ratio"] == ratio and r["mode"] == mode]
        if not reps:
            continue
        std_mean = float(np.mean([alpha_std(r) for r in reps]))
        pts.append((ratio, mode, std_mean, s["miss_rate_mean"]))

    fig, ax = plt.subplots(figsize=(10, 6.5))
    for ratio, mode, x, y in pts:
        ax.plot(x, y, RATIO_MARKER.get(ratio, "o"),
                color=FAM_COLOR[MODE2FAM[mode]], ms=10,
                markeredgecolor="black", markeredgewidth=0.8)
        if ratio == 500:  # 25/25 上标名字, 其余 ratio 太挤不标
            ax.annotate(mode, (x, y), fontsize=7, ha="left", va="bottom",
                        xytext=(4, 3), textcoords="offset points")
    for fam, c in FAM_COLOR.items():
        ax.plot([], [], "o", color=c, ms=9, label=fam)
    for ratio, mk in RATIO_MARKER.items():
        ax.plot([], [], mk, color="#64748b", ms=9,
                label=f"ratio {RATIO_LABELS[ratio]}")
    ax.set_xlabel("std(α), full trajectory (window-level jitter)")
    ax.set_ylabel("ctrl miss %")
    ax.set_title("Exp10: variance tax — miss vs α jitter "
                 "(16 adaptive modes × 3 ratios)")
    ax.legend(fontsize=8, ncol=2)
    ax.grid(True, alpha=0.3)
    fig.tight_layout()
    out = os.path.join(outdir, "exp10_vartax_miss_vs_jitter.png")
    fig.savefig(out)
    print(f"[saved] {out}")
    plt.close(fig)

    # 相关性: 全局(3 ratios 混池)与 25/25 单独各算一个
    xs_all = np.array([p[2] for p in pts]); ys_all = np.array([p[3] for p in pts])
    r_all = np.corrcoef(xs_all, ys_all)[0, 1]
    p500 = [p for p in pts if p[0] == 500]
    r_500 = np.corrcoef([p[2] for p in p500], [p[3] for p in p500])[0, 1]
    print(f"[corr] miss vs std(α): 25/25 跨 16 模式 r = {r_500:.2f} "
          f"(report.md §6.6: ≈0.18); 3 ratios 混池({len(pts)} 点) r = {r_all:.2f}")
    print("       近乎零的相关 = 方差税不是「抖多少」的函数, 而是「在哪里抖」的函数")

    # --------------------------------------- 单点对照: fixed25 vs adamw50 (25/25)
    print("\n[spot check] 同样的均值, 三倍的 miss (25/25):")
    print(f"{'mode':>8}  {'α均值':>6}  {'α标准差':>7}  {'miss%':>6}")
    for mode in ("fixed25", "adamw50", "aimd50"):
        s = stats.get((500, mode))
        reps = [r for r in computed if r["ratio"] == 500 and r["mode"] == mode]
        std_mean = float(np.mean([alpha_std(r) for r in reps])) if reps else 0.0
        print(f"{mode:>8}  {s['alpha_steady_mean']:>6.1f}  {std_mean:>7.1f}  "
              f"{s['miss_rate_mean']:>6.1f}")

    # --------------------------- adamw50 窗口级分桶 Jensen 分解(25/25, 逐 rep)
    print("\n[bucket] adamw50 @25/25 逐窗按生效 α 分桶 "
          "(W 行 alpha = 当窗实际生效值(SPSA 探针 ±5 在内), D 行 = 逐窗 miss):")
    print(f"{'rep':>4}  {'桶':>10}  {'窗口占比%':>9}  {'miss%':>7}")
    buckets = [("α<15", lambda a: a < 15),
               ("15≤α<40", lambda a: 15 <= a < 40),
               ("α≥40", lambda a: a >= 40)]

    def handle(run):
        if run["mode"] != "adamw50" or run["ratio"] != 500:
            return
        # 生效 α 取 W 行(调度器当窗实际用的值), 不是 A.after(更新后的基准值)
        amap = {}
        for w in run["W"]:
            amap.setdefault(w["win"], w["alpha"])
        dmap = {d["win"]: (d["jobs"], d["miss"]) for d in run["D"]}
        wins = sorted(set(amap) & set(dmap))
        if not wins:
            return
        half = wins[len(wins) // 2]  # 测量段(后半段)
        seg = [w for w in wins if w >= half]
        for name, pred in buckets:
            sel = [w for w in seg if pred(amap[w])]
            if not sel:
                continue
            j = sum(dmap[w][0] for w in sel)
            m = sum(dmap[w][1] for w in sel)
            print(f"{run['rep']:>4}  {name:>10}  "
                  f"{len(sel) / len(seg) * 100:>9.1f}  "
                  f"{(m / j * 100.0 if j else 0.0):>7.1f}")

    parse_csv(path, on_run=handle)
    print("\nDone.")


if __name__ == "__main__":
    main()
