#!/usr/bin/env python3
"""
stat_exp2.py -- Exp 2: multi-config edge trade-off.
Outputs to ./logs/sched/edge/ by default.

Usage:
    python3 stat_exp2.py ./logs/sched/edge/edge.csv
    python3 stat_exp2.py ./logs/sched/edge/edge.csv --detail medium
"""

import sys
import os
import re
import numpy as np
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

plt.rcParams.update({
    "font.family": "DejaVu Sans", "font.size": 11,
    "figure.dpi": 150, "savefig.dpi": 200,
    "savefig.bbox": "tight", "axes.grid": True, "grid.alpha": 0.3,
})

COLORS_CFG = {
    "light":   "#16a34a",
    "medlo":   "#2563eb",
    "medium":  "#d97706",
    "heavy":   "#dc2626",
}


# ------------------------------------------------------------------ parse
def parse(path):
    runs = []
    cur = None
    skip = False

    with open(path) as f:
        for line in f:
            line = line.strip()
            if not line:
                continue

            if line.startswith("# WARMUP"):
                skip = True
                cur = None
                continue

            m = re.search(r'# RUN config=(\w+) alpha=(\d+) rep=(\d+)/\d+', line)
            if m:
                if cur is not None and not skip:
                    runs.append(cur)
                skip = False
                cur = {
                    "config": m.group(1), "alpha": int(m.group(2)), "rep": int(m.group(3)),
                    "W": [], "J": [], "S": [], "D": []
                }
                continue

            if skip or cur is None:
                continue

            p = line.split(",")
            tag = p[0]
            try:
                if tag == "W" and len(p) >= 8:
                    cur["W"].append({"win": int(p[1]), "name": p[4],
                                     "run_delta": int(p[5])})
                elif tag == "J" and len(p) >= 12:
                    cur["J"].append({"name": p[2], "jobs": int(p[4]), "miss": int(p[5]),
                                     "late_sum": int(p[6]), "late_max": int(p[7])})
                elif tag == "S" and len(p) >= 4:
                    cur["S"].append({"win": int(p[1]), "jain_q": int(p[3])})
                elif tag == "D" and len(p) >= 6:
                    cur["D"].append({"win": int(p[1]), "jobs_delta": int(p[3]),
                                     "miss_delta": int(p[4])})
            except (IndexError, ValueError):
                continue

    if cur is not None and not skip:
        runs.append(cur)
    return runs


def compute(run):
    s = {"config": run["config"], "alpha": run["alpha"], "rep": run["rep"]}
    for j in run["J"]:
        if j["name"] == "ctrl":
            s["miss_rate"] = j["miss"] / j["jobs"] * 100.0 if j["jobs"] > 0 else 0.0
            s["avg_late"] = j["late_sum"] / j["miss"] if j["miss"] > 0 else 0.0
            s["max_late"] = j["late_max"]
    ws = [w for w in run["W"] if w["win"] > 3]
    for name in ["ctrl", "ai", "log"]:
        s.setdefault("work", {})[name] = sum(w["run_delta"] for w in ws if w["name"] == name)
    if run["D"]:
        rates = [d["miss_delta"] / d["jobs_delta"] * 100 if d["jobs_delta"] > 0 else 0
                 for d in run["D"]]
        s["win_miss"] = np.array(rates)
    if run["S"]:
        s["jain"] = np.mean([x["jain_q"] for x in run["S"]]) / 1000.0
    return s


def aggregate(runs):
    from collections import defaultdict
    by_cfg_alpha = defaultdict(list)
    for r in runs:
        by_cfg_alpha[(r["config"], r["alpha"])].append(r)

    stats = []
    for (cfg, alpha), reps in sorted(by_cfg_alpha.items()):
        row = {"config": cfg, "alpha": alpha, "nreps": len(reps)}
        for key in ["miss_rate", "avg_late", "max_late", "jain"]:
            vals = [r.get(key, 0) for r in reps if key in r or key == "jain"]
            if vals:
                row[key + "_mean"] = np.mean(vals)
                row[key + "_std"] = np.std(vals)
        for name in ["ctrl", "ai", "log"]:
            vals = [r.get("work", {}).get(name, 0) for r in reps]
            if vals:
                row.setdefault("work", {})[name + "_mean"] = np.mean(vals)
                row.setdefault("work", {})[name + "_std"] = np.std(vals)
        stats.append(row)
    return stats


