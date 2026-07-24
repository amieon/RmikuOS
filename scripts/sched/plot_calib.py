#!/usr/bin/env python3
"""
plot_calib.py -- Visualize calibrate v2 output.

Usage:
    python3 ./scripts/sched/plot_calib.py ./logs/sched/calibrate/riscv64.txt
    python3 ./scripts/sched/plot_calib.py ./logs/sched/calibrate/riscv64.txt ./logs/sched/calibrate/loongarch64.txt   # overlay both

Output:
    ./logs/sched/calibrate/calib_linearity.png   (Phase 1: tick-time)
    ./logs/sched/calibrate/calib_burn.png        (Phase 2: burn sweep)
    ./logs/sched/calibrate/calib_summary.png     (Phase 3 + recommendations)
"""

import sys
import re
import numpy as np
import matplotlib
matplotlib.use("Agg")          # headless, no display needed
import matplotlib.pyplot as plt
from matplotlib.ticker import MaxNLocator

# ── style ──────────────────────────────────────────────────────
plt.rcParams.update({
    "font.family":      "DejaVu Sans",   # always available, no CJK
    "font.size":        11,
    "axes.titlesize":   13,
    "axes.labelsize":   12,
    "figure.dpi":       150,
    "savefig.dpi":      200,
    "savefig.bbox":     "tight",
    "axes.grid":        True,
    "grid.alpha":       0.3,
    "lines.markersize": 6,
})

COLORS = ["#2563eb", "#dc2626", "#16a34a", "#9333ea", "#ea580c"]


# ── parser ─────────────────────────────────────────────────────
def parse_log(path):
    """Parse calibrate v2 log into structured dict."""
    d = {
        "platform": "unknown",
        "L":  [],   # (span, rep, delta_us)
        "LS": [],   # (span, mean, std, rate_x1000)
        "LR": None, # (slope_x1000, intercept, r2_x10000)
        "B":  [],   # (iters, rep, total_ticks, inner)
        "BS": [],   # (iters, mean_x1000, std_x1000)
        "DRIFT": None,
    }
    with open(path) as f:
        for line in f:
            line = line.strip()
            if line.startswith("# calibrate"):
                m = re.search(r"platform=(\S+)", line)
                if m:
                    d["platform"] = m.group(1)
                continue
            if line.startswith("#"):
                continue
            parts = line.split(",")
            tag = parts[0]
            try:
                if tag == "L":
                    d["L"].append((int(parts[1]), int(parts[2]), int(parts[3])))
                elif tag == "LS":
                    d["LS"].append((int(parts[1]), int(parts[2]),
                                    int(parts[3]), int(parts[4])))
                elif tag == "LR":
                    d["LR"] = (int(parts[1]), int(parts[2]), int(parts[3]))
                elif tag == "B":
                    d["B"].append((int(parts[1]), int(parts[2]),
                                   int(parts[3]), int(parts[4])))
                elif tag == "BS":
                    d["BS"].append((int(parts[1]), int(parts[2]),
                                    int(parts[3])))
                elif tag == "DRIFT":
                    d["DRIFT"] = (int(parts[1]), int(parts[2]), int(parts[3]))
            except (IndexError, ValueError):
                continue
    return d


