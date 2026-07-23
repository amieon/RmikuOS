#!/usr/bin/env python3
# plot_sr.py —— GBN vs SR 对照出图
#
# 用法: python3 scripts/plot_sr.py [--dir logs/sr] [--loss 50] [--size 1M]
#
# 输出: logs/sr/figs/
#   fig_sr_dist.png   curl 耗时分布(箱线+散点)——主结果:SR 应多峰收窄
#   fig_sr_dur.png    内核 dur_ms 分布(排除 curl/进程开销,互相印证)
#   fig_sr_rtx.png    每连接:降窗次数 vs 重传次数(应近似相等)

import argparse
import csv
import re
from pathlib import Path

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

STAT_RE = re.compile(
    r"\[tcp-stat\] fd=\d+ dur_ms=(\d+) bytes=(\d+) segs=(\d+)"
    r" rtx_rto=(\d+) rtx_fast=(\d+) loss=(\d+)")


def load_runs(path, loss, size):
    out = {}
    with open(path) as f:
        for r in csv.DictReader(f):
            if r["loss"] == str(loss) and r["size"] == size:
                out.setdefault(r["version"], []).append(float(r["time_s"]))
    return out


def load_stats(events_dir, loss, size):
    out = {}
    pat = re.compile(rf"^(.+)-l{loss}-s{size}\.events\.txt$")
    for p in sorted(events_dir.glob(f"*-l{loss}-s{size}.events.txt")):
        m = pat.match(p.name)
        if not m:
            continue
        ver = m.group(1)
        for line in p.open(errors="ignore"):
            s = STAT_RE.search(line)
            if s:
                dur = int(s.group(1))
                rtx = int(s.group(4)) + int(s.group(5))
                loss_n = int(s.group(6))
                out.setdefault(ver, []).append((dur, rtx, loss_n))
    return out


def strip_box(ax, data, labels, ylabel, title):
    colors = ("#d62728", "#2ca02c", "#1f77b4", "#9467bd")
    xs = list(range(len(data)))
    bp = ax.boxplot(data, positions=xs, widths=0.5, showfliers=False,
                    patch_artist=True, medianprops=dict(color="black"))
    for patch, c in zip(bp["boxes"], colors):
        patch.set_facecolor(c)
        patch.set_alpha(0.35)
    for i, ys in enumerate(data):
        jitter = [i + (hash((i, j)) % 100 - 50) / 500 for j in range(len(ys))]
        ax.scatter(jitter, ys, s=14, alpha=0.75, zorder=5, color=colors[i])
        ax.annotate(f"n={len(ys)}\nmean={sum(ys)/len(ys):.2f}",
                    (i, max(ys)), textcoords="offset points",
                    xytext=(0, 8), ha="center", fontsize=8)
    ax.set_xticks(xs)
    ax.set_xticklabels(labels)
    ax.set_ylabel(ylabel)
    ax.set_title(title)
    ax.grid(axis="y", alpha=0.3)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--dir", default="logs/sr")
    ap.add_argument("--loss", default="50")
    ap.add_argument("--size", default="1M")
    args = ap.parse_args()

    d = Path(args.dir)
    out = d / "figs"
    out.mkdir(parents=True, exist_ok=True)

    runs = load_runs(d / "runs.csv", args.loss, args.size)
    stats = load_stats(d, args.loss, args.size)
    vers = sorted(set(runs) | set(stats))
    if not vers:
        print("没有匹配的数据,检查 --loss/--size 和文件名")
        return

    data = [runs[v] for v in vers if v in runs]
    if data:
        fig, ax = plt.subplots(figsize=(7, 5))
        strip_box(ax, data, vers, "transfer time (s)",
                  f"Completion time distribution — l{args.loss} {args.size}\n"
                  "(SR should narrow the multi-modal spread)")
        fig.tight_layout()
        fig.savefig(out / "fig_sr_dist.png", dpi=150)
        print("[fig1] -> fig_sr_dist.png")

    data2 = [[s[0] / 1000.0 for s in stats[v]] for v in vers if v in stats]
    if data2:
        fig, ax = plt.subplots(figsize=(7, 5))
        strip_box(ax, data2, vers, "connection lifetime dur_ms (s)",
                  f"Kernel-side connection lifetime — l{args.loss} {args.size}")
        fig.tight_layout()
        fig.savefig(out / "fig_sr_dur.png", dpi=150)
        print("[fig2] -> fig_sr_dur.png")

    labels, drops_m, rtx_m = [], [], []
    for v in vers:
        if v not in stats:
            continue
        ss = stats[v]
        labels.append(v)
        drops_m.append(sum(s[2] for s in ss) / len(ss))
        rtx_m.append(sum(s[1] for s in ss) / len(ss))
    if labels:
        x = range(len(labels))
        w = 0.35
        fig, ax = plt.subplots(figsize=(7, 4.5))
        ax.bar([i - w / 2 for i in x], drops_m, w, label="loss events / conn",
               color="#7f7f7f")
        ax.bar([i + w / 2 for i in x], rtx_m, w, label="retransmissions / conn",
               color="#1f77b4")
        for i, (a, b) in enumerate(zip(drops_m, rtx_m)):
            ax.annotate(f"{a:.1f}", (i - w / 2, a), ha="center",
                        xytext=(0, 3), textcoords="offset points", fontsize=9)
            ax.annotate(f"{b:.1f}", (i + w / 2, b), ha="center",
                        xytext=(0, 3), textcoords="offset points", fontsize=9)
        ax.set_xticks(list(x))
        ax.set_xticklabels(labels)
        ax.set_ylabel("count per connection")
        ax.set_title(f"Same retransmissions, different stalls — l{args.loss} {args.size}\n"
                     "(SR's win is eliminating wait chains, not fewer retransmits)")
        ax.legend()
        ax.grid(axis="y", alpha=0.3)
        fig.tight_layout()
        fig.savefig(out / "fig_sr_rtx.png", dpi=150)
        print("[fig3] -> fig_sr_rtx.png")

    print(f"全部输出到 {out}/")


if __name__ == "__main__":
    main()