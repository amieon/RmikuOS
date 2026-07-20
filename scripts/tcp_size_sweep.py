#!/usr/bin/env python3
# plot_tcp.py —— TCP experiment data aggregation and plotting
#
# Reads:
#   logs/tcp/size_sweep.csv   (from tcp_size_sweep.sh: size sweep @ 0% loss)
#   logs/tcp/summary.csv      (from tcp_exp.sh: loss-rate sweep @ 100K)
# Outputs:
#   logs/tcp/fig1_size_sweep.png    time vs file size (control group, do-no-harm)
#   logs/tcp/fig2_drift.png         cross-session drift (predictability evidence)
#     (fallback: fig2_rtx.png if only one session and rtx > 0)
#   logs/tcp/fig3_loss_sweep.png    time vs loss rate (Jacobson effect, main result)
#   prints a stats table (speedups + drift)
#
# Notes:
#   - If size_sweep.csv contains several sessions appended back-to-back
#     (same version+size appearing multiple times), each occurrence is kept
#     as a separate session in file order; fig1 shows all of them and fig2
#     plots first->last drift per version.
#   - All-zero rtx @ 0% loss is the EXPECTED clean result (warmup+sleep
#     eliminated self-inflicted noise). An empty-looking rtx chart is not a
#     bug; this script detects that case and says so instead.
#
# Requires: pip install matplotlib   (no CJK fonts needed, all labels English)

import csv
import os
import re
import sys

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

OUTDIR = "../logs/tcp"
SIZE_ORDER = ["4K", "8K", "16K", "32K", "64K", "128K", "256K", "512K", "1M"]

LABEL = {"new": "Adaptive RTO (Jacobson/Karn)", "old": "Fixed RTO (1s)"}
COLOR = {"new": "#1f77b4", "old": "#d62728"}
NAN = float("nan")


def load_size_sweep(path):
    """version -> {size: [(median, rtx, samples), ...]}  one entry per session."""
    data = {"new": {}, "old": {}}
    if not os.path.exists(path):
        print(f"!! {path} not found, skipping fig 1/2")
        return data
    with open(path) as f:
        for row in csv.DictReader(f):
            v = row["version"].strip()
            s = row["size"].strip()
            if v not in data or s not in SIZE_ORDER:
                continue
            data[v].setdefault(s, []).append(
                (float(row["median_s"]), int(row["rtx_count"]), int(row["sample_count"])))
    return data


def load_loss_sweep(path):
    """version -> {loss_pct: median}"""
    data = {"new": {}, "old": {}}
    if not os.path.exists(path):
        print(f"!! {path} not found, skipping fig 3")
        return data
    skipped = []
    with open(path) as f:
        for row in csv.DictReader(f):
            m = re.fullmatch(r"(new|old)-(\d+)pct", row["name"].strip())
            if not m:
                skipped.append(row["name"])
                continue
            data[m.group(1)][int(m.group(2))] = float(row["median_s"])
    if skipped:
        print(f"!! skipped unrecognized rows in summary.csv: {skipped}")
    return data


def session_at(data, v, s, si):
    """median of session si for (v, s), or NAN."""
    sess = data[v].get(s, [])
    return sess[si][0] if si < len(sess) else NAN


def fig1_size_sweep(data):
    sizes = [s for s in SIZE_ORDER if data["new"].get(s) or data["old"].get(s)]
    if not sizes:
        return
    fig, ax = plt.subplots(figsize=(8, 5))
    x = list(range(len(sizes)))
    multi = False
    for v in ("new", "old"):
        nsess = max((len(data[v].get(s, [])) for s in sizes), default=0)
        for si in range(nsess):
            ys = [session_at(data, v, s, si) for s in sizes]
            if si == nsess - 1:
                ax.plot(x, ys, "o-", color=COLOR[v], linewidth=2, markersize=6,
                        label=f"{LABEL[v]}" + (" (latest)" if nsess > 1 else ""))
            else:
                multi = True
                ax.plot(x, ys, "o--", color=COLOR[v], alpha=0.35, linewidth=1,
                        markersize=4, label=f"{LABEL[v]} (earlier session)")
    ax.set_xticks(x)
    ax.set_xticklabels(sizes)
    ax.set_yscale("log")
    ax.set_xlabel("File size")
    ax.set_ylabel("Median transfer time (s, log scale)")
    title = "Transfer time vs file size @ 0% injected loss (control group)"
    if multi:
        title += "\nsolid = latest session, dashed = earlier session"
    ax.set_title(title)
    ax.grid(alpha=0.3, which="both")
    ax.legend()
    fig.tight_layout()
    fig.savefig(f"{OUTDIR}/fig1_size_sweep.png", dpi=150)
    print(f"-> {OUTDIR}/fig1_size_sweep.png")