# ── Figure 1: Tick-Time Linearity ─────────────────────────────
def plot_linearity(datasets, out="./logs/sched/calibrate/calib_linearity.png"):
    fig, axes = plt.subplots(1, 2, figsize=(12, 5))

    # --- left: raw points + regression line ---
    ax = axes[0]
    for i, d in enumerate(datasets):
        spans  = np.array([r[0] for r in d["L"]])
        dus    = np.array([r[2] for r in d["L"]]) / 1e6   # -> seconds
        c = COLORS[i % len(COLORS)]
        ax.scatter(spans, dus, color=c, alpha=0.5, s=30,
                   label=f'{d["platform"]} raw')
        # regression line from LR
        if d["LR"]:
            slope = d["LR"][0] / 1e3      # us/tick
            intercept = d["LR"][1]        # us
            x_fit = np.array([0, max(spans) * 1.05])
            y_fit = (slope * x_fit + intercept) / 1e6
            ax.plot(x_fit, y_fit, color=c, lw=2, ls="--",
                    label=f'{d["platform"]} fit')
    ax.set_xlabel("Span (ticks)")
    ax.set_ylabel("Elapsed time (s)")
    ax.set_title("Phase 1: Tick-Time Linearity")
    ax.legend(fontsize=9)
    ax.xaxis.set_major_locator(MaxNLocator(integer=True))

    # --- right: us/tick per span (should be flat) ---
    ax = axes[1]
    for i, d in enumerate(datasets):
        spans = np.array([r[0] for r in d["LS"]])
        rate  = np.array([r[3] for r in d["LS"]]) / 1e3   # ms/tick
        std   = np.array([r[2] for r in d["LS"]]) / 1e3 / spans  # ms/tick err
        c = COLORS[i % len(COLORS)]
        ax.errorbar(spans, rate, yerr=std, fmt="o-", color=c,
                    capsize=4, label=d["platform"])
    ax.set_xlabel("Span (ticks)")
    ax.set_ylabel("Rate (ms / tick)")
    ax.set_title("Per-span tick rate (should be constant)")
    ax.legend(fontsize=9)
    ax.xaxis.set_major_locator(MaxNLocator(integer=True))

    # R² annotation
    for i, d in enumerate(datasets):
        if d["LR"]:
            r2 = d["LR"][2] / 1e4
            ax.annotate(f'{d["platform"]}: R²={r2:.4f}',
                        xy=(0.05, 0.92 - i * 0.08),
                        xycoords="axes fraction", fontsize=10,
                        color=COLORS[i % len(COLORS)])

    fig.suptitle("Calibration: Tick is a Faithful Linear Clock", y=1.02)
    fig.tight_layout()
    fig.savefig(out)
    print(f"[saved] {out}")
    plt.close(fig)


# ── Figure 2: Burn Sweep ──────────────────────────────────────
def plot_burn(datasets, out="./logs/sched/calibrate/calib_burn.png"):
    fig, axes = plt.subplots(1, 2, figsize=(12, 5))

    # --- left: ticks vs iters ---
    ax = axes[0]
    for i, d in enumerate(datasets):
        iters = np.array([r[0] for r in d["BS"]]) / 1e3   # k-iters
        mean  = np.array([r[1] for r in d["BS"]]) / 1e3   # ticks
        std   = np.array([r[2] for r in d["BS"]]) / 1e3
        c = COLORS[i % len(COLORS)]
        ax.errorbar(iters, mean, yerr=std, fmt="o-", color=c,
                    capsize=4, label=d["platform"])
    ax.set_xlabel("Burn iterations (×1000)")
    ax.set_ylabel("Consumed ticks")
    ax.set_title("Phase 2: Burn Cost vs Iterations")
    ax.legend(fontsize=9)

    # --- right: ticks per 1000 iters (should be flat) ---
    ax = axes[1]
    for i, d in enumerate(datasets):
        iters = np.array([r[0] for r in d["BS"]]) / 1e3
        mean  = np.array([r[1] for r in d["BS"]]) / 1e3
        std   = np.array([r[2] for r in d["BS"]]) / 1e3
        cost  = mean / iters          # ticks per k-iter
        err   = std  / iters
        c = COLORS[i % len(COLORS)]
        ax.errorbar(iters, cost, yerr=err, fmt="s-", color=c,
                    capsize=4, label=d["platform"])
    ax.set_xlabel("Burn iterations (×1000)")
    ax.set_ylabel("Ticks per 1k iters")
    ax.set_title("Burn efficiency (should be constant)")
    ax.legend(fontsize=9)

    # annotate the 400k operating point
    for i, d in enumerate(datasets):
        for r in d["BS"]:
            if r[0] == 400000:
                ticks = r[1] / 1e3
                ax = axes[0]
                ax.axhline(y=ticks, color=COLORS[i % len(COLORS)],
                           ls=":", alpha=0.5)
                ax.annotate(f'  {ticks:.2f} ticks',
                            xy=(400, ticks), fontsize=9,
                            color=COLORS[i % len(COLORS)])

    fig.suptitle("Calibration: Burn Load is Linear in Iterations", y=1.02)
    fig.tight_layout()
    fig.savefig(out)
    print(f"[saved] {out}")
    plt.close(fig)


