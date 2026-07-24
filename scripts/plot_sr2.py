#!/usr/bin/env python3
# plot_sr2.py —— GBN vs SR 丢包率扫描出图
#
# 用法: python3 scripts/plot_sr2.py [--csv logs/sr/runs.csv] [--out logs/sr/figs]
#
# 输出:
#   fig_sr2_time_vs_loss.png   耗时(中位数+IQR) vs 丢包率,两条线 —— 主结果
#   fig_sr2_box.png            各丢包档分组箱线图 —— 看分布形态与离散度

import argparse
import csv
from pathlib import Path

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

LOSSES_ORDER = [0, 5, 6, 7, 10, 20, 50, 100, 200, 500]
COLORS = {"gbn": "#d62728", "sr": "#2ca02c"}


def load(path):
    data = {}
    for r in csv.DictReader(open(path)):
        data.setdefault((r["version"], int(r["loss"])), []).append(float(r["time_s"]))
    return data


def med(xs):
    s = sorted(xs)
    n = len(s)
    return s[n // 2] if n % 2 else (s[n // 2 - 1] + s[n // 2]) / 2


def iqr(xs):
    s = sorted(xs)
    n = len(s)
    return s[n // 4], s[(3 * n) // 4]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--csv", default="logs/sr/runs.csv")
    ap.add_argument("--out", default="logs/sr/figs")
    args = ap.parse_args()
    data = load(args.csv)
    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    losses = [l for l in LOSSES_ORDER if any(k[1] == l for k in data)]
    vers = sorted({k[0] for k in data})

    # ---- fig1: 中位数 + IQR vs 丢包率 ----
    fig, ax = plt.subplots(figsize=(8.5, 5))
    xlabels = []
    for xi, l in enumerate(losses):
        xlabels.append("0 (no loss)" if l == 0 else f"1/{l}")
        for ver in vers:
            ys = data.get((ver, l))
            if not ys:
                continue
            m = med(ys)
            q1, q3 = iqr(ys)
            off = -0.12 if ver == "gbn" else 0.12
            ax.errorbar([xi + off], [m],
                        yerr=[[m - q1], [q3 - m]],
                        fmt="o", color=COLORS.get(ver, "gray"),
                        capsize=4, markersize=6,
                        label=ver if xi == 0 else None)
    for ver in vers:
        ys0 = data.get((ver, 0))
        if ys0:
            ax.axhline(med(ys0), color=COLORS.get(ver, "gray"),
                       ls="--", alpha=0.4)
    ax.set_xticks(range(len(losses)))
    ax.set_xticklabels(xlabels)
    ax.set_xlabel("loss rate (one injected drop per N segments)")
    ax.set_ylabel("transfer time, median [IQR] (s)")
    ax.set_title("GBN vs SR — 1M transfer under injected loss (n=20/cell)\n"
                 "dashed lines = no-loss baseline")
    ax.legend()
    ax.grid(alpha=0.3)
    fig.tight_layout()
    fig.savefig(out / "fig_sr2_time_vs_loss.png", dpi=150)
    print("[fig1] -> fig_sr2_time_vs_loss.png")

    # ---- fig2: 分组箱线图 ----
    fig, ax = plt.subplots(figsize=(10, 5))
    positions, colors = [], []
    box_data = []
    for xi, l in enumerate(losses):
        for off, ver in ((-0.18, "gbn"), (0.18, "sr")):
            ys = data.get((ver, l))
            if not ys:
                continue
            box_data.append(ys)
            positions.append(xi + off)
            colors.append(COLORS.get(ver, "gray"))
    bp = ax.boxplot(box_data, positions=positions, widths=0.3,
                    patch_artist=True, showfliers=True,
                    medianprops=dict(color="black"))
    for patch, c in zip(bp["boxes"], colors):
        patch.set_facecolor(c)
        patch.set_alpha(0.45)
    ax.set_xticks(range(len(losses)))
    ax.set_xticklabels(xlabels)
    ax.set_xlabel("loss rate")
    ax.set_ylabel("transfer time (s)")
    ax.set_title("Per-cell distributions (left/red=gbn, right/green=sr)")
    ax.grid(axis="y", alpha=0.3)
    fig.tight_layout()
    fig.savefig(out / "fig_sr2_box.png", dpi=150)
    print("[fig2] -> fig_sr2_box.png")

    # ---- 控制台输出汇总表 ----
    print(f"\n{'loss':>6} | " + " | ".join(f"{v:>16}" for v in vers))
    for l in losses:
        cells = []
        for ver in vers:
            ys = data.get((ver, l))
            if ys:
                m = med(ys)
                mean = sum(ys) / len(ys)
                cells.append(f"med {m:5.1f} mean {mean:5.1f}")
            else:
                cells.append("-")
        print(f"{('1/'+str(l)) if l else '0':>6} | " + " | ".join(f"{c:>16}" for c in cells))


if __name__ == "__main__":
    main()