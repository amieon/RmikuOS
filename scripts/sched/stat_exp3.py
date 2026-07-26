#!/usr/bin/env python3
"""
stat_exp3.py -- Exp 3: AIMD constant load vs fixed baseline.

Usage:
    python3 stat_exp3.py ./logs/sched/aimd/sexp3_aimd.csv
"""

import sys
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

COLORS = {"aimd": "#2563eb", "fixed": "#dc2626", "ctrl": "#dc2626", "ai": "#16a34a"}


# ------------------------------------------------------------------ parse
def parse(path):
    """
    Delimiters:
      # RUN aimd alpha0=50 rep=N/5
      # RUN fixed alpha=50 rep=N/5
      # WARMUP ... (discarded)
    """
    runs = []
    cur = None
    skip = False

    with open(path) as f:
        for line in f:
            line = line.strip()
            if not line:
                continue

            # warmup: discard
            if line.startswith("# WARMUP"):
                if cur is not None:
                    pass  # discard
                skip = True
                cur = None
                continue

            # formal run
            m = re.search(r'# RUN (aimd|fixed).*?rep=(\d+)/\d+', line)
            if m:
                if cur is not None and not skip:
                    runs.append(cur)
                skip = False
                mode = m.group(1)
                rep = int(m.group(2))
                cur = {
                    "mode": mode, "rep": rep,
                    "W": [], "D": [], "A": [], "S": [], "J": [], "K": []
                }
                continue

            if line.startswith("#"):
                continue
            if cur is None or skip:
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
                    cur["S"].append({"win": int(p[1]), "jain_q": int(p[3])})
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
    s = {"mode": run["mode"], "rep": run["rep"]}

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
        total_rd = sum(w["run_delta"] for w in ws if w["name"] == name)
        s.setdefault("work", {})[name] = total_rd

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


def aggregate(runs, mode):
    reps = [r for r in runs if r["mode"] == mode]
    if not reps:
        return None

    row = {"mode": mode, "nreps": len(reps)}

    # scalar
    for key in ["miss_rate", "avg_late", "max_late", "resp_std", "jain"]:
        vals = [r.get(key, 0) for r in reps if key in r or key == "jain"]
        if vals:
            row[key + "_mean"] = np.mean(vals)
            row[key + "_std"] = np.std(vals)

    # work
    for name in ["ctrl", "ai", "log"]:
        vals = [r.get("work", {}).get(name, 0) for r in reps]
        if vals:
            row.setdefault("work", {})[name + "_mean"] = np.mean(vals)
            row.setdefault("work", {})[name + "_std"] = np.std(vals)

    # alpha trajectory (align by window index, take mean)
    if mode == "aimd":
        traj_list = [r["alpha_traj"] for r in reps if "alpha_traj" in r]
        if traj_list:
            min_len = min(len(t) for t in traj_list)
            aligned = np.array([t[:min_len] for t in traj_list])
            row["alpha_mean"] = aligned.mean(axis=0)
            row["alpha_std"] = aligned.std(axis=0)

    return row


# ------------------------------------------------------------------ print
def print_summary(aimd_agg, fixed_agg):
    print("=" * 90)
    print("EXPERIMENT 3: AIMD CONSTANT LOAD")
    print("=" * 90)

    if fixed_agg:
        print("\n--- Fixed 50 Baseline ---")
        print(f"  reps: {fixed_agg['nreps']}")
        print(f"  miss rate: {fixed_agg.get('miss_rate_mean',0):.2f} ± {fixed_agg.get('miss_rate_std',0):.2f}%")
        print(f"  avg late:  {fixed_agg.get('avg_late_mean',0):.1f} ± {fixed_agg.get('avg_late_std',0):.1f}")
        print(f"  ai work:   {fixed_agg.get('work',{}).get('ai_mean',0):.0f} ± {fixed_agg.get('work',{}).get('ai_std',0):.0f}")

    if aimd_agg:
        print("\n--- AIMD Adaptive ---")
        print(f"  reps: {aimd_agg['nreps']}")
        print(f"  miss rate: {aimd_agg.get('miss_rate_mean',0):.2f} ± {aimd_agg.get('miss_rate_std',0):.2f}%")
        print(f"  avg late:  {aimd_agg.get('avg_late_mean',0):.1f} ± {aimd_agg.get('avg_late_std',0):.1f}")
        print(f"  ai work:   {aimd_agg.get('work',{}).get('ai_mean',0):.0f} ± {aimd_agg.get('work',{}).get('ai_std',0):.0f}")
        if "alpha_mean" in aimd_agg:
            a = aimd_agg["alpha_mean"]
            print(f"  α steady:  mean={a[-60:].mean():.1f} std={a[-60:].std():.1f} (last 60 windows)")
            print(f"  α range:   [{a.min():.0f}, {a.max():.0f}]")

    if aimd_agg and fixed_agg:
        print("\n--- Comparison ---")
        aimd_miss = aimd_agg.get("miss_rate_mean", 0)
        fixed_miss = fixed_agg.get("miss_rate_mean", 0)
        aimd_work = aimd_agg.get("work", {}).get("ai_mean", 0)
        fixed_work = fixed_agg.get("work", {}).get("ai_mean", 0)
        print(f"  miss:  AIMD {aimd_miss:.2f}% vs fixed {fixed_miss:.2f}%  (Δ={aimd_miss-fixed_miss:+.2f}%)")
        print(f"  work:  AIMD {aimd_work:.0f} vs fixed {fixed_work:.0f}  (Δ={aimd_work-fixed_work:+.0f})")
        if aimd_miss < fixed_miss and aimd_work > fixed_work:
            print("  VERDICT: AIMD Pareto dominates fixed 50 ✅")
        elif aimd_miss < fixed_miss:
            print("  VERDICT: AIMD better miss, lower work")
        else:
            print("  VERDICT: AIMD did NOT beat fixed 50")

    print("=" * 90)