def fig2_drift_or_rtx(data):
    """Prefer the cross-session drift chart (needs >=2 sessions per version);
    fall back to an honest rtx chart; skip with a note if both are trivial."""
    sizes = [s for s in SIZE_ORDER if data["new"].get(s) or data["old"].get(s)]
    if not sizes:
        return
    drift = {}  # v -> {size: drift%}
    for v in ("new", "old"):
        for s in sizes:
            sess = data[v].get(s, [])
            if len(sess) >= 2 and sess[0][0] > 0:
                drift.setdefault(v, {})[s] = (sess[-1][0] / sess[0][0] - 1.0) * 100.0

    if drift:
        fig, ax = plt.subplots(figsize=(8, 5))
        worst = 0.0
        for v in ("new", "old"):
            if v not in drift:
                continue
            ss = [s for s in sizes if s in drift[v]]
            xs = [sizes.index(s) for s in ss]
            ys = [drift[v][s] for s in ss]
            worst = max(worst, max(abs(y) for y in ys))
            ax.plot(xs, ys, "o-", color=COLOR[v], label=LABEL[v],
                    linewidth=2, markersize=6)
        ax.axhline(0, color="gray", linewidth=1)
        ax.set_xticks(list(range(len(sizes))))
        ax.set_xticklabels(sizes)
        ax.set_xlabel("File size")
        ax.set_ylabel("Cross-session drift (%)  [+ = second session slower]")
        ax.set_title("Predictability: same version, two sessions back-to-back\n"
                     f"(max |drift| = {worst:.0f}%)")
        ax.grid(alpha=0.3)
        ax.legend()
        fig.tight_layout()
        fig.savefig(f"{OUTDIR}/fig2_drift.png", dpi=150)
        print(f"-> {OUTDIR}/fig2_drift.png")
        return

    # Fallback: rtx chart, but only if there is anything nonzero to show.
    total_rtx = sum(sess[0][1] for v in ("new", "old") for s in sizes
                    for sess in data[v].get(s, [])[:1])
    if total_rtx == 0:
        print("-> fig2 skipped: single session and 0 retransmissions everywhere")
        print("   (0 rtx @ 0% loss is the expected clean control-group result)")
        return
    fig, ax = plt.subplots(figsize=(8, 5))
    x = list(range(len(sizes)))
    w = 0.35
    for i, v in enumerate(("new", "old")):
        ys = [data[v].get(s, [(0, 0, 0)])[0][1] for s in sizes]
        ax.bar([xi + (i - 0.5) * w for xi in x], ys, width=w,
               color=COLOR[v], label=f"{LABEL[v]} retransmissions", alpha=0.85)
    ax.set_xticks(x)
    ax.set_xticklabels(sizes)
    ax.set_xlabel("File size")
    ax.set_ylabel("Retransmission count @ 0% injected loss")
    ax.set_title("Spurious retransmissions @ 0% injected loss")
    ax.grid(alpha=0.3, axis="y")
    ax.legend()
    fig.tight_layout()
    fig.savefig(f"{OUTDIR}/fig2_rtx.png", dpi=150)
    print(f"-> {OUTDIR}/fig2_rtx.png")


def fig3_loss_sweep(data):
    fig, ax = plt.subplots(figsize=(8, 5))
    plotted = False
    for v in ("new", "old"):
        if not data[v]:
            continue
        xs = sorted(data[v])
        ys = [data[v][p] for p in xs]
        ax.plot(xs, ys, "o-", color=COLOR[v], label=LABEL[v], linewidth=2, markersize=6)
        plotted = True
    if not plotted:
        return
    ax.set_xlabel("Injected loss rate (%)")
    ax.set_ylabel("Median transfer time (s)")
    ax.set_title("100K file: transfer time vs loss rate\n(Jacobson/Karn vs fixed 1s RTO)")
    ax.grid(alpha=0.3)
    ax.legend()
    fig.tight_layout()
    fig.savefig(f"{OUTDIR}/fig3_loss_sweep.png", dpi=150)
    print(f"-> {OUTDIR}/fig3_loss_sweep.png")


def print_stats(size_data, loss_data):
    print("\n===== Stats =====")
    print("[Exp1: size sweep @ 0% loss, latest session]  size    new     old     old/new")
    for s in SIZE_ORDER:
        n = size_data["new"].get(s)
        o = size_data["old"].get(s)
        if n and o:
            nm, om = n[-1][0], o[-1][0]
            print(f"  {s:>5}  {nm:6.3f}s {om:6.3f}s  {om/nm:.2f}x")
    print("[Exp1b: cross-session drift, first->last]")
    for v in ("new", "old"):
        for s in SIZE_ORDER:
            sess = size_data[v].get(s, [])
            if len(sess) >= 2 and sess[0][0] > 0:
                d = (sess[-1][0] / sess[0][0] - 1.0) * 100.0
                print(f"  {v} {s:>5}: {sess[0][0]:7.3f}s -> {sess[-1][0]:7.3f}s  {d:+.0f}%")
    print("[Exp2: loss sweep @ 100K]  loss    new     old     speedup")
    for p in sorted(set(loss_data["new"]) & set(loss_data["old"])):
        n, o = loss_data["new"][p], loss_data["old"][p]
        print(f"  {p:>3}%  {n:6.3f}s {o:6.3f}s  {o/n:.2f}x")


def main():
    size_data = load_size_sweep(f"{OUTDIR}/size_sweep.csv")
    loss_data = load_loss_sweep(f"{OUTDIR}/summary.csv")
    fig1_size_sweep(size_data)
    fig2_drift_or_rtx(size_data)
    fig3_loss_sweep(loss_data)
    print_stats(size_data, loss_data)


if __name__ == "__main__":
    sys.exit(main())