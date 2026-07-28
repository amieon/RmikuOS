#!/usr/bin/env python3
"""
stat_exp0.py -- Summarize & plot Experiment 0 (Baseline EDF).

Supports multi-rep output from sexp0_edf.c:
  - Parses "# WARMUP" / "# RUN rep=N" markers to split reps
  - Discards warmup (rep=0)
  - Skips Window 1 (startup noise)
  - Aggregates: mean ± std across reps

Usage:
    python3 ./scripts/sched/stat_exp0.py ./logs/sched/edf/sexp0_edf.csv

Output:
    - Compact text summary to stdout
    - exp0_miss_rate.png   (per-window ctrl miss rate, reps overlaid)
    - exp0_cpu_share.png  (per-window CPU share)
    - exp0_summary.png    (bar chart: miss rate + share)
"""

import sys
import re
import numpy as np
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

plt.rcParams.update({
    "font.family": "DejaVu Sans",
    "font.size": 11,
    "figure.dpi": 150,
    "savefig.dpi": 200,
    "savefig.bbox": "tight",
    "axes.grid": True,
    "grid.alpha": 0.3,
})

COLORS = {"ctrl": "#dc2626", "ai": "#2563eb", "log": "#16a34a"}


def parse_schedlab(path):
    """Parse multi-rep schedlab output. Returns list of reps, each rep is a dict of W/D/S/J/K rows."""
    reps = []  # list of {W:[], D:[], S:[], J:[], K:[]}
    cur = None

    with open(path) as f:
        for line in f:
            line = line.strip()
            if not line:
                continue

            # Rep markers
            if line.startswith("# WARMUP"):
                cur = {"W": [], "D": [], "S": [], "J": [], "K": [], "warmup": True}
                reps.append(cur)
                continue
            if line.startswith("# RUN rep="):
                cur = {"W": [], "D": [], "S": [], "J": [], "K": [], "warmup": False}
                reps.append(cur)
                continue

            # Skip other comments
            if line.startswith("#"):
                continue
            if cur is None:
                continue

            parts = line.split(",")
            tag = parts[0]
            try:
                if tag == "W":
                    cur["W"].append({
                        "win": int(parts[1]),
                        "alpha": int(parts[2]),
                        "pid": int(parts[3]),
                        "name": parts[4],
                        "run_delta": int(parts[5]),
                        "eff_tickets": int(parts[6]),
                        "ready_threads": int(parts[7]),
                    })
                elif tag == "D":
                    cur["D"].append({
                        "win": int(parts[1]),
                        "alpha": int(parts[2]),
                        "jobs_delta": int(parts[3]),
                        "miss_delta": int(parts[4]),
                        "late_delta": int(parts[5]),
                    })
                elif tag == "S":
                    cur["S"].append({
                        "win": int(parts[1]),
                        "alpha": int(parts[2]),
                        "jain_q": int(parts[3]),
                        "max_slowdown_q": int(parts[4]),
                    })
                elif tag == "J":
                    cur["J"].append({
                        "pid": int(parts[1]),
                        "name": parts[2],
                        "threads": int(parts[3]),
                        "jobs": int(parts[4]),
                        "miss": int(parts[5]),
                        "late_sum": int(parts[6]),
                        "late_max": int(parts[7]),
                        "resp_sum": int(parts[8]),
                        "resp_sumsq": int(parts[9]),
                        "resp_min": int(parts[10]),
                        "resp_max": int(parts[11]),
                    })
                elif tag == "K":
                    cur["K"].append({
                        "pid": int(parts[1]),
                        "name": parts[2],
                        "threads": int(parts[3]),
                        "work": int(parts[4]),
                    })
            except (IndexError, ValueError):
                continue

    return reps


def compute_rep_stats(rep):
    """Compute stats for a single rep. Skips Window 1."""
    stats = {}

    # Jobs tasks (ctrl) from J row
    for j in rep["J"]:
        name = j["name"]
        miss_rate = j["miss"] / j["jobs"] * 100 if j["jobs"] > 0 else 0
        avg_resp = j["resp_sum"] / j["jobs"] if j["jobs"] > 0 else 0
        avg_late = j["late_sum"] / j["miss"] if j["miss"] > 0 else 0
        stats[name] = {
            "type": "jobs",
            "jobs": j["jobs"],
            "miss": j["miss"],
            "miss_rate_pct": miss_rate,
            "late_sum": j["late_sum"],
            "late_max": j["late_max"],
            "avg_late": avg_late,
            "resp_min": j["resp_min"],
            "resp_max": j["resp_max"],
            "avg_resp": avg_resp,
        }

    # Spin tasks (ai, log) from K rows
    for k in rep["K"]:
        stats[k["name"]] = {
            "type": "spin",
            "threads": k["threads"],
            "work": k["work"],
        }

    # Per-window ctrl miss rate (skip Window 1)
    if rep["D"]:
        wins = sorted(set(d["win"] for d in rep["D"]))
        wins = [w for w in wins if w > 1]  # skip Window 1
        win_miss_rate = []
        for w in wins:
            wd = [d for d in rep["D"] if d["win"] == w]
            jobs = sum(d["jobs_delta"] for d in wd)
            miss = sum(d["miss_delta"] for d in wd)
            rate = miss / jobs * 100 if jobs > 0 else 0
            win_miss_rate.append(rate)
        stats["_ctrl_win_miss_rate"] = np.array(win_miss_rate)

    # Per-window CPU share (skip Window 1)
    if rep["W"]:
        wins = sorted(set(w["win"] for w in rep["W"]))
        wins = [w for w in wins if w > 1]  # skip Window 1
        tasks = sorted(set(w["name"] for w in rep["W"]))
        share = {t: [] for t in tasks}
        for w in wins:
            ww = [x for x in rep["W"] if x["win"] == w]
            total_run = sum(x["run_delta"] for x in ww)
            for x in ww:
                s = x["run_delta"] / total_run * 100 if total_run > 0 else 0
                share[x["name"]].append(s)
        stats["_cpu_share"] = {t: np.array(v) for t, v in share.items()}

    # Jain fairness (skip Window 1)
    if rep["S"]:
        jains = [s["jain_q"] / 1000.0 for s in rep["S"] if s["win"] > 1]
        stats["_jain"] = np.array(jains)

    return stats


