#!/usr/bin/env python3
"""
AIMD vs fixed-alpha 对照结论图。

直接吃 raw 日志（adaptive 与 fixed 混在一起都行），自动按模式分组：
  - fixed   run：窗口动作含 fixed_hold
  - adaptive run：其余（AIMD）
对每个 case，画 tardiness-vs-AI-work 散点：
  - fixed 各 alpha 连成 baseline 曲线（按 alpha 排序）
  - adaptive 的点单独高亮
多 repeat 时同配置自动取均值，并以 min/max 误差棒表示波动。

“帕累托占优”读法：AIMD 的点若落在 fixed 曲线的【左上方】
（更低 tardiness + 更高 AI work），即为占优。

用法：
  python3 plot_aimd_vs_fixed.py <raw.log> [out_dir]
"""
import os
import re
import sys
import math
from collections import defaultdict

import matplotlib.pyplot as plt


RUN_RE = re.compile(
    r"\[adaptive_alpha\]\s+run\s+initial_alpha=(?P<ia>\d+)"
    r"\s+control_threads=(?P<ct>\d+)\s+ai_threads=(?P<at>\d+)"
    r"\s+logger_threads=(?P<lt>\d+)"
)
WIN_RE = re.compile(r"\[adaptive_window\].*?action=(?P<action>\S+)")
CTRL_RE = re.compile(
    r"\[edge_deadline\]\s+alpha=(?P<alpha>\d+)\s+role=control_result.*?"
    r"jobs=(?P<jobs>\d+)\s+miss=(?P<miss>\d+)"
    r"(?:\s+lateness_sum=(?P<lsum>\d+)\s+lateness_max=(?P<lmax>\d+)"
    r"\s+resp_sum=(?P<rsum>\d+)\s+resp_sumsq=(?P<rsq>\d+)"
    r"\s+resp_min=(?P<rmin>\d+)\s+resp_max=(?P<rmax>\d+))?"
)
AI_RE = re.compile(r"\[edge_deadline\]\s+alpha=\d+\s+role=ai_result.*?work=(?P<work>\d+)")
FINAL_RE = re.compile(r"\[adaptive_alpha\]\s+final_alpha=(?P<fa>\d+)")


def parse(path):
    runs = []
    cur = None
    with open(path, "r", encoding="utf-8", errors="ignore") as f:
        for line in f:
            m = RUN_RE.search(line)
            if m:
                if cur:
                    runs.append(cur)
                cur = {
                    "initial_alpha": int(m.group("ia")),
                    "case": f"{m.group('ct')}_{m.group('at')}_{m.group('lt')}",
                    "is_fixed": False,
                    "ctrl": None, "ai_work": None, "final_alpha": None,
                }
                continue
            if cur is None:
                continue
            m = WIN_RE.search(line)
            if m:
                if m.group("action") == "fixed_hold":
                    cur["is_fixed"] = True
                continue
            m = CTRL_RE.search(line)
            if m:
                jobs = int(m.group("jobs"))
                miss = int(m.group("miss"))
                d = {"alpha": int(m.group("alpha")), "jobs": jobs, "miss": miss,
                     "miss_rate": miss / jobs if jobs else 0.0,
                     "mean_tard": -1.0, "max_tard": -1}
                if m.group("lsum") is not None and jobs > 0:
                    d["mean_tard"] = int(m.group("lsum")) / jobs
                    d["max_tard"] = int(m.group("lmax"))
                cur["ctrl"] = d
                continue
            m = AI_RE.search(line)
            if m:
                cur["ai_work"] = int(m.group("work"))
                continue
            m = FINAL_RE.search(line)
            if m:
                cur["final_alpha"] = int(m.group("fa"))
                continue
    if cur:
        runs.append(cur)
    # 只保留拿到 control + ai 的完整 run
    return [r for r in runs if r["ctrl"] and r["ai_work"] is not None]


def agg(points):
    """同 key 多 repeat -> (mean_x, mean_y, xlo, xhi, ylo, yhi)。"""
    xs = [p[0] for p in points]
    ys = [p[1] for p in points]
    mx, my = sum(xs) / len(xs), sum(ys) / len(ys)
    return mx, my, min(xs), max(xs), min(ys), max(ys)