# ------------------------------------------------------------------ print
def print_summary(stats):
    print("=" * 100)
    print("EXPERIMENT 2: MULTI-CONFIG EDGE TRADE-OFF")
    print("=" * 100)
    hdr = f"{'config':>8} {'α':>4} {'miss%':>7} {'±':>5} {'ai_rd':>9} {'ctrl_rd':>9} {'Jain':>6}"
    print(hdr)
    print("-" * 100)
    for s in stats:
        w = s.get("work", {})
        print(f"{s['config']:>8} {s['alpha']:>4} "
              f"{s.get('miss_rate_mean',0):>7.2f} {s.get('miss_rate_std',0):>5.2f} "
              f"{w.get('ai_mean',0):>9.0f} {w.get('ctrl_mean',0):>9.0f} "
              f"{s.get('jain_mean',0):>6.3f}")
    print("=" * 100)

    print("\n--- Knee Points (miss<5% max α) ---")
    configs = sorted(set(s["config"] for s in stats))
    for cfg in configs:
        pts = [s for s in stats if s["config"] == cfg]
        knee = None
        for s in reversed(pts):
            if s.get("miss_rate_mean", 100) < 5.0:
                knee = s["alpha"]
                break
        print(f"  {cfg:>8}: α* = {knee if knee is not None else 'N/A'}")


# ------------------------------------------------------------------ plots
def plot_miss_all(stats, outdir):
    configs = ["light", "medlo", "medium", "heavy"]
    alphas = np.array([0, 10, 20, 30, 40, 50, 60, 70, 80, 90, 100])

    fig, axes = plt.subplots(2, 2, figsize=(12, 10))
    axes = axes.flatten()

    for idx, cfg in enumerate(configs):
        ax = axes[idx]
        pts = [s for s in stats if s["config"] == cfg]
        miss = [s.get("miss_rate_mean", 0) for s in pts]
        err = [s.get("miss_rate_std", 0) for s in pts]
        color = COLORS_CFG.get(cfg, "#666")
        ax.errorbar(alphas, miss, yerr=err, fmt="o-", color=color,
                    capsize=3, lw=1.5, ms=5)
        ax.axhline(5, color="gray", ls="--", lw=0.8, label="5% threshold")
        ai_map = {'light': 7, 'medlo': 15, 'medium': 25, 'heavy': 75}
        ax.set_title(f"{cfg} (ai={ai_map[cfg]})")
        ax.set_xlabel("α")
        ax.set_ylabel("ctrl Miss Rate (%)")
        ax.set_ylim(-2, 105)
        ax.legend(fontsize=8)

    fig.suptitle("Exp 2: Miss Rate Curves for 4 Load Levels", fontsize=14)
    fig.tight_layout(rect=[0, 0, 1, 0.96])
    out = os.path.join(outdir, "exp2_miss_all.png")
    fig.savefig(out); print(f"[saved] {out}"); plt.close(fig)


def plot_throughput_all(stats, outdir):
    configs = ["light", "medlo", "medium", "heavy"]
    alphas = np.array([0, 10, 20, 30, 40, 50, 60, 70, 80, 90, 100])

    fig, axes = plt.subplots(2, 2, figsize=(12, 10))
    axes = axes.flatten()

    for idx, cfg in enumerate(configs):
        ax = axes[idx]
        pts = [s for s in stats if s["config"] == cfg]
        for name, color in [("ai", COLORS_CFG.get(cfg, "#666")), ("ctrl", "#666666")]:
            vals = [s.get("work", {}).get(name + "_mean", 0) for s in pts]
            ax.plot(alphas, vals, "o-", color=color, lw=1.2, ms=4, label=name)
        ax.set_title(f"{cfg}")
        ax.set_xlabel("α")
        ax.set_ylabel("run_delta (ticks)")
        ax.legend(fontsize=8)

    fig.suptitle("Exp 2: Throughput Curves for 4 Load Levels", fontsize=14)
    fig.tight_layout(rect=[0, 0, 1, 0.96])
    out = os.path.join(outdir, "exp2_throughput_all.png")
    fig.savefig(out); print(f"[saved] {out}"); plt.close(fig)