def print_summary(reps_stats, path):
    """Print text summary. Only non-warmup reps."""
    formal = [s for s in reps_stats if not s.get("warmup", False)]

    print("=" * 62)
    print("EXPERIMENT 0: BASELINE EDF (alpha=1, no backoff)")
    print("=" * 62)
    print(f"\nFile: {path}")
    print(f"Reps: {len(reps_stats)} total ({len(reps_stats) - len(formal)} warmup discarded, {len(formal)} formal)")
    print(f"Window 1: skipped (startup noise)")

    # Per-rep summary
    for idx, stats in enumerate(formal):
        rep_num = idx + 1
        print(f"\n--- Rep {rep_num} ---")
        for name, s in stats.items():
            if name.startswith("_"):
                continue
            if s["type"] == "jobs":
                print(f"  [{name}] jobs={s['jobs']} miss={s['miss']} "
                      f"miss_rate={s['miss_rate_pct']:.2f}%")
                print(f"         avg_late={s['avg_late']:.1f} "
                      f"late_max={s['late_max']} "
                      f"resp=[{s['resp_min']}, {s['resp_max']}] "
                      f"avg_resp={s['avg_resp']:.1f}")
            elif s["type"] == "spin":
                print(f"  [{name}] spin threads={s['threads']} work={s['work']}")

        if "_ctrl_win_miss_rate" in stats:
            wr = stats["_ctrl_win_miss_rate"]
            print(f"  [ctrl per-win] mean={wr.mean():.2f}% std={wr.std():.2f} "
                  f"min={wr.min():.2f} max={wr.max():.2f}")
        if "_cpu_share" in stats:
            parts = []
            for t in ["ctrl", "ai", "log"]:
                if t in stats["_cpu_share"]:
                    arr = stats["_cpu_share"][t]
                    parts.append(f"{t}={arr.mean():.1f}±{arr.std():.1f}")
            print(f"  [CPU share %] {'  '.join(parts)}")
        if "_jain" in stats:
            j = stats["_jain"]
            print(f"  [Jain] mean={j.mean():.4f} min={j.min():.4f}")

    # Aggregate across reps
    if len(formal) >= 2:
        print(f"\n--- Aggregate ({len(formal)} reps) ---")

        # ctrl miss rate
        ctrl_rates = [s["ctrl"]["miss_rate_pct"] for s in formal
                      if "ctrl" in s and s["ctrl"]["type"] == "jobs"]
        if ctrl_rates:
            print(f"  ctrl miss_rate = {np.mean(ctrl_rates):.2f} ± {np.std(ctrl_rates):.2f}%")

        # CPU share
        for name in ["ctrl", "ai", "log"]:
            shares = []
            for s in formal:
                if "_cpu_share" in s and name in s["_cpu_share"]:
                    shares.append(s["_cpu_share"][name].mean())
            if shares:
                print(f"  {name} CPU share = {np.mean(shares):.1f} ± {np.std(shares):.1f}%")

        # Jain
        jains = [s["_jain"].mean() for s in formal if "_jain" in s]
        if jains:
            print(f"  Jain index = {np.mean(jains):.4f} ± {np.std(jains):.4f}")

    print("\n" + "=" * 62)

    # Verdict
    if formal:
        ctrl_miss = np.mean([s["ctrl"]["miss_rate_pct"] for s in formal
                             if "ctrl" in s and s["ctrl"]["type"] == "jobs"])
        ctrl_share = np.mean([s["_cpu_share"]["ctrl"].mean() for s in formal
                              if "_cpu_share" in s and "ctrl" in s["_cpu_share"]])
        print("\nVERDICT:")
        print(f"  ctrl CPU share = {ctrl_share:.1f}% (expect ~66%)")
        print(f"  ctrl miss rate = {ctrl_miss:.2f}% (expect >95%)")
        if ctrl_share > 55 and ctrl_miss > 90:
            print("  => PASS: stride fair, but deadline destroyed by ai preemption")
            print("  => Adaptive alpha is necessary")
        else:
            print("  => CHECK: results deviate from expected, investigate")

    print("=" * 62)