# ------------------------------------------------------------------ plots
def plot_alpha_trajectory(aimd_agg, prefix="./logs/sched/aimd/exp3"):
    if "alpha_mean" not in aimd_agg:
        return
    fig, ax = plt.subplots(figsize=(12, 4.5))
    a = aimd_agg["alpha_mean"]
    std = aimd_agg.get("alpha_std", np.zeros_like(a))
    x = np.arange(len(a))
    ax.plot(x, a, "-", color=COLORS["aimd"], lw=1.2, label="α mean")
    ax.fill_between(x, a - std, a + std, alpha=0.2, color=COLORS["aimd"])
    ax.axhline(40, color="gray", ls="--", lw=1, label="knee α*=40")
    ax.set_xlabel("Window")
    ax.set_ylabel("α")
    ax.set_title("Exp 3: AIMD α Trajectory (mean ± std, n=5)")
    ax.legend()
    ax.set_ylim(-2, 105)
    fig.tight_layout()
    fig.savefig(f"{prefix}_alpha_traj.png")
    print(f"[saved] {prefix}_alpha_traj.png")
    plt.close(fig)


def plot_miss_trajectory(runs, prefix="./logs/sched/aimd/exp3"):
    fig, ax = plt.subplots(figsize=(12, 4.5))
    for mode, color in [("aimd", COLORS["aimd"]), ("fixed", COLORS["fixed"])]:
        reps = [r for r in runs if r["mode"] == mode and "win_miss" in r]
        if not reps:
            continue
        min_len = min(len(r["win_miss"]) for r in reps)
        aligned = np.array([r["win_miss"][:min_len] for r in reps])
        mean = aligned.mean(axis=0)
        std = aligned.std(axis=0)
        x = np.arange(len(mean))
        ax.plot(x, mean, "-", color=color, lw=1.2, label=f"{mode}")
        ax.fill_between(x, mean - std, mean + std, alpha=0.2, color=color)
    ax.set_xlabel("Window")
    ax.set_ylabel("ctrl Miss Rate (%)")
    ax.set_title("Exp 3: Per-window Miss Rate")
    ax.legend()
    ax.set_ylim(-2, 105)
    fig.tight_layout()
    fig.savefig(f"{prefix}_miss_traj.png")
    print(f"[saved] {prefix}_miss_traj.png")
    plt.close(fig)


def plot_comparison(aimd_agg, fixed_agg, prefix="./logs/sched/aimd/exp3"):
    if not aimd_agg or not fixed_agg:
        return
    fig, axes = plt.subplots(1, 2, figsize=(10, 4))

    # miss rate bar
    ax = axes[0]
    modes = ["fixed", "aimd"]
    vals = [fixed_agg.get("miss_rate_mean", 0), aimd_agg.get("miss_rate_mean", 0)]
    errs = [fixed_agg.get("miss_rate_std", 0), aimd_agg.get("miss_rate_std", 0)]
    bars = ax.bar(modes, vals, yerr=errs, capsize=6,
                  color=[COLORS["fixed"], COLORS["aimd"]],
                  edgecolor="black", linewidth=0.8, width=0.5)
    for bar, v in zip(bars, vals):
        ax.text(bar.get_x() + bar.get_width() / 2, v + 1.5,
                f"{v:.1f}%", ha="center", fontsize=12, fontweight="bold")
    ax.set_ylabel("ctrl Miss Rate (%)")
    ax.set_title("Miss Rate Comparison")
    ax.set_ylim(0, max(vals) * 1.2 + 5)

    # ai work bar
    ax = axes[1]
    vals = [fixed_agg.get("work", {}).get("ai_mean", 0),
            aimd_agg.get("work", {}).get("ai_mean", 0)]
    errs = [fixed_agg.get("work", {}).get("ai_std", 0),
            aimd_agg.get("work", {}).get("ai_std", 0)]
    bars = ax.bar(modes, vals, yerr=errs, capsize=6,
                  color=[COLORS["fixed"], COLORS["aimd"]],
                  edgecolor="black", linewidth=0.8, width=0.5)
    for bar, v in zip(bars, vals):
        ax.text(bar.get_x() + bar.get_width() / 2, v + max(vals) * 0.02,
                f"{v:.0f}", ha="center", fontsize=12, fontweight="bold")
    ax.set_ylabel("ai Throughput (ticks)")
    ax.set_title("Throughput Comparison")

    fig.suptitle("Exp 3: AIMD vs Fixed 50", fontsize=13)
    fig.tight_layout()
    fig.savefig(f"{prefix}_comparison.png")
    print(f"[saved] {prefix}_comparison.png")
    plt.close(fig)


# ------------------------------------------------------------------ main
def main():
    if len(sys.argv) < 2:
        print(f"Usage: {sys.argv[0]} <sexp3_aimd.csv>")
        sys.exit(1)

    runs = parse(sys.argv[1])
    print(f"[parsed] {len(runs)} runs (warmup discarded)")

    computed = [compute(r) for r in runs]
    aimd_agg = aggregate(computed, "aimd")
    fixed_agg = aggregate(computed, "fixed")

    print()
    print_summary(aimd_agg, fixed_agg)

    if aimd_agg:
        plot_alpha_trajectory(aimd_agg)
    plot_miss_trajectory(computed)
    if aimd_agg and fixed_agg:
        plot_comparison(aimd_agg, fixed_agg)

    print("\nDone.")


if __name__ == "__main__":
    main()