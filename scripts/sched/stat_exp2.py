#!/usr/bin/env python3
"""
stat_exp2.py -- Exp 2: Edge Deadline Trade-off.
Parse sexp2_edge.c output: # WARMUP / # RUN alpha=N rep=M/3 delimiters.
Throughput = sum(W.run_delta) per task (no K-row dependency).

Usage:
    python3 stat_exp2.py ./logs/sched/edge/sexp2_edge.csv
    python3 stat_exp2.py ./logs/sched/edge/sexp2_edge.csv --detail 50
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

COLORS = {"ctrl": "#dc2626", "ai": "#2563eb", "log": "#16a34a"}


# ------------------------------------------------------------------ parse
def parse(path):
    """
    Parse sexp2_edge.c output.
    Delimiters: # WARMUP alpha=N (discard), # RUN alpha=N rep=M/3 (keep)
    """
    runs = []
    cur = None
    skip = False

    with open(path) as f:
        for line in f:
            line = line.strip()
            if not line:
                continue

            # --- warmup: discard previous cur, set skip ---
            if line.startswith("# WARMUP"):
                if cur is not None:
                    # discard warmup data
                    pass
                skip = True
                cur = None
                continue

            # --- formal run: save previous cur (if not skip), start new ---
            m = re.search(r'# RUN alpha=(\d+) rep=(\d+)/\d+', line)
            if m:
                if cur is not None and not skip:
                    runs.append(cur)
                skip = False
                cur = {
                    "alpha": int(m.group(1)),
                    "rep": int(m.group(2)),
                    "W": [], "J": [], "S": [], "D": []
                }
                continue

            # --- sl_run header/footer lines, ignore ---
            if line.startswith("#"):
                continue

            # --- data rows ---
            if cur is None:
                continue

            p = line.split(",")
            tag = p[0]
            try:
                if tag == "W" and len(p) >= 8:
                    # W,win,alpha,pid,name,run_delta,eff_tickets,ready_threads
                    cur["W"].append({
                        "win": int(p[1]), "name": p[4],
                        "run_delta": int(p[5]), "eff_tickets": int(p[6])
                    })
                elif tag == "J" and len(p) >= 12:
                    cur["J"].append({
                        "name": p[2], "jobs": int(p[4]), "miss": int(p[5]),
                        "late_sum": int(p[6]), "late_max": int(p[7]),
                        "resp_sum": int(p[8]), "resp_sumsq": int(p[9]),
                        "resp_min": int(p[10]), "resp_max": int(p[11])
                    })
                elif tag == "S" and len(p) >= 4:
                    cur["S"].append({
                        "win": int(p[1]), "jain_q": int(p[3]),
                        "max_slowdown_q": int(p[4])
                    })
                elif tag == "D" and len(p) >= 6:
                    cur["D"].append({
                        "win": int(p[1]), "jobs_delta": int(p[3]),
                        "miss_delta": int(p[4]), "late_delta": int(p[5])
                    })
                elif tag == "K" and len(p) >= 5:
                    cur.setdefault("K", []).append({
                        "name": p[2], "work": int(p[4])
                    })
            except (IndexError, ValueError):
                continue

    # append last run
    if cur is not None and not skip:
        runs.append(cur)

    return runs


# ------------------------------------------------------------------ compute
def compute(run):
    """Per-run statistics."""
    s = {"alpha": run["alpha"], "rep": run["rep"]}

    # --- ctrl deadline from J ---
    for j in run["J"]:
        if j["name"] == "ctrl":
            s["jobs"] = j["jobs"]
            s["miss"] = j["miss"]
            s["miss_rate"] = j["miss"] / j["jobs"] * 100.0 if j["jobs"] > 0 else 0.0
            s["avg_late"] = j["late_sum"] / j["miss"] if j["miss"] > 0 else 0.0
            s["max_late"] = j["late_max"]
            s["avg_resp"] = j["resp_sum"] / j["jobs"] if j["jobs"] > 0 else 0.0
            n = j["jobs"]
            if n > 0:
                mean = j["resp_sum"] / n
                var = j["resp_sumsq"] / n - mean * mean
                s["resp_std"] = np.sqrt(max(var, 0))
            else:
                s["resp_std"] = 0.0

    # --- throughput from W (sum run_delta per task, skip first 3 windows) ---
    ws = [w for w in run["W"] if w["win"] > 3]
    for name in ["ctrl", "ai", "log"]:
        total_rd = sum(w["run_delta"] for w in ws if w["name"] == name)
        s.setdefault("work", {})[name] = total_rd

    # --- per-window miss rate from D ---
    if run["D"]:
        rates = []
        for d in run["D"]:
            if d["jobs_delta"] > 0:
                rates.append(d["miss_delta"] / d["jobs_delta"] * 100.0)
            else:
                rates.append(0.0)
        s["win_miss_mean"] = np.mean(rates)
        s["win_miss_std"] = np.std(rates)

    # --- Jain from S ---
    if run["S"]:
        s["jain"] = np.mean([x["jain_q"] for x in run["S"]]) / 1000.0

    return s


def aggregate(runs):
    """Group by alpha, compute mean±std across reps."""
    from collections import defaultdict
    by_alpha = defaultdict(list)
    for r in runs:
        by_alpha[r["alpha"]].append(r)

    stats = []
    for alpha in sorted(by_alpha.keys()):
        reps = by_alpha[alpha]
        row = {"alpha": alpha, "nreps": len(reps)}

        for key in ["miss_rate", "avg_late", "max_late", "avg_resp", "resp_std",
                    "win_miss_mean", "win_miss_std", "jain"]:
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
    print("=" * 120)
    print("EXPERIMENT 2: EDGE DEADLINE TRADE-OFF  (11 alphas x 3 reps)")
    print("  ctrl: 1t,300tk,p=10,cpu=3,burn=400k  ai:25t,100tk  log:9t,50tk")
    print("  throughput = sum(W.run_delta) per task (skip first 3 windows)")
    print("=" * 120)
    hdr = (f"{'α':>4}  {'miss%':>7}  {'±':>5}  {'ai_rd':>9}  {'±':>7}  {'log_rd':>9}  {'±':>7}"
           f"  {'ctrl_rd':>9}  {'±':>7}  {'avg_late':>8}  {'max_late':>8}  {'Jain':>6}")
    print(hdr)
    print("-" * 120)

    for s in stats:
        w = s.get("work", {})
        print(f"{s['alpha']:>4}"
              f"  {s.get('miss_rate_mean',0):>7.2f}"
              f"  {s.get('miss_rate_std',0):>5.2f}"
              f"  {w.get('ai_mean',0):>9.0f}"
              f"  {w.get('ai_std',0):>7.0f}"
              f"  {w.get('log_mean',0):>9.0f}"
              f"  {w.get('log_std',0):>7.0f}"
              f"  {w.get('ctrl_mean',0):>9.0f}"
              f"  {w.get('ctrl_std',0):>7.0f}"
              f"  {s.get('avg_late_mean',0):>8.1f}"
              f"  {s.get('max_late_mean',0):>8.1f}"
              f"  {s.get('jain_mean',0):>6.3f}")
    print("=" * 120)


# ------------------------------------------------------------------ plots
def plot_all(stats, prefix="./logs/sched/edge/exp2"):
    alphas = np.array([s["alpha"] for s in stats])

    # 1) miss rate vs alpha
    fig, ax = plt.subplots(figsize=(9, 4.5))
    miss = [s.get("miss_rate_mean", 0) for s in stats]
    err = [s.get("miss_rate_std", 0) for s in stats]
    ax.errorbar(alphas, miss, yerr=err, fmt="o-", color=COLORS["ctrl"],
                capsize=4, lw=1.5, ms=6)
    ax.set_xlabel("α"); ax.set_ylabel("ctrl Miss Rate (%)")
    ax.set_title("Exp 2: Miss Rate vs α (mean ± std, n=3)")
    ax.set_ylim(-2, 105); fig.tight_layout()
    fig.savefig(f"{prefix}_miss_rate.png")
    print(f"[saved] {prefix}_miss_rate.png"); plt.close(fig)

    # 2) throughput (run_delta) vs alpha
    fig, ax = plt.subplots(figsize=(9, 4.5))
    for name, color in [("ai", COLORS["ai"]), ("log", COLORS["log"]), ("ctrl", COLORS["ctrl"])]:
        m = [s.get("work", {}).get(name + "_mean", 0) for s in stats]
        e = [s.get("work", {}).get(name + "_std", 0) for s in stats]
        ax.errorbar(alphas, m, yerr=e, fmt="o-", color=color,
                    capsize=4, lw=1.2, ms=5, label=name)
    ax.set_xlabel("α"); ax.set_ylabel("CPU Time (run_delta ticks)")
    ax.set_title("Exp 2: Throughput vs α")
    ax.legend(); fig.tight_layout()
    fig.savefig(f"{prefix}_throughput.png")
    print(f"[saved] {prefix}_throughput.png"); plt.close(fig)

    # 3) trade-off: dual-axis, alpha on x
    fig, ax1 = plt.subplots(figsize=(9, 5))
    
    # left: miss rate
    miss = [s.get("miss_rate_mean", 0) for s in stats]
    err = [s.get("miss_rate_std", 0) for s in stats]
    ax1.errorbar(alphas, miss, yerr=err, fmt="o-", color=COLORS["ctrl"],
                 capsize=4, lw=2, ms=7, label="ctrl miss rate (%)")
    ax1.set_xlabel("α", fontsize=12)
    ax1.set_ylabel("ctrl Miss Rate (%)", color=COLORS["ctrl"], fontsize=12)
    ax1.tick_params(axis="y", labelcolor=COLORS["ctrl"])
    ax1.set_ylim(-2, 105)
    ax1.set_xlim(-2, 105)
    ax1.grid(True, alpha=0.3)
    
    # knee point annotation
    ax1.axvline(40, color="gray", ls=":", lw=1.5, alpha=0.7)
    ax1.annotate("knee α*=40", xy=(40, 35), xytext=(55, 50),
                 fontsize=10, color="gray",
                 arrowprops=dict(arrowstyle="->", color="gray", lw=1))
    
    # right: ai throughput
    ax2 = ax1.twinx()
    ai_rd = [s.get("work", {}).get("ai_mean", 0) for s in stats]
    ai_err = [s.get("work", {}).get("ai_std", 0) for s in stats]
    ax2.errorbar(alphas, ai_rd, yerr=ai_err, fmt="s--", color=COLORS["ai"],
                 capsize=4, lw=2, ms=7, label="ai throughput (ticks)")
    ax2.set_ylabel("ai Throughput (run_delta ticks)", color=COLORS["ai"], fontsize=12)
    ax2.tick_params(axis="y", labelcolor=COLORS["ai"])
    
    # combined legend
    h1, l1 = ax1.get_legend_handles_labels()
    h2, l2 = ax2.get_legend_handles_labels()
    ax1.legend(h1 + h2, l1 + l2, loc="center left", fontsize=10)
    
    ax1.set_title("Exp 2: Trade-off — ctrl Miss Rate vs ai Throughput", fontsize=13)
    fig.tight_layout()
    fig.savefig(f"{prefix}_tradeoff.png", dpi=150)
    print(f"[saved] {prefix}_tradeoff.png")
    plt.close(fig)

    # 4) tardiness
    fig, ax = plt.subplots(figsize=(9, 4.5))
    avg_l = [s.get("avg_late_mean", 0) for s in stats]
    max_l = [s.get("max_late_mean", 0) for s in stats]
    ax.plot(alphas, avg_l, "o-", color="#ea580c", lw=1.2, label="avg late")
    ax.plot(alphas, max_l, "s--", color="#7c3aed", lw=1.2, label="max late")
    ax.set_xlabel("α"); ax.set_ylabel("Late Ticks")
    ax.set_title("Exp 2: ctrl Tardiness vs α")
    ax.legend(); fig.tight_layout()
    fig.savefig(f"{prefix}_tardiness.png")
    print(f"[saved] {prefix}_tardiness.png"); plt.close(fig)

    # 5) Jain
    fig, ax = plt.subplots(figsize=(9, 4.5))
    jain = [s.get("jain_mean", 0) for s in stats]
    ax.plot(alphas, jain, "o-", color="#0891b2", lw=1.2)
    ax.axhline(1.0, color="gray", ls="--", lw=0.8)
    ax.set_xlabel("α"); ax.set_ylabel("Jain Fairness Index")
    ax.set_title("Exp 2: Jain vs α")
    ax.set_ylim(0, 1.05); fig.tight_layout()
    fig.savefig(f"{prefix}_jain.png")
    print(f"[saved] {prefix}_jain.png"); plt.close(fig)


# ------------------------------------------------------------------ detail plot
def plot_detail(alpha, runs, out=None):
    """Plot window-level detail for one alpha across reps."""
    reps = [r for r in runs if r["alpha"] == alpha]
    if not reps:
        print(f"[warn] no reps for alpha={alpha}")
        return

    fig, axes = plt.subplots(3, 1, figsize=(11, 9), sharex=True)

    for rep_idx, run in enumerate(reps):
        ds = sorted(run.get("D", []), key=lambda x: x["win"])
        if ds:
            wins = [d["win"] for d in ds]
            rates = [d["miss_delta"] / d["jobs_delta"] * 100 if d["jobs_delta"] > 0 else 0
                     for d in ds]
            axes[0].plot(wins, rates, ".-", lw=0.8, ms=3, alpha=0.7, label=f"rep{rep_idx+1}")

        ws = sorted([w for w in run.get("W", []) if w["name"] == "ai"], key=lambda x: x["win"])
        if ws:
            wins = [w["win"] for w in ws]
            rds = [w["run_delta"] for w in ws]
            axes[1].plot(wins, rds, ".-", lw=0.8, ms=3, alpha=0.7, label=f"rep{rep_idx+1}")

        ss = sorted(run.get("S", []), key=lambda x: x["win"])
        if ss:
            wins = [s["win"] for s in ss]
            jains = [s["jain_q"] / 1000.0 for s in ss]
            axes[2].plot(wins, jains, ".-", lw=0.8, ms=3, alpha=0.7, label=f"rep{rep_idx+1}")

    axes[0].set_ylabel("ctrl miss rate (%)")
    axes[0].set_title(f"Exp 2 Detail: α = {alpha}")
    axes[0].legend(fontsize=8, ncol=3)
    axes[0].grid(True, alpha=0.3)

    axes[1].set_ylabel("ai run_delta")
    axes[1].grid(True, alpha=0.3)

    axes[2].set_ylabel("Jain index")
    axes[2].set_xlabel("Window")
    axes[2].set_ylim(0, 1.05)
    axes[2].grid(True, alpha=0.3)

    fig.tight_layout()
    if out is None:
        out = f"exp2_detail_a{alpha}.png"
    fig.savefig(out, dpi=150)
    print(f"[saved] {out}")
    plt.close(fig)


# ------------------------------------------------------------------ main
def main():
    if len(sys.argv) < 2:
        print(f"Usage: {sys.argv[0]} <sexp2_edge.csv> [--detail <alpha>]")
        sys.exit(1)

    path = sys.argv[1]
    detail_alpha = None
    if "--detail" in sys.argv:
        i = sys.argv.index("--detail")
        if i + 1 < len(sys.argv):
            detail_alpha = int(sys.argv[i + 1])

    runs = parse(path)
    print(f"[parsed] {len(runs)} runs (warmup discarded)")

    computed = [compute(r) for r in runs]
    stats = aggregate(computed)
    if not stats:
        print("No valid data."); sys.exit(1)

    print()
    print_summary(stats)
    plot_all(stats)

    if detail_alpha is not None:
        plot_detail(detail_alpha, runs)

    print("\nDone.")


if __name__ == "__main__":
    main()