def plot_case(case, runs, metric, out_dir):
    """metric: 'mean_tard' | 'max_tard' | 'miss_rate'"""
    label = {"mean_tard": "mean tardiness (ticks)",
             "max_tard": "max tardiness (ticks)",
             "miss_rate": "control miss rate"}[metric]

    # fixed: 按固定 alpha 分组（用 ctrl.alpha，即实际生效 alpha）
    fixed_groups = defaultdict(list)   # alpha -> [(x_tard, y_work)]
    adapt_pts = []                     # [(x_tard, y_work, final_alpha)]

    for r in runs:
        if r["case"] != case:
            continue
        x = r["ctrl"][metric]
        y = r["ai_work"]
        if x is None or x < 0:
            continue
        if r["is_fixed"]:
            fixed_groups[r["ctrl"]["alpha"]].append((x, y))
        else:
            adapt_pts.append((x, y, r["final_alpha"]))

    if not fixed_groups and not adapt_pts:
        return

    fig, ax = plt.subplots(figsize=(7.5, 5))

    # --- fixed baseline 曲线（按 alpha 排序连线）---
    if fixed_groups:
        alphas_sorted = sorted(fixed_groups.keys())
        bx, by = [], []
        for a in alphas_sorted:
            mx, my, xlo, xhi, ylo, yhi = agg(fixed_groups[a])
            bx.append(mx); by.append(my)
            ax.errorbar(mx, my,
                        xerr=[[mx - xlo], [xhi - mx]],
                        yerr=[[my - ylo], [yhi - my]],
                        fmt="o", color="tab:blue", capsize=3, markersize=6,
                        zorder=3)
            ax.annotate(f"α={a}", (mx, my), textcoords="offset points",
                        xytext=(6, 5), fontsize=8, color="tab:blue")
        ax.plot(bx, by, "-", color="tab:blue", alpha=0.6,
                label="fixed alpha (baseline)", zorder=2)

    # --- adaptive 点（高亮）---
    if adapt_pts:
        ax_aggr = defaultdict(list)   # final_alpha 不稳定，按起点聚不合适 -> 全聚成一团
        axs = [p[0] for p in adapt_pts]
        ays = [p[1] for p in adapt_pts]
        finals = [p[2] for p in adapt_pts]
        ax.scatter(axs, ays, color="tab:red", marker="*", s=180,
                   label="AIMD (adaptive)", zorder=5, edgecolor="black",
                   linewidth=0.4)
        # 标注收敛 alpha
        for x, y, fa in adapt_pts:
            ax.annotate(f"→α={fa}", (x, y), textcoords="offset points",
                        xytext=(6, -10), fontsize=8, color="tab:red")

    ax.set_xlabel(label + "  (lower is better)")
    ax.set_ylabel("AI work  (higher is better)")
    ax.set_title(f"AIMD vs fixed-alpha trade-off, case={case}")
    ax.grid(True, alpha=0.3)
    ax.legend(loc="best")

    # 帕累托方向提示
    ax.annotate("better\n(low tardiness, high throughput)",
                xy=(0.02, 0.97), xycoords="axes fraction",
                fontsize=8, color="gray", va="top")

    fig.tight_layout()
    p = os.path.join(out_dir, f"aimd_vs_fixed_{metric}_{case}.png")
    fig.savefig(p, dpi=200)
    plt.close(fig)
    print(f"wrote {p}")


def print_table(runs):
    print()
    print(f"{'case':<10} {'mode':<9} {'alpha':<6} {'miss/jobs':<11} "
          f"{'mean_tard':<10} {'max_tard':<9} {'ai_work':<8}")
    print("-" * 70)
    rows = []
    for r in runs:
        mode = "fixed" if r["is_fixed"] else "AIMD"
        alpha = r["ctrl"]["alpha"] if r["is_fixed"] else r["final_alpha"]
        rows.append((r["case"], mode, alpha, r))
    for case, mode, alpha, r in sorted(rows, key=lambda t: (t[0], t[1], t[2])):
        c = r["ctrl"]
        mt = f"{c['mean_tard']:.3f}" if c["mean_tard"] >= 0 else "-"
        mx = f"{c['max_tard']}" if c["max_tard"] >= 0 else "-"
        print(f"{case:<10} {mode:<9} {alpha:<6} "
              f"{c['miss']}/{c['jobs']:<9} {mt:<10} {mx:<9} {r['ai_work']:<8}")


def main():
    if len(sys.argv) < 2:
        print("usage: plot_aimd_vs_fixed.py <raw.log> [out_dir]")
        return 1
    raw = sys.argv[1]
    out_dir = sys.argv[2] if len(sys.argv) >= 3 else "logs/figs_compare"
    os.makedirs(out_dir, exist_ok=True)

    runs = parse(raw)
    n_fixed = sum(1 for r in runs if r["is_fixed"])
    n_adapt = len(runs) - n_fixed
    cases = sorted(set(r["case"] for r in runs))
    print(f"parsed runs={len(runs)} (fixed={n_fixed}, adaptive={n_adapt}), "
          f"cases={cases}")

    print_table(runs)

    for case in cases:
        for metric in ("mean_tard", "max_tard", "miss_rate"):
            plot_case(case, runs, metric, out_dir)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())