def plot_miss_rate(formal, out="./logs/sched/edf/exp0_miss_rate.png"):
    fig, ax = plt.subplots(figsize=(10, 4))
    for idx, stats in enumerate(formal):
        if "_ctrl_win_miss_rate" not in stats:
            continue
        wr = stats["_ctrl_win_miss_rate"]
        x = np.arange(2, len(wr) + 2)  # start from Window 2
        ax.plot(x, wr, color=COLORS["ctrl"], lw=1.0, alpha=0.7,
                label=f"rep {idx+1}")
    ax.set_xlabel("Window (each = 100 ticks, Window 1 skipped)")
    ax.set_ylabel("ctrl miss rate (%)")
    ax.set_title("Exp 0: ctrl Deadline Miss Rate (Baseline EDF, α=1)")
    ax.set_ylim(-2, 105)
    ax.legend(fontsize=9)
    fig.tight_layout()
    fig.savefig(out)
    print(f"[saved] {out}")
    plt.close(fig)


def plot_cpu_share(formal, out="./logs/sched/edf/exp0_cpu_share.png"):
    fig, ax = plt.subplots(figsize=(10, 4))
    if not formal or "_cpu_share" not in formal[0]:
        return
    stats = formal[0]
    for name, arr in stats["_cpu_share"].items():
        color = COLORS.get(name, "#666666")
        x = np.arange(2, len(arr) + 2)
        ax.plot(x, arr, color=color, lw=1.0, label=name)
    ax.set_xlabel("Window (Window 1 skipped)")
    ax.set_ylabel("CPU share (%)")
    ax.set_title("Exp 0: Per-window CPU Share (Baseline EDF)")
    ax.legend()
    ax.set_ylim(-2, 105)
    fig.tight_layout()
    fig.savefig(out)
    print(f"[saved] {out}")
    plt.close(fig)


def plot_summary_bar(formal, out="./logs/sched/edf/exp0_summary.png"):
    fig, axes = plt.subplots(1, 2, figsize=(9, 4))

    # Left: ctrl miss rate
    ax = axes[0]
    rates = [s["ctrl"]["miss_rate_pct"] for s in formal
             if "ctrl" in s and s["ctrl"]["type"] == "jobs"]
    if rates:
        ax.bar(["ctrl"], [np.mean(rates)], yerr=[np.std(rates)],
               color=COLORS["ctrl"], edgecolor="black", width=0.4)
        ax.text(0, np.mean(rates) + 1, f"{np.mean(rates):.1f}%",
                ha="center", fontweight="bold")
    ax.set_ylabel("Miss Rate (%)")
    ax.set_title("ctrl Deadline Miss")
    ax.set_ylim(0, 105)

    # Right: CPU share
    ax = axes[1]
    names, means, stds = [], [], []
    for name in ["ctrl", "ai", "log"]:
        shares = [s["_cpu_share"][name].mean() for s in formal
                  if "_cpu_share" in s and name in s["_cpu_share"]]
        if shares:
            names.append(name)
            means.append(np.mean(shares))
            stds.append(np.std(shares))
    if names:
        bars = ax.bar(names, means, yerr=stds,
                      color=[COLORS.get(n, "#666") for n in names],
                      edgecolor="black", width=0.5)
        for bar, v in zip(bars, means):
            ax.text(bar.get_x() + bar.get_width()/2, v + 1, f"{v:.1f}%",
                    ha="center", fontsize=11, fontweight="bold")
    ax.set_ylabel("CPU Share (%)")
    ax.set_title("CPU Share (mean ± std)")
    ax.set_ylim(0, 80)

    fig.suptitle("Exp 0: Baseline EDF Summary", fontsize=12, y=1.02)
    fig.tight_layout()
    fig.savefig(out)
    print(f"[saved] {out}")
    plt.close(fig)


def main():
    if len(sys.argv) < 2:
        print(f"Usage: {sys.argv[0]} <sexp0_edf.csv>")
        sys.exit(1)

    path = sys.argv[1]
    reps = parse_schedlab(path)

    formal = [r for r in reps if not r.get("warmup", False)]
    print(f"[parsed] {path}: {len(reps)} reps ({len(reps) - len(formal)} warmup, {len(formal)} formal)")
    for idx, r in enumerate(reps):
        tag = "WARMUP" if r.get("warmup") else f"rep={idx}"
        print(f"  {tag}: W={len(r['W'])} D={len(r['D'])} S={len(r['S'])} "
              f"J={len(r['J'])} K={len(r['K'])}")

    if not formal:
        print("No formal reps (all warmup). Exiting.")
        sys.exit(1)

    reps_stats = [compute_rep_stats(r) for r in reps]
    formal_stats = [compute_rep_stats(r) for r in formal]

    print()
    print_summary(reps_stats, path)
    plot_miss_rate(formal_stats)
    plot_cpu_share(formal_stats)
    plot_summary_bar(formal_stats)
    print("\nDone.")


if __name__ == "__main__":
    main()
