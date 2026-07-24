#!/usr/bin/env python3
"""
stat_exp0.py -- Summarize & plot Experiment 0 (Baseline EDF) output.

Handles schedlab multi-prefix CSV:
  W,win,alpha,pid,name,run_delta,eff_tickets,ready_threads
  D,win,alpha,jobs_delta,miss_delta,late_delta
  S,win,next_alpha,jain_q,max_slowdown_q
  J,pid,name,threads,jobs,miss,late_sum,late_max,resp_sum,resp_sumsq,resp_min,resp_max
  K,pid,name,threads,work


Usage:
    python3 ./scripts/sched/stat_exp0.py ./logs/sched/edf/sexp0_edf.csv
    python3 ./scripts/sched/stat_exp0.py ./logs/sched/edf/sexp0_edf_2.csv ./logs/sched/edf/sexp0_edf_3.csv  # multi-rep

Output:
    - Compact text summary to stdout (paste this to me)
    - ./logs/sched/edf/exp0_miss_rate.png   (per-window miss rate over time)
    - ./logs/sched/edf/exp0_summary.png     (bar chart: overall miss rate per task)
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


# ── Parser ─────────────────────────────────────────────────────
def parse_schedlab(path):
    """Parse multi-prefix schedlab output into structured dicts."""
    meta = {}
    W_rows = []   # per-window per-task CPU stats
    D_rows = []   # per-window deadline stats (in-parent jobs)
    S_rows = []   # per-window summary
    J_rows = []   # final job summary
    K_rows = []   # final spin summary

    with open(path) as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            if line.startswith("#"):
                for kv in re.findall(r'(\w+)=([\w(),]+)', line):
                    meta[kv[0]] = kv[1]
                continue
            parts = line.split(",")
            tag = parts[0]
            try:
                if tag == "W":
                    # W,win,alpha,pid,name,run_delta,eff_tickets,ready_threads
                    W_rows.append({
                        "win": int(parts[1]),
                        "alpha": int(parts[2]),
                        "pid": int(parts[3]),
                        "name": parts[4],
                        "run_delta": int(parts[5]),
                        "eff_tickets": int(parts[6]),
                        "ready_threads": int(parts[7]),
                    })
                elif tag == "D":
                    # D,win,alpha,jobs_delta,miss_delta,late_delta
                    D_rows.append({
                        "win": int(parts[1]),
                        "alpha": int(parts[2]),
                        "jobs_delta": int(parts[3]),
                        "miss_delta": int(parts[4]),
                        "late_delta": int(parts[5]),
                    })
                elif tag == "S":
                    # S,win,next_alpha,jain_q,max_slowdown_q
                    S_rows.append({
                        "win": int(parts[1]),
                        "alpha": int(parts[2]),
                        "jain_q": int(parts[3]),
                        "max_slowdown_q": int(parts[4]),
                    })
                elif tag == "J":
                    # J,pid,name,threads,jobs,miss,late_sum,late_max,resp_sum,resp_sumsq,resp_min,resp_max
                    J_rows.append({
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
                    # K,pid,name,threads,work
                    K_rows.append({
                        "pid": int(parts[1]),
                        "name": parts[2],
                        "threads": int(parts[3]),
                        "work": int(parts[4]),
                    })
            except (IndexError, ValueError) as e:
                continue  # skip malformed lines

    return {
        "meta": meta,
        "W": W_rows,
        "D": D_rows,
        "S": S_rows,
        "J": J_rows,
        "K": K_rows,
    }


# ── Statistics ─────────────────────────────────────────────────
def compute_stats(data):
    stats = {}

    # --- ctrl: from J row (final) + D rows (per-window) ---
    for j in data["J"]:
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

    # --- ai, log: from K rows (spin throughput) ---
    for k in data["K"]:
        name = k["name"]
        stats[name] = {
            "type": "spin",
            "threads": k["threads"],
            "work": k["work"],
        }

    # --- per-window ctrl miss rate from D rows ---
    if data["D"]:
        wins = sorted(set(d["win"] for d in data["D"]))
        win_miss_rate = []
        for w in wins:
            wd = [d for d in data["D"] if d["win"] == w]
            jobs = sum(d["jobs_delta"] for d in wd)
            miss = sum(d["miss_delta"] for d in wd)
            rate = miss / jobs * 100 if jobs > 0 else 0
            win_miss_rate.append(rate)
        stats["_ctrl_win_miss_rate"] = np.array(win_miss_rate)

    # --- per-window CPU share from W rows ---
    if data["W"]:
        wins = sorted(set(w["win"] for w in data["W"]))
        tasks = sorted(set(w["name"] for w in data["W"]))
        share = {t: [] for t in tasks}
        for w in wins:
            ww = [x for x in data["W"] if x["win"] == w]
            total_run = sum(x["run_delta"] for x in ww)
            for x in ww:
                s = x["run_delta"] / total_run * 100 if total_run > 0 else 0
                share[x["name"]].append(s)
        stats["_cpu_share"] = {t: np.array(v) for t, v in share.items()}

    # --- fairness from S rows ---
    if data["S"]:
        jains = [s["jain_q"] / 1000.0 for s in data["S"]]
        stats["_jain"] = np.array(jains)

    return stats


# ── Print summary ──────────────────────────────────────────────
def print_summary(all_stats, all_meta, paths):
    print("=" * 62)
    print("EXPERIMENT 0: BASELINE EDF (alpha=1, no backoff)")
    print("=" * 62)

    for idx, (stats, meta, path) in enumerate(zip(all_stats, all_meta, paths)):
        print(f"\n--- Rep {idx} ({path}) ---")
        print(f"  config: {meta}")

        # Jobs tasks (ctrl)
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
                print(f"  [{name}] spin threads={s['threads']} "
                      f"work={s['work']}")

        # Per-window ctrl miss rate summary
        if "_ctrl_win_miss_rate" in stats:
            wr = stats["_ctrl_win_miss_rate"]
            print(f"  [ctrl per-window] mean={wr.mean():.2f}% "
                  f"std={wr.std():.2f} min={wr.min():.2f} max={wr.max():.2f}")

        # CPU share summary
        if "_cpu_share" in stats:
            print(f"  [CPU share %] ", end="")
            for t, arr in stats["_cpu_share"].items():
                print(f"{t}={arr.mean():.1f}±{arr.std():.1f}  ", end="")
            print()

        # Jain fairness
        if "_jain" in stats:
            j = stats["_jain"]
            print(f"  [Jain index] mean={j.mean():.4f} "
                  f"min={j.min():.4f}")

    # Cross-rep
    if len(all_stats) > 1:
        print(f"\n--- Aggregate ({len(all_stats)} reps) ---")
        for name in ["ctrl", "ai", "log"]:
            rates = []
            works = []
            for s in all_stats:
                if name in s:
                    if s[name]["type"] == "jobs":
                        rates.append(s[name]["miss_rate_pct"])
                    elif s[name]["type"] == "spin":
                        works.append(s[name]["work"])
            if rates:
                print(f"  {name}: miss_rate = {np.mean(rates):.2f} ± {np.std(rates):.2f}%")
            if works:
                print(f"  {name}: work = {np.mean(works):.0f} ± {np.std(works):.0f}")

    print("\n" + "=" * 62)


# ── Plots ──────────────────────────────────────────────────────
def plot_miss_rate_over_time(all_stats, out="./logs/sched/edf/exp0_miss_rate.png"):
    fig, ax = plt.subplots(figsize=(10, 4))
    for idx, stats in enumerate(all_stats[:3]):
        if "_ctrl_win_miss_rate" not in stats:
            continue
        wr = stats["_ctrl_win_miss_rate"]
        x = np.arange(len(wr))
        ls = "-" if idx == 0 else "--"
        ax.plot(x, wr, color=COLORS["ctrl"], ls=ls, lw=1.0,
                alpha=1.0 if idx == 0 else 0.4,
                label=f"ctrl" + (f" rep{idx}" if idx > 0 else ""))
    ax.set_xlabel("Window index (each = 100 ticks)")
    ax.set_ylabel("ctrl miss rate (%)")
    ax.set_title("Exp 0: ctrl Deadline Miss Rate (Baseline EDF, α=1)")
    ax.set_ylim(-2, 105)
    ax.legend()
    fig.tight_layout()
    fig.savefig(out)
    print(f"[saved] {out}")
    plt.close(fig)


def plot_cpu_share(all_stats, out="exp0_cpu_share.png"):
    fig, ax = plt.subplots(figsize=(10, 4))
    stats = all_stats[0]
    if "_cpu_share" not in stats:
        print("[skip] no W data for CPU share plot")
        return
    for name, arr in stats["_cpu_share"].items():
        color = COLORS.get(name, "#666666")
        ax.plot(np.arange(len(arr)), arr, color=color, lw=1.0, label=name)
    ax.set_xlabel("Window index")
    ax.set_ylabel("CPU share (%)")
    ax.set_title("Exp 0: Per-window CPU Share (Baseline EDF)")
    ax.legend()
    ax.set_ylim(-2, 105)
    fig.tight_layout()
    fig.savefig(out)
    print(f"[saved] {out}")
    plt.close(fig)


def plot_summary_bar(all_stats, out="./logs/sched/edf/exp0_summary.png"):
    fig, axes = plt.subplots(1, 2, figsize=(9, 4))

    # Left: miss rate (only jobs tasks)
    ax = axes[0]
    names, vals = [], []
    for name, s in all_stats[0].items():
        if name.startswith("_"):
            continue
        if s["type"] == "jobs":
            names.append(name)
            vals.append(s["miss_rate_pct"])
    if names:
        bars = ax.bar(names, vals, color=[COLORS.get(n, "#666") for n in names],
                      edgecolor="black", linewidth=0.8, width=0.5)
        for bar, v in zip(bars, vals):
            ax.text(bar.get_x() + bar.get_width()/2, v + 1, f"{v:.1f}%",
                    ha="center", fontsize=11, fontweight="bold")
    ax.set_ylabel("Deadline Miss Rate (%)")
    ax.set_title("ctrl Miss Rate")
    ax.set_ylim(0, max(max(vals) * 1.15, 10) if vals else 100)

    # Right: spin throughput
    ax = axes[1]
    names, vals = [], []
    for name, s in all_stats[0].items():
        if name.startswith("_"):
            continue
        if s["type"] == "spin":
            names.append(name)
            vals.append(s["work"])
    if names:
        bars = ax.bar(names, vals, color=[COLORS.get(n, "#666") for n in names],
                      edgecolor="black", linewidth=0.8, width=0.5)
        for bar, v in zip(bars, vals):
            ax.text(bar.get_x() + bar.get_width()/2, v + max(vals)*0.02,
                    f"{v}", ha="center", fontsize=10)
    ax.set_ylabel("Work iterations")
    ax.set_title("Spin Throughput")

    fig.suptitle("Exp 0: Baseline EDF Summary", fontsize=12, y=1.02)
    fig.tight_layout()
    fig.savefig(out)
    print(f"[saved] {out}")
    plt.close(fig)


# ── main ───────────────────────────────────────────────────────
def main():
    if len(sys.argv) < 2:
        print(f"Usage: {sys.argv[0]} <exp0.csv> [rep2.csv ...]")
        sys.exit(1)

    all_stats = []
    all_meta = []

    for path in sys.argv[1:]:
        data = parse_schedlab(path)
        nW = len(data["W"])
        nD = len(data["D"])
        nJ = len(data["J"])
        nK = len(data["K"])
        print(f"[parsed] {path}: W={nW} D={nD} S={len(data['S'])} "
              f"J={nJ} K={nK}")
        if nW == 0 and nJ == 0 and nK == 0:
            print(f"  [WARN] no usable data, skipping")
            continue
        stats = compute_stats(data)
        all_stats.append(stats)
        all_meta.append(data["meta"])

    if not all_stats:
        print("No valid data. Exiting.")
        sys.exit(1)

    print()
    print_summary(all_stats, all_meta, sys.argv[1:])
    plot_miss_rate_over_time(all_stats)
    plot_cpu_share(all_stats)
    plot_summary_bar(all_stats)
    print("\nDone. Paste the SUMMARY block above to me.")


if __name__ == "__main__":
    main()