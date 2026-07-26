#!/usr/bin/env python3
"""
stat_exp3.py -- Exp 3: AIMD constant load, multi-config, multi-start.
Handles 4 configs (light/medlo/medium/heavy) x fixed50 + aimd0/50/100.
Medlo gets dedicated deep-dive plots; others get summary.

Usage:
    python3 stat_exp3.py ./logs/sched/aimd/sexp3_aimd.csv
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
COLORS_MODE = {
    "fixed":   "#666666",
    "aimd0":   "#0891b2",
    "aimd50":  "#2563eb",
    "aimd100": "#dc2626",
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

            # warmup delimiter
            if line.startswith("# WARMUP"):
                if cur is not None and not skip:
                    runs.append(cur)
                skip = True
                cur = None
                continue

            # formal run delimiter
            # e.g. # RUN config=medlo mode=aimd alpha0=50 rep=1/5
            # e.g. # RUN config=medlo mode=fixed alpha=50 rep=1/5
            m = re.search(r'# RUN config=(\w+) mode=(\w+)(?: alpha0=(\d+)| alpha=(\d+)) rep=(\d+)/\d+', line)
            if m:
                if cur is not None and not skip:
                    runs.append(cur)
                skip = False
                config = m.group(1)
                mode = m.group(2)
                alpha = int(m.group(3)) if m.group(3) else int(m.group(4))
                rep = int(m.group(5))
                # normalize mode name
                if mode == "aimd":
                    mode_key = f"aimd{alpha}"
                else:
                    mode_key = "fixed"
                cur = {
                    "config": config, "mode": mode_key, "alpha0": alpha, "rep": rep,
                    "W": [], "D": [], "A": [], "S": [], "J": [], "K": []
                }
                continue

            if skip or cur is None:
                continue

            p = line.split(",")
            tag = p[0]
            try:
                if tag == "W" and len(p) >= 8:
                    cur["W"].append({"win": int(p[1]), "name": p[4],
                                   "run_delta": int(p[5]), "eff_tickets": int(p[6])})
                elif tag == "D" and len(p) >= 6:
                    cur["D"].append({"win": int(p[1]), "jobs_delta": int(p[3]),
                                     "miss_delta": int(p[4]), "late_delta": int(p[5])})
                elif tag == "A" and len(p) >= 5:
                    cur["A"].append({"win": int(p[1]), "before": int(p[2]),
                                     "after": int(p[3]), "action": p[4]})
                elif tag == "S" and len(p) >= 4:
                    cur["S"].append({"win": int(p[1]), "jain_q": int(p[3]),
                                     "max_slowdown_q": int(p[4])})
                elif tag == "J" and len(p) >= 12:
                    cur["J"].append({"name": p[2], "jobs": int(p[4]), "miss": int(p[5]),
                                     "late_sum": int(p[6]), "late_max": int(p[7]),
                                     "resp_sum": int(p[8]), "resp_sumsq": int(p[9]),
                                     "resp_min": int(p[10]), "resp_max": int(p[11])})
                elif tag == "K" and len(p) >= 5:
                    cur["K"].append({"name": p[2], "work": int(p[4])})
            except (IndexError, ValueError):
                continue

    if cur is not None and not skip:
        runs.append(cur)
    return runs


# ------------------------------------------------------------------ compute
def compute(run):
    s = {"config": run["config"], "mode": run["mode"], "alpha0": run["alpha0"], "rep": run["rep"]}

    # ctrl deadline from J
    for j in run["J"]:
        if j["name"] == "ctrl":
            s["jobs"] = j["jobs"]
            s["miss"] = j["miss"]
            s["miss_rate"] = j["miss"] / j["jobs"] * 100.0 if j["jobs"] > 0 else 0.0
            s["avg_late"] = j["late_sum"] / j["miss"] if j["miss"] > 0 else 0.0
            s["max_late"] = j["late_max"]
            n = j["jobs"]
            if n > 0:
                mean = j["resp_sum"] / n
                var = j["resp_sumsq"] / n - mean * mean
                s["resp_std"] = np.sqrt(max(var, 0))
            else:
                s["resp_std"] = 0.0

    # throughput from W (skip first 3 windows)
    ws = [w for w in run["W"] if w["win"] > 3]
    for name in ["ctrl", "ai", "log"]:
        s.setdefault("work", {})[name] = sum(w["run_delta"] for w in ws if w["name"] == name)

    # per-window miss rate from D
    if run["D"]:
        rates = []
        for d in run["D"]:
            if d["jobs_delta"] > 0:
                rates.append(d["miss_delta"] / d["jobs_delta"] * 100.0)
            else:
                rates.append(0.0)
        s["win_miss"] = np.array(rates)

    # alpha trajectory from A
    if run["A"]:
        s["alpha_traj"] = np.array([a["after"] for a in run["A"]])
        s["alpha_win"] = np.array([a["win"] for a in run["A"]])
        actions = {}
        for a in run["A"]:
            actions[a["action"]] = actions.get(a["action"], 0) + 1
        s["actions"] = actions

    # Jain
    if run["S"]:
        s["jain"] = np.mean([x["jain_q"] for x in run["S"]]) / 1000.0

    return s


def aggregate(runs, group_keys):
    from collections import defaultdict
    by_group = defaultdict(list)
    for r in runs:
        key = tuple(r[k] for k in group_keys)
        by_group[key].append(r)

    stats = []
    for key, reps in sorted(by_group.items()):
        row = dict(zip(group_keys, key))
        row["nreps"] = len(reps)

        for field in ["miss_rate", "avg_late", "max_late", "resp_std", "jain"]:
            vals = [r.get(field, 0) for r in reps if field in r or field == "jain"]
            if vals:
                row[f"{field}_mean"] = np.mean(vals)
                row[f"{field}_std"] = np.std(vals)

        for name in ["ctrl", "ai", "log"]:
            vals = [r.get("work", {}).get(name, 0) for r in reps]
            if vals:
                row.setdefault("work", {})[f"{name}_mean"] = np.mean(vals)
                row.setdefault("work", {})[f"{name}_std"] = np.std(vals)

        # alpha trajectory alignment (AIMD only)
        if "mode" in row and row["mode"].startswith("aimd"):
            traj_list = [r["alpha_traj"] for r in reps if "alpha_traj" in r]
            if traj_list:
                min_len = min(len(t) for t in traj_list)
                aligned = np.array([t[:min_len] for t in traj_list])
                row["alpha_mean"] = aligned.mean(axis=0)
                row["alpha_std"] = aligned.std(axis=0)

        stats.append(row)
    return stats


# ------------------------------------------------------------------ print
def print_summary(stats):
    print("=" * 110)
    print("EXPERIMENT 3: AIMD CONSTANT LOAD (multi-config, multi-start)")
    print("=" * 110)
    hdr = f"{'config':>8} {'mode':>8} {'α0':>4} {'miss%':>7} {'±':>5} {'ai_rd':>9} {'ctrl_rd':>9} {'Jain':>6}"
    print(hdr)
    print("-" * 110)
    for s in stats:
        w = s.get("work", {})
        print(f"{s.get('config',''):>8} {s.get('mode',''):>8} {s.get('alpha0',0):>4} "
              f"{s.get('miss_rate_mean',0):>7.2f} {s.get('miss_rate_std',0):>5.2f} "
              f"{w.get('ai_mean',0):>9.0f} {w.get('ctrl_mean',0):>9.0f} "
              f"{s.get('jain_mean',0):>6.3f}")
    print("=" * 110)


# ------------------------------------------------------------------ plots
def plot_miss_all(stats, outdir):
    """Fig 1: miss rate comparison across 4 configs."""
    configs = ["light", "medlo", "medium", "heavy"]
    modes = ["fixed", "aimd0", "aimd50", "aimd100"]

    fig, axes = plt.subplots(2, 2, figsize=(12, 10))
    axes = axes.flatten()

    for idx, cfg in enumerate(configs):
        ax = axes[idx]
        for mode in modes:
            pts = [s for s in stats if s.get("config") == cfg and s.get("mode") == mode]
            if not pts:
                continue
            # bar plot: one bar per mode
            label = mode
            color = COLORS_MODE.get(mode, "#666")
            val = pts[0].get("miss_rate_mean", 0)
            err = pts[0].get("miss_rate_std", 0)
            ax.bar(label, val, yerr=err, capsize=4, color=color, edgecolor="black", linewidth=0.8, width=0.6)
        ax.set_title(f"{cfg}")
        ax.set_ylabel("ctrl Miss Rate (%)")
        ax.set_ylim(0, 105)
        ax.grid(True, alpha=0.3, axis="y")

    fig.suptitle("Exp 3: Miss Rate by Config & Mode", fontsize=14)
    fig.tight_layout(rect=[0, 0, 1, 0.96])
    out = os.path.join(outdir, "exp3_miss_all.png")
    fig.savefig(out); print(f"[saved] {out}"); plt.close(fig)


def plot_alpha_traj_medlo(runs, outdir):
    """Fig 2: medlo α trajectory for 3 AIMD starts."""
    fig, ax = plt.subplots(figsize=(12, 4.5))
    for mode, color in [("aimd0", COLORS_MODE["aimd0"]),
                         ("aimd50", COLORS_MODE["aimd50"]),
                         ("aimd100", COLORS_MODE["aimd100"])]:
        reps = [r for r in runs if r["config"] == "medlo" and r["mode"] == mode and "alpha_traj" in r]
        if not reps:
            continue
        min_len = min(len(r["alpha_traj"]) for r in reps)
        aligned = np.array([r["alpha_traj"][:min_len] for r in reps])
        mean = aligned.mean(axis=0)
        std = aligned.std(axis=0)
        x = np.arange(len(mean))
        ax.plot(x, mean, "-", color=color, lw=1.5, label=f"α0={mode.replace('aimd','')}")
        ax.fill_between(x, mean - std, mean + std, alpha=0.2, color=color)
    ax.axhline(40, color="gray", ls="--", lw=1, label="knee α*=40")
    ax.set_xlabel("Window")
    ax.set_ylabel("α")
    ax.set_title("Exp 3: Medlo α Trajectory (3 starts)")
    ax.legend(); ax.set_ylim(-2, 105)
    fig.tight_layout()
    out = os.path.join(outdir, "exp3_alpha_traj_medlo.png")
    fig.savefig(out); print(f"[saved] {out}"); plt.close(fig)


def plot_miss_traj_medlo(runs, outdir):
    """Fig 3: medlo per-window miss rate."""
    fig, ax = plt.subplots(figsize=(12, 4.5))
    for mode, color in [("fixed", COLORS_MODE["fixed"]),
                         ("aimd0", COLORS_MODE["aimd0"]),
                         ("aimd50", COLORS_MODE["aimd50"]),
                         ("aimd100", COLORS_MODE["aimd100"])]:
        reps = [r for r in runs if r["config"] == "medlo" and r["mode"] == mode and "win_miss" in r]
        if not reps:
            continue
        min_len = min(len(r["win_miss"]) for r in reps)
        aligned = np.array([r["win_miss"][:min_len] for r in reps])
        mean = aligned.mean(axis=0)
        std = aligned.std(axis=0)
        x = np.arange(len(mean))
        ax.plot(x, mean, "-", color=color, lw=1.0, label=mode)
        ax.fill_between(x, mean - std, mean + std, alpha=0.15, color=color)
    ax.set_xlabel("Window")
    ax.set_ylabel("ctrl Miss Rate (%)")
    ax.set_title("Exp 3: Medlo Per-window Miss Rate")
    ax.legend(); ax.set_ylim(-2, 105)
    fig.tight_layout()
    out = os.path.join(outdir, "exp3_miss_traj_medlo.png")
    fig.savefig(out); print(f"[saved] {out}"); plt.close(fig)


def plot_comparison_medlo(stats, outdir):
    """Fig 4: medlo fixed vs AIMD bar comparison."""
    fig, axes = plt.subplots(1, 2, figsize=(10, 4))
    modes = ["fixed", "aimd0", "aimd50", "aimd100"]
    colors = [COLORS_MODE[m] for m in modes]

    # left: miss rate
    ax = axes[0]
    vals = []
    errs = []
    for m in modes:
        s = next((x for x in stats if x.get("config") == "medlo" and x.get("mode") == m), None)
        vals.append(s.get("miss_rate_mean", 0) if s else 0)
        errs.append(s.get("miss_rate_std", 0) if s else 0)
    bars = ax.bar(range(len(modes)), vals, yerr=errs, capsize=4, color=colors,
                  edgecolor="black", linewidth=0.8, width=0.5)
    for bar, v in zip(bars, vals):
        ax.text(bar.get_x() + bar.get_width() / 2, v + 1.5,
                f"{v:.1f}%", ha="center", fontsize=10, fontweight="bold")
    ax.set_xticks(range(len(modes)))
    ax.set_xticklabels(modes)
    ax.set_ylabel("ctrl Miss Rate (%)")
    ax.set_title("Medlo: Miss Rate")

    # right: ai throughput
    ax = axes[1]
    vals = []
    errs = []
    for m in modes:
        s = next((x for x in stats if x.get("config") == "medlo" and x.get("mode") == m), None)
        vals.append(s.get("work", {}).get("ai_mean", 0) if s else 0)
        errs.append(s.get("work", {}).get("ai_std", 0) if s else 0)
    bars = ax.bar(range(len(modes)), vals, yerr=errs, capsize=4, color=colors,
                  edgecolor="black", linewidth=0.8, width=0.5)
    for bar, v in zip(bars, vals):
        ax.text(bar.get_x() + bar.get_width() / 2, v + max(vals) * 0.02,
                f"{v:.0f}", ha="center", fontsize=10, fontweight="bold")
    ax.set_xticks(range(len(modes)))
    ax.set_xticklabels(modes)
    ax.set_ylabel("ai Throughput (ticks)")
    ax.set_title("Medlo: Throughput")

    fig.suptitle("Exp 3: Medlo Fixed vs AIMD", fontsize=13)
    fig.tight_layout(rect=[0, 0, 1, 0.96])
    out = os.path.join(outdir, "exp3_comparison_medlo.png")
    fig.savefig(out); print(f"[saved] {out}"); plt.close(fig)


def plot_convergence_medlo(runs, outdir):
    """Fig 5: medlo 3-start convergence overlay."""
    fig, ax = plt.subplots(figsize=(12, 4.5))
    for mode, color in [("aimd0", COLORS_MODE["aimd0"]),
                         ("aimd50", COLORS_MODE["aimd50"]),
                         ("aimd100", COLORS_MODE["aimd100"])]:
        reps = [r for r in runs if r["config"] == "medlo" and r["mode"] == mode and "alpha_traj" in r]
        if not reps:
            continue
        for rep_idx, r in enumerate(reps):
            alpha = r["alpha_traj"]
            x = np.arange(len(alpha))
            ax.plot(x, alpha, "-", color=color, lw=0.6, alpha=0.4)
        # mean
        min_len = min(len(r["alpha_traj"]) for r in reps)
        aligned = np.array([r["alpha_traj"][:min_len] for r in reps])
        mean = aligned.mean(axis=0)
        ax.plot(np.arange(len(mean)), mean, "-", color=color, lw=2,
                label=f"α0={mode.replace('aimd','')} (n={len(reps)})")
    ax.axhline(40, color="gray", ls="--", lw=1, label="knee α*=40")
    ax.set_xlabel("Window")
    ax.set_ylabel("α")
    ax.set_title("Exp 3: Medlo Convergence from 3 Starts")
    ax.legend(); ax.set_ylim(-2, 105)
    fig.tight_layout()
    out = os.path.join(outdir, "exp3_convergence_medlo.png")
    fig.savefig(out); print(f"[saved] {out}"); plt.close(fig)


def plot_summary_config(runs, config, outdir):
    """One summary figure per non-medlo config."""
    fig, axes = plt.subplots(2, 2, figsize=(11, 9))
    modes = ["fixed", "aimd0", "aimd50", "aimd100"]

    # top-left: miss rate bars
    ax = axes[0, 0]
    vals, errs = [], []
    for m in modes:
        reps = [r for r in runs if r["config"] == config and r["mode"] == m]
        if reps:
            vals.append(np.mean([x.get("miss_rate", 0) for x in reps]))
            errs.append(np.std([x.get("miss_rate", 0) for x in reps]))
        else:
            vals.append(0); errs.append(0)
    colors = [COLORS_MODE[m] for m in modes]
    ax.bar(range(len(modes)), vals, yerr=errs, capsize=4, color=colors,
           edgecolor="black", linewidth=0.8, width=0.5)
    ax.set_xticks(range(len(modes))); ax.set_xticklabels(modes, fontsize=9)
    ax.set_ylabel("miss%"); ax.set_title("Miss Rate")

    # top-right: ai throughput
    ax = axes[0, 1]
    vals, errs = [], []
    for m in modes:
        reps = [r for r in runs if r["config"] == config and r["mode"] == m]
        if reps:
            vals.append(np.mean([x.get("work", {}).get("ai", 0) for x in reps]))
            errs.append(np.std([x.get("work", {}).get("ai", 0) for x in reps]))
        else:
            vals.append(0); errs.append(0)
    ax.bar(range(len(modes)), vals, yerr=errs, capsize=4, color=colors,
           edgecolor="black", linewidth=0.8, width=0.5)
    ax.set_xticks(range(len(modes))); ax.set_xticklabels(modes, fontsize=9)
    ax.set_ylabel("ai run_delta"); ax.set_title("Throughput")

    # bottom-left: alpha trajectory (aimd only)
    ax = axes[1, 0]
    for m in ["aimd0", "aimd50", "aimd100"]:
        reps = [r for r in runs if r["config"] == config and r["mode"] == m and "alpha_traj" in r]
        if not reps:
            continue
        min_len = min(len(r["alpha_traj"]) for r in reps)
        aligned = np.array([r["alpha_traj"][:min_len] for r in reps])
        mean = aligned.mean(axis=0)
        ax.plot(np.arange(len(mean)), mean, "-", color=COLORS_MODE[m], lw=1.5,
                label=m.replace("aimd", "α0="))
    ax.set_xlabel("Window"); ax.set_ylabel("α")
    ax.set_title("α Trajectory"); ax.legend(fontsize=9)

    # bottom-right: actions pie (aimd50 reps aggregated)
    ax = axes[1, 1]
    reps = [r for r in runs if r["config"] == config and r["mode"] == "aimd50"]
    actions = {}
    for r in reps:
        for a, c in r.get("actions", {}).items():
            actions[a] = actions.get(a, 0) + c
    if actions:
        labels = list(actions.keys())
        sizes = list(actions.values())
        ax.pie(sizes, labels=labels, autopct="%1.1f%%", startangle=90)
    ax.set_title("AIMD50 Actions")

    fig.suptitle(f"Exp 3: {config} Summary", fontsize=13)
    fig.tight_layout(rect=[0, 0, 1, 0.96])
    out = os.path.join(outdir, f"exp3_summary_{config}.png")
    fig.savefig(out); print(f"[saved] {out}"); plt.close(fig)


def plot_convergence_medlo_clean(runs, outdir):
    """Fig 5b: medlo 3-start convergence, no std shading (clean overlay)."""
    fig, ax = plt.subplots(figsize=(12, 4.5))
    
    for mode, color in [("aimd0", COLORS_MODE["aimd0"]),
                         ("aimd50", COLORS_MODE["aimd50"]),
                         ("aimd100", COLORS_MODE["aimd100"])]:
        reps = [r for r in runs if r["config"] == "medlo" and r["mode"] == mode and "alpha_traj" in r]
        if not reps:
            continue
        
        # 每个 rep 的细线
        for r in reps:
            alpha = r["alpha_traj"]
            x = np.arange(len(alpha))
            ax.plot(x, alpha, "-", color=color, lw=0.5, alpha=0.35)
        
        # 均值粗线
        min_len = min(len(r["alpha_traj"]) for r in reps)
        aligned = np.array([r["alpha_traj"][:min_len] for r in reps])
        mean = aligned.mean(axis=0)
        ax.plot(np.arange(len(mean)), mean, "-", color=color, lw=2.5,
                label=f"α0={mode.replace('aimd','')} mean (n={len(reps)})")
    
    ax.axhline(40, color="gray", ls="--", lw=1.2, label="knee α*=40")
    ax.set_xlabel("Window", fontsize=12)
    ax.set_ylabel("α", fontsize=12)
    ax.set_title("Exp 3: Medlo Convergence from 3 Starts (clean)", fontsize=13)
    ax.legend(loc="upper right", fontsize=10)
    ax.set_ylim(-2, 105)
    fig.tight_layout()
    out = os.path.join(outdir, "exp3_convergence_medlo_clean.png")
    fig.savefig(out); print(f"[saved] {out}"); plt.close(fig)

# ------------------------------------------------------------------ main
def main():
    if len(sys.argv) < 2:
        print(f"Usage: {sys.argv[0]} <sexp3_aimd.csv>"); sys.exit(1)

    path = sys.argv[1]
    outdir = os.path.dirname(path) or "./logs/sched/aimd"
    os.makedirs(outdir, exist_ok=True)

    runs = parse(path)
    print(f"[parsed] {len(runs)} runs (warmup discarded)")

    computed = [compute(r) for r in runs]
    stats = aggregate(computed, ["config", "mode", "alpha0"])

    print()
    print_summary(stats)

    # global
    plot_miss_all(stats, outdir)

    # medlo deep dive
    plot_alpha_traj_medlo(computed, outdir)
    plot_miss_traj_medlo(computed, outdir)
    plot_comparison_medlo(stats, outdir)
    plot_convergence_medlo(computed, outdir)
    plot_convergence_medlo_clean(computed, outdir)
    # other configs summary
    for cfg in ["light", "medium", "heavy"]:
        plot_summary_config(computed, cfg, outdir)

    print("\nDone.")


if __name__ == "__main__":
    main()