# ── Figure 3: Drift + Summary ─────────────────────────────────
def plot_summary(datasets, out="./logs/sched/calibrate/calib_summary.png"):
    fig, axes = plt.subplots(1, 2, figsize=(11, 4.5))

    # --- left: drift bar chart ---
    ax = axes[0]
    names  = [d["platform"] for d in datasets]
    drifts = [d["DRIFT"][2] if d["DRIFT"] else 0 for d in datasets]
    bars = ax.bar(names, drifts, color=[COLORS[i % len(COLORS)]
                                        for i in range(len(datasets))],
                  width=0.5, edgecolor="black", linewidth=0.8)
    ax.axhline(y=5, color="red", ls="--", lw=1.5, label="5% threshold")
    for bar, v in zip(bars, drifts):
        ax.text(bar.get_x() + bar.get_width() / 2, v + 0.3,
                f"{v}%", ha="center", fontsize=12, fontweight="bold")
    ax.set_ylabel("Drift (%)")
    ax.set_title("Phase 3: QEMU Simulation Drift")
    ax.set_ylim(0, max(max(drifts, default=1) * 1.5, 8))
    ax.legend(fontsize=9)

    # --- right: calibration summary table ---
    ax = axes[1]
    ax.axis("off")
    rows = []
    for d in datasets:
        slope_ms = d["LR"][0] / 1e6 if d["LR"] else 0   # ms/tick
        r2 = d["LR"][2] / 1e4 if d["LR"] else 0
        burn400 = "?"
        for r in d["BS"]:
            if r[0] == 400000:
                burn400 = f'{r[1]/1e3:.2f} +/- {r[2]/1e3:.2f}'
        drift = f'{d["DRIFT"][2]}%' if d["DRIFT"] else "?"
        rows.append([d["platform"], f"{slope_ms:.2f}", f"{r2:.4f}",
                     burn400, drift])

    table = ax.table(
        cellText=rows,
        colLabels=["Platform", "ms/tick", "R²",
                   "burn(400k) ticks", "Drift"],
        loc="center", cellLoc="center",
    )
    table.auto_set_font_size(False)
    table.set_fontsize(10)
    table.scale(1.0, 1.6)
    # header style
    for j in range(5):
        table[0, j].set_facecolor("#374151")
        table[0, j].set_text_props(color="white", fontweight="bold")
    ax.set_title("Calibration Summary", pad=20)

    fig.tight_layout()
    fig.savefig(out)
    print(f"[saved] {out}")
    plt.close(fig)


# ── main ───────────────────────────────────────────────────────
def main():
    if len(sys.argv) < 2:
        print(f"Usage: {sys.argv[0]} <calib.log> [calib2.log ...]")
        sys.exit(1)

    datasets = [parse_log(p) for p in sys.argv[1:]]
    for d, p in zip(datasets, sys.argv[1:]):
        n_pts = len(d["L"]) + len(d["B"])
        print(f"[parsed] {p}: platform={d['platform']}, "
              f"{len(d['L'])} linearity pts, {len(d['B'])} burn pts")

    plot_linearity(datasets)
    plot_burn(datasets)
    plot_summary(datasets)
    print("\nAll done. Go run experiment 0!")


if __name__ == "__main__":
    main()