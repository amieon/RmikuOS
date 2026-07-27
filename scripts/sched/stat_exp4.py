#!/usr/bin/env python3
"""
stat_exp4_v2.py -- Exp 4 v2: Dynamic load, tighter deadline.
Config: ctrl(period=4,cpu=2), ai=150, log=32, total=72000, nreps=3.
Fixed: 0/30/60/100. AIMD: 0/50/100.

Usage:
    python3 stat_exp4_v2.py ./logs/sched/dyn/sexp4_dyn_v2.csv
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

COLORS = {
    "fixed0":   "#94a3b8",
    "fixed30":  "#64748b",
    "fixed60":  "#475569",
    "fixed100": "#1e293b",
    "aimd15":    "#0891b2",
    "aimd50":   "#2563eb",
    "aimd100":  "#dc2626",
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
                if cur is not None and not skip:
                    runs.append(cur)
                skip = True
                cur = None
                continue

            # # RUN config=dyn mode=fixed alpha=30 rep=1/3
            # # RUN config=dyn mode=aimd alpha0=50 rep=1/3
            m = re.search(r'# RUN config=(\w+) mode=(\w+)(?: alpha=(\d+)| alpha0=(\d+)) rep=(\d+)/\d+', line)
            if m:
                if cur is not None and not skip:
                    runs.append(cur)
                skip = False
                config = m.group(1)
                mode = m.group(2)
                alpha = int(m.group(3)) if m.group(3) else int(m.group(4))
                rep = int(m.group(5))
                if mode == "fixed":
                    mode_key = f"fixed{alpha}"
                else:
                    mode_key = f"aimd{alpha}"
                cur = {
                    "config": config, "mode": mode_key, "alpha0": alpha, "rep": rep,
                    "W": [], "D": [], "A": [], "S": [], "J": [],
                }
                continue

            if skip or cur is None:
                continue

            p = line.split(",")
            tag = p[0]
            try:
                if tag == "W" and len(p) >= 8:
                    cur["W"].append({"win": int(p[1]), "name": p[4], "run_delta": int(p[5])})
                elif tag == "D" and len(p) >= 6:
                    cur["D"].append({"win": int(p[1]), "jobs_delta": int(p[3]),
                                     "miss_delta": int(p[4]), "late_delta": int(p[5])})
                elif tag == "A" and len(p) >= 5:
                    cur["A"].append({"win": int(p[1]), "before": int(p[2]),
                                     "after": int(p[3]), "action": p[4]})
                elif tag == "S" and len(p) >= 4:
                    cur["S"].append({"win": int(p[1]), "alpha": int(p[2]), "jain_q": int(p[3])})
                elif tag == "J" and len(p) >= 12:
                    cur["J"].append({"name": p[2], "jobs": int(p[4]), "miss": int(p[5]),
                                     "late_sum": int(p[6]), "late_max": int(p[7])})
            except (IndexError, ValueError):
                continue

    if cur is not None and not skip:
        runs.append(cur)
    return runs


# ------------------------------------------------------------------ compute
def phase_of(win, total_wins):
    seg = total_wins // 3
    if win <= seg:
        return 0
    elif win <= 2 * seg:
        return 1
    else:
        return 2

def compute(run):
    s = {"mode": run["mode"], "alpha0": run["alpha0"], "rep": run["rep"]}

    max_win = 0
    for d in run["D"]:
        max_win = max(max_win, d["win"])
    for w in run["W"]:
        max_win = max(max_win, w["win"])
    total_wins = max_win if max_win > 0 else 720

    miss_by_win = {}
    jobs_by_win = {}
    ai_rd_by_win = {}
    alpha_by_win = {}

    for d in run["D"]:
        miss_by_win[d["win"]] = d["miss_delta"]
        jobs_by_win[d["win"]] = d["jobs_delta"]

    for w in run["W"]:
        if w["name"] == "ai":
            ai_rd_by_win[w["win"]] = w["run_delta"]

    for a in run["A"]:
        alpha_by_win[a["win"]] = a["after"]

    if not alpha_by_win and run["D"]:
        for d in run["D"]:
            alpha_by_win[d["win"]] = run["alpha0"]

    miss_rates = []
    alphas = []
    ai_rds = []

    phase_stats = {0: {"miss": 0, "jobs": 0, "ai_rd": 0, "count": 0},
                   1: {"miss": 0, "jobs": 0, "ai_rd": 0, "count": 0},
                   2: {"miss": 0, "jobs": 0, "ai_rd": 0, "count": 0}}

    for w in range(1, total_wins + 1):
        ph = phase_of(w, total_wins)
        jobs = jobs_by_win.get(w, 0)
        miss = miss_by_win.get(w, 0)
        mr = (miss / jobs * 100.0) if jobs > 0 else 0.0
        miss_rates.append(mr)
        phase_stats[ph]["miss"] += miss
        phase_stats[ph]["jobs"] += jobs
        phase_stats[ph]["count"] += 1

        ai_rd = ai_rd_by_win.get(w, 0)
        ai_rds.append(ai_rd)
        phase_stats[ph]["ai_rd"] += ai_rd

        alphas.append(alpha_by_win.get(w, run["alpha0"]))

    s["total_wins"] = total_wins
    s["miss_rates"] = np.array(miss_rates)
    s["alphas"] = np.array(alphas)
    s["ai_rds"] = np.array(ai_rds)

    for ph in [0, 1, 2]:
        st = phase_stats[ph]
        s[f"ph{ph}_miss_rate"] = (st["miss"] / st["jobs"] * 100.0) if st["jobs"] > 0 else 0.0
        s[f"ph{ph}_ai_rd"] = st["ai_rd"] / st["count"] if st["count"] > 0 else 0.0

    for j in run["J"]:
        if j["name"] == "ctrl":
            s["jobs"] = j["jobs"]
            s["miss"] = j["miss"]
            s["miss_rate"] = j["miss"] / j["jobs"] * 100.0 if j["jobs"] > 0 else 0.0
            s["avg_late"] = j["late_sum"] / j["miss"] if j["miss"] > 0 else 0.0
            s["max_late"] = j["late_max"]

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

        for field in ["miss_rate", "avg_late", "max_late", "jain"]:
            vals = [r.get(field, 0) for r in reps if field in r]
            if vals:
                row[f"{field}_mean"] = np.mean(vals)
                row[f"{field}_std"] = np.std(vals)

        for ph in [0, 1, 2]:
            for metric in ["miss_rate", "ai_rd"]:
                kn = f"ph{ph}_{metric}"
                vals = [r.get(kn, 0) for r in reps]
                if vals:
                    row[f"{kn}_mean"] = np.mean(vals)
                    row[f"{kn}_std"] = np.std(vals)

        for traj in ["miss_rates", "alphas", "ai_rds"]:
            traj_list = [r[traj] for r in reps if traj in r]
            if traj_list:
                min_len = min(len(t) for t in traj_list)
                aligned = np.array([t[:min_len] for t in traj_list])
                row[f"{traj}_mean"] = aligned.mean(axis=0)
                row[f"{traj}_std"] = aligned.std(axis=0)

        stats.append(row)
    return stats


# ------------------------------------------------------------------ print
def print_summary(stats):
    print("=" * 120)
    print("EXPERIMENT 4 v2: DYNAMIC LOAD (tighter deadline) — Fixed vs AIMD")
    print("=" * 120)
    print(f"{'mode':>10} {'α/α0':>4} {'miss%':>7} {'±':>5} {'P0_miss':>8} {'P1_miss':>8} {'P2_miss':>8} {'Jain':>6}")
    print("-" * 120)
    for s in stats:
        print(f"{s.get('mode',''):>10} {s.get('alpha0',0):>4} "
              f"{s.get('miss_rate_mean',0):>7.2f} {s.get('miss_rate_std',0):>5.2f} "
              f"{s.get('ph0_miss_rate_mean',0):>8.2f} "
              f"{s.get('ph1_miss_rate_mean',0):>8.2f} "
              f"{s.get('ph2_miss_rate_mean',0):>8.2f} "
              f"{s.get('jain_mean',0):>6.3f}")
    print("=" * 120)


# ------------------------------------------------------------------ plots
def add_phase_background(ax, total_wins):
    seg = total_wins // 3
    ax.axvspan(0, seg, facecolor="#dbeafe", alpha=0.3, zorder=0)
    ax.axvspan(seg, 2 * seg, facecolor="#fee2e2", alpha=0.3, zorder=0)
    ax.axvspan(2 * seg, total_wins, facecolor="#dbeafe", alpha=0.3, zorder=0)
    ax.axvline(seg, color="gray", ls="--", lw=0.8, alpha=0.5)
    ax.axvline(2 * seg, color="gray", ls="--", lw=0.8, alpha=0.5)


def plot_miss_all(stats, outdir):
    """Fig 1: overall miss rate, all 7 configs."""
    fig, ax = plt.subplots(figsize=(10, 5))
    modes = ["fixed0", "fixed30", "fixed60", "fixed100", "aimd15", "aimd50", "aimd100"]
    colors = [COLORS.get(m, "#666") for m in modes]
    vals, errs = [], []
    for m in modes:
        s = next((x for x in stats if x.get("mode") == m), None)
        vals.append(s.get("miss_rate_mean", 0) if s else 0)
        errs.append(s.get("miss_rate_std", 0) if s else 0)
    bars = ax.bar(range(len(modes)), vals, yerr=errs, capsize=4, color=colors,
                  edgecolor="black", linewidth=0.8, width=0.5)
    for bar, v in zip(bars, vals):
        ax.text(bar.get_x() + bar.get_width() / 2, v + max(vals) * 0.02 if max(vals) > 0 else 1,
                f"{v:.1f}%", ha="center", fontsize=9, fontweight="bold")
    ax.set_xticks(range(len(modes)))
    ax.set_xticklabels(modes, rotation=30, ha="right", fontsize=9)
    ax.set_ylabel("ctrl Miss Rate (%)")
    ax.set_title("Exp 4 v2: Overall Miss Rate (Fixed 0/30/60/100 + AIMD 0/50/100)")
    ax.set_ylim(0, 105)
    fig.tight_layout()
    out = os.path.join(outdir, "exp4v2_miss_all.png")
    fig.savefig(out)
    print(f"[saved] {out}")
    plt.close(fig)


def plot_alpha_traj(runs, outdir):
    """Fig 2: AIMD alpha trajectory with phase background."""
    fig, ax = plt.subplots(figsize=(14, 4.5))
    total_wins = runs[0].get("total_wins", 720) if runs else 720
    add_phase_background(ax, total_wins)

    for mode, color in [("aimd15", COLORS["aimd15"]),
                         ("aimd50", COLORS["aimd50"]),
                         ("aimd100", COLORS["aimd100"])]:
        reps = [r for r in runs if r["mode"] == mode and "alphas" in r]
        if not reps:
            continue
        
        # 画每个 rep 的完整细线
        max_len = max(len(r["alphas"]) for r in reps)
        for r in reps:
            x = np.arange(len(r["alphas"]))
            ax.plot(x, r["alphas"], "-", color=color, lw=0.4, alpha=0.3)
        
        # 用 nanmean 处理不等长数组，不再截断
        padded = np.full((len(reps), max_len), np.nan)
        for i, r in enumerate(reps):
            padded[i, :len(r["alphas"])] = r["alphas"]
        mean = np.nanmean(padded, axis=0)
        # 只画有数据的部分
        valid = ~np.isnan(mean)
        ax.plot(np.arange(max_len)[valid], mean[valid], "-", color=color, lw=2,
                label=f"α0={mode.replace('aimd','')} (n={len(reps)})")

    ax.set_xlabel("Window")
    ax.set_ylabel("α")
    ax.set_title("Exp 4 v2: AIMD α Trajectory under Dynamic Load")
    ax.legend(loc="upper right")
    ax.set_ylim(-2, 105)
    fig.tight_layout()
    out = os.path.join(outdir, "exp4v2_alpha_traj.png")
    fig.savefig(out); print(f"[saved] {out}"); plt.close(fig)


def plot_miss_traj(runs, outdir):
    """Fig 3: per-window miss rate, all modes."""
    fig, ax = plt.subplots(figsize=(14, 4.5))
    total_wins = runs[0].get("total_wins", 720) if runs else 720
    add_phase_background(ax, total_wins)

    modes = ["fixed0", "fixed30", "fixed60", "fixed100", "aimd15", "aimd50", "aimd100"]
    for mode in modes:
        reps = [r for r in runs if r["mode"] == mode and "miss_rates" in r]
        if not reps:
            continue
        min_len = min(len(r["miss_rates"]) for r in reps)
        aligned = np.array([r["miss_rates"][:min_len] for r in reps])
        mean = aligned.mean(axis=0)
        x = np.arange(len(mean))
        color = COLORS.get(mode, "#666")
        lw = 1.5 if mode.startswith("aimd") else 1.0
        ls = "-" if mode.startswith("aimd") else "--"
        ax.plot(x, mean, ls, color=color, lw=lw, label=mode)

    ax.set_xlabel("Window")
    ax.set_ylabel("ctrl Miss Rate (%)")
    ax.set_title("Exp 4 v2: Per-window Miss Rate")
    ax.legend(ncol=2, fontsize=9)
    ax.set_ylim(-2, 105)
    fig.tight_layout()
    out = os.path.join(outdir, "exp4v2_miss_traj.png")
    fig.savefig(out)
    print(f"[saved] {out}")
    plt.close(fig)


def plot_phase_comparison(stats, outdir):
    """Fig 4: phase-resolved miss and throughput."""
    fig, axes = plt.subplots(1, 2, figsize=(12, 4.5))
    modes = ["fixed0", "fixed30", "fixed60", "fixed100", "aimd15", "aimd50", "aimd100"]
    colors = [COLORS.get(m, "#666") for m in modes]
    x = np.arange(3)
    width = 0.11

    # Left: miss by phase
    ax = axes[0]
    for i, mode in enumerate(modes):
        s = next((x for x in stats if x.get("mode") == mode), None)
        if not s:
            continue
        vals = [s.get(f"ph{ph}_miss_rate_mean", 0) for ph in [0, 1, 2]]
        errs = [s.get(f"ph{ph}_miss_rate_std", 0) for ph in [0, 1, 2]]
        offset = (i - 3) * width
        ax.bar(x + offset, vals, width, yerr=errs, capsize=2,
               color=colors[i], edgecolor="black", linewidth=0.4, label=mode)
    ax.set_xticks(x)
    ax.set_xticklabels(["P0 Light", "P1 Heavy", "P2 Light"])
    ax.set_ylabel("ctrl Miss Rate (%)")
    ax.set_title("Miss Rate by Phase")
    ax.legend(ncol=2, fontsize=7)

    # Right: ai throughput by phase
    ax = axes[1]
    for i, mode in enumerate(modes):
        s = next((x for x in stats if x.get("mode") == mode), None)
        if not s:
            continue
        vals = [s.get(f"ph{ph}_ai_rd_mean", 0) for ph in [0, 1, 2]]
        errs = [s.get(f"ph{ph}_ai_rd_std", 0) for ph in [0, 1, 2]]
        offset = (i - 3) * width
        ax.bar(x + offset, vals, width, yerr=errs, capsize=2,
               color=colors[i], edgecolor="black", linewidth=0.4, label=mode)
    ax.set_xticks(x)
    ax.set_xticklabels(["P0 Light", "P1 Heavy", "P2 Light"])
    ax.set_ylabel("ai Avg Run Delta (ticks/window)")
    ax.set_title("ai Throughput by Phase")
    ax.legend(ncol=2, fontsize=7)

    fig.suptitle("Exp 4 v2: Phase-resolved Comparison", fontsize=13)
    fig.tight_layout(rect=[0, 0, 1, 0.96])
    out = os.path.join(outdir, "exp4v2_phase_comparison.png")
    fig.savefig(out)
    print(f"[saved] {out}")
    plt.close(fig)


def plot_transition_zoom(runs, outdir):
    """Fig 5: zoom at both phase boundaries."""
    total_wins = runs[0].get("total_wins", 720) if runs else 720
    seg = total_wins // 3

    fig, axes = plt.subplots(1, 2, figsize=(12, 4.5))

    # Light -> Heavy
    ax = axes[0]
    zoom_start = seg - 15
    zoom_end = seg + 35
    for mode in ["fixed50", "fixed100", "aimd15", "aimd50", "aimd100"]:
        reps = [r for r in runs if r["mode"] == mode and "miss_rates" in r]
        if not reps:
            continue
        min_len = min(len(r["miss_rates"]) for r in reps)
        aligned = np.array([r["miss_rates"][:min_len] for r in reps])
        mean = aligned.mean(axis=0)
        x = np.arange(len(mean))
        mask = (x >= zoom_start) & (x <= zoom_end)
        if not np.any(mask):
            continue
        color = COLORS.get(mode, "#666")
        lw = 2 if mode.startswith("aimd") else 1.2
        ls = "-" if mode.startswith("aimd") else "--"
        ax.plot(x[mask], mean[mask], ls, color=color, lw=lw, label=mode)
    ax.axvline(seg, color="gray", ls="--", lw=1, label="boundary")
    ax.set_xlabel("Window")
    ax.set_ylabel("ctrl Miss Rate (%)")
    ax.set_title("Light → Heavy Transition")
    ax.legend(fontsize=8)

    # Heavy -> Light
    ax = axes[1]
    zoom_start = 2 * seg - 15
    zoom_end = 2 * seg + 35
    for mode in ["fixed50", "fixed100", "aimd15", "aimd50", "aimd100"]:
        reps = [r for r in runs if r["mode"] == mode and "miss_rates" in r]
        if not reps:
            continue
        min_len = min(len(r["miss_rates"]) for r in reps)
        aligned = np.array([r["miss_rates"][:min_len] for r in reps])
        mean = aligned.mean(axis=0)
        x = np.arange(len(mean))
        mask = (x >= zoom_start) & (x <= zoom_end)
        if not np.any(mask):
            continue
        color = COLORS.get(mode, "#666")
        lw = 2 if mode.startswith("aimd") else 1.2
        ls = "-" if mode.startswith("aimd") else "--"
        ax.plot(x[mask], mean[mask], ls, color=color, lw=lw, label=mode)
    ax.axvline(2 * seg, color="gray", ls="--", lw=1, label="boundary")
    ax.set_xlabel("Window")
    ax.set_ylabel("ctrl Miss Rate (%)")
    ax.set_title("Heavy → Light Transition")
    ax.legend(fontsize=8)

    fig.suptitle("Exp 4 v2: Phase Transition Zoom", fontsize=13)
    fig.tight_layout(rect=[0, 0, 1, 0.96])
    out = os.path.join(outdir, "exp4v2_transition_zoom.png")
    fig.savefig(out)
    print(f"[saved] {out}")
    plt.close(fig)


def plot_summary(runs, stats, outdir):
    """Fig 6: 2x2 summary."""
    fig, axes = plt.subplots(2, 2, figsize=(11, 9))

    # Top-left: overall miss
    ax = axes[0, 0]
    modes = ["fixed0", "fixed30", "fixed60", "fixed100", "aimd15", "aimd50", "aimd100"]
    colors = [COLORS.get(m, "#666") for m in modes]
    vals, errs = [], []
    for m in modes:
        s = next((x for x in stats if x.get("mode") == m), None)
        vals.append(s.get("miss_rate_mean", 0) if s else 0)
        errs.append(s.get("miss_rate_std", 0) if s else 0)
    ax.bar(range(len(modes)), vals, yerr=errs, capsize=3, color=colors,
           edgecolor="black", linewidth=0.6, width=0.5)
    for bar, v in zip(ax.patches, vals):
        ax.text(bar.get_x() + bar.get_width() / 2, v + (max(vals) * 0.02 if max(vals) > 0 else 1),
                f"{v:.1f}%", ha="center", fontsize=8, fontweight="bold")
    ax.set_xticks(range(len(modes)))
    ax.set_xticklabels(modes, rotation=45, ha="right", fontsize=8)
    ax.set_ylabel("miss%")
    ax.set_title("Overall Miss Rate")

    # Top-right: ai total throughput
    ax = axes[0, 1]
    vals, errs = [], []
    for m in modes:
        reps = [r for r in runs if r["mode"] == m]
        if reps:
            totals = [float(np.sum(r.get("ai_rds", []))) for r in reps]
            vals.append(np.mean(totals))
            errs.append(np.std(totals))
        else:
            vals.append(0)
            errs.append(0)
    ax.bar(range(len(modes)), vals, yerr=errs, capsize=3, color=colors,
           edgecolor="black", linewidth=0.6, width=0.5)
    ax.set_xticks(range(len(modes)))
    ax.set_xticklabels(modes, rotation=45, ha="right", fontsize=8)
    ax.set_ylabel("ai total run_delta")
    ax.set_title("Overall ai Throughput")

    # Bottom-left: alpha traj (aimd only)
    ax = axes[1, 0]
    total_wins = runs[0].get("total_wins", 720) if runs else 720
    add_phase_background(ax, total_wins)
    for mode, color in [("aimd15", COLORS["aimd15"]),
                         ("aimd50", COLORS["aimd50"]),
                         ("aimd100", COLORS["aimd100"])]:
        reps = [r for r in runs if r["mode"] == mode and "alphas" in r]
        if not reps:
            continue
        min_len = min(len(r["alphas"]) for r in reps)
        aligned = np.array([r["alphas"][:min_len] for r in reps])
        mean = aligned.mean(axis=0)
        ax.plot(np.arange(len(mean)), mean, "-", color=color, lw=1.5,
                label=f"α0={mode.replace('aimd','')}")
    ax.set_xlabel("Window")
    ax.set_ylabel("α")
    ax.set_title("α Trajectory (AIMD)")
    ax.legend(fontsize=9)

    # Bottom-right: P1 heavy phase miss only
    ax = axes[1, 1]
    vals, errs = [], []
    for m in modes:
        s = next((x for x in stats if x.get("mode") == m), None)
        vals.append(s.get("ph1_miss_rate_mean", 0) if s else 0)
        errs.append(s.get("ph1_miss_rate_std", 0) if s else 0)
    ax.bar(range(len(modes)), vals, yerr=errs, capsize=3, color=colors,
           edgecolor="black", linewidth=0.6, width=0.5)
    for bar, v in zip(ax.patches, vals):
        ax.text(bar.get_x() + bar.get_width() / 2, v + (max(vals) * 0.02 if max(vals) > 0 else 1),
                f"{v:.1f}%", ha="center", fontsize=8, fontweight="bold")
    ax.set_xticks(range(len(modes)))
    ax.set_xticklabels(modes, rotation=45, ha="right", fontsize=8)
    ax.set_ylabel("miss%")
    ax.set_title("Heavy Phase (P1) Miss Rate")

    fig.suptitle("Exp 4 v2: Summary", fontsize=13)
    fig.tight_layout(rect=[0, 0, 1, 0.96])
    out = os.path.join(outdir, "exp4v2_summary.png")
    fig.savefig(out)
    print(f"[saved] {out}")
    plt.close(fig)


# ------------------------------------------------------------------ main
def main():
    if len(sys.argv) < 2:
        print(f"Usage: {sys.argv[0]} <sexp4_dyn_v2.csv>")
        sys.exit(1)

    path = sys.argv[1]
    outdir = os.path.dirname(path) or "./logs/sched/dyn"
    os.makedirs(outdir, exist_ok=True)

    runs = parse(path)
    print(f"[parsed] {len(runs)} runs (warmup discarded)")

    computed = [compute(r) for r in runs]
    stats = aggregate(computed, ["mode", "alpha0"])

    print()
    print_summary(stats)

    plot_miss_all(stats, outdir)
    plot_alpha_traj(computed, outdir)
    plot_miss_traj(computed, outdir)
    plot_phase_comparison(stats, outdir)
    plot_transition_zoom(computed, outdir)
    plot_summary(computed, stats, outdir)

    print("\nDone.")


if __name__ == "__main__":
    main()