def plot_knee_comparison(stats, outdir):
    configs = ["light", "medlo", "medium", "heavy"]
    knees = []
    for cfg in configs:
        pts = [s for s in stats if s["config"] == cfg]
        knee = None
        for s in reversed(pts):
            if s.get("miss_rate_mean", 100) < 5.0:
                knee = s["alpha"]
                break
        knees.append(knee if knee is not None else -5)

    fig, ax = plt.subplots(figsize=(7, 4.5))
    colors = [COLORS_CFG[c] for c in configs]
    bars = ax.bar(configs, knees, color=colors, edgecolor="black", linewidth=0.8, width=0.5)
    for bar, v in zip(bars, knees):
        label = str(v) if v >= 0 else "N/A"
        ax.text(bar.get_x() + bar.get_width() / 2, v + 1.5,
                label, ha="center", fontsize=12, fontweight="bold")
    ax.set_ylabel("Knee Point α* (miss<5%)")
    ax.set_title("Exp 2: Knee Point Shifts with Load Pressure")
    ax.set_ylim(-10, 60)
    ax.axhline(0, color="gray", ls="--", lw=0.8)
    fig.tight_layout()
    out = os.path.join(outdir, "exp2_knee_comparison.png")
    fig.savefig(out); print(f"[saved] {out}"); plt.close(fig)


def plot_medium_detail(runs, outdir):
    """Per-window detail for medium config, all reps."""
    medium_runs = [r for r in runs if r["config"] == "medium"]
    if not medium_runs:
        print("[warn] no medium runs for detail plot")
        return

    fig, axes = plt.subplots(3, 1, figsize=(12, 9), sharex=True)

    for rep_idx, run in enumerate(medium_runs):
        # per-window miss rate
        ds = sorted(run.get("D", []), key=lambda x: x["win"])
        if ds:
            wins = [d["win"] for d in ds]
            rates = [d["miss_delta"] / d["jobs_delta"] * 100 if d["jobs_delta"] > 0 else 0
                     for d in ds]
            axes[0].plot(wins, rates, ".-", lw=0.8, ms=3, alpha=0.7,
                         label=f"α={run['alpha']} rep{run['rep']}")

        # ai throughput
        ws = sorted([w for w in run.get("W", []) if w["name"] == "ai"], key=lambda x: x["win"])
        if ws:
            wins = [w["win"] for w in ws]
            rds = [w["run_delta"] for w in ws]
            axes[1].plot(wins, rds, ".-", lw=0.8, ms=3, alpha=0.7,
                         label=f"α={run['alpha']} rep{run['rep']}")

        # Jain
        ss = sorted(run.get("S", []), key=lambda x: x["win"])
        if ss:
            wins = [s["win"] for s in ss]
            jains = [s["jain_q"] / 1000.0 for s in ss]
            axes[2].plot(wins, jains, ".-", lw=0.8, ms=3, alpha=0.7,
                         label=f"α={run['alpha']} rep{run['rep']}")

    axes[0].set_ylabel("ctrl miss rate (%)")
    axes[0].set_title("Exp 2 Detail: medium config (ai=25, log=9)")
    axes[0].legend(fontsize=7, ncol=4, loc="upper left")
    axes[0].grid(True, alpha=0.3)

    axes[1].set_ylabel("ai run_delta")
    axes[1].grid(True, alpha=0.3)

    axes[2].set_ylabel("Jain index")
    axes[2].set_xlabel("Window")
    axes[2].set_ylim(0, 1.05)
    axes[2].grid(True, alpha=0.3)

    fig.tight_layout()
    out = os.path.join(outdir, "exp2_medium_detail.png")
    fig.savefig(out, dpi=150)
    print(f"[saved] {out}")
    plt.close(fig)


# ------------------------------------------------------------------ main
def main():
    if len(sys.argv) < 2:
        print(f"Usage: {sys.argv[0]} <edge.csv> [--detail <config>]")
        sys.exit(1)

    path = sys.argv[1]
    detail_cfg = None
    if "--detail" in sys.argv:
        i = sys.argv.index("--detail")
        if i + 1 < len(sys.argv):
            detail_cfg = sys.argv[i + 1]

    outdir = os.path.dirname(path) or "./logs/sched/edge"
    os.makedirs(outdir, exist_ok=True)

    runs = parse(path)
    print(f"[parsed] {len(runs)} runs (warmup discarded)")

    computed = [compute(r) for r in runs]
    stats = aggregate(computed)

    print()
    print_summary(stats)

    plot_miss_all(stats, outdir)
    plot_throughput_all(stats, outdir)
    plot_knee_comparison(stats, outdir)

    if detail_cfg:
        plot_medium_detail([r for r in runs if r["config"] == detail_cfg], outdir)
    else:
        plot_medium_detail(runs, outdir)

    print("\nDone.")


if __name__ == "__main__":
    main()