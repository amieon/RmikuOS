#!/usr/bin/env python3
"""
stat_exp1.py -- Exp 1: alpha mechanism verification (step=1, 101 trials).
Auto-detects task names from CSV.

Usage:
    python3 stat_exp1.py ./logs/sched/mech/sexp1_mech.csv
"""

import sys, re
import numpy as np
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

plt.rcParams.update({
    "font.family": "DejaVu Sans", "font.size": 11,
    "figure.dpi": 150, "savefig.dpi": 200,
    "savefig.bbox": "tight", "axes.grid": True, "grid.alpha": 0.3,
})

PALETTE = ["#dc2626", "#2563eb", "#16a34a", "#d97706", "#7c3aed", "#0891b2"]


# ── Parse ──────────────────────────────────────────────────────
def parse(path):
    trials = []
    cur = None
    with open(path) as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            if line.startswith("#"):
                m = re.search(r'alpha=(\d+)', line)
                if m:
                    if cur:
                        trials.append(cur)
                    cur = {"alpha": int(m.group(1)), "W": [], "S": [], "K": []}
                continue
            if cur is None:
                continue
            p = line.split(",")
            tag = p[0]
            try:
                if tag == "W" and len(p) >= 8:
                    # W, win, 0, grp_idx, name, run_delta, eff_tickets, ready_threads
                    cur["W"].append({
                        "win":           int(p[1]),
                        "name":          p[4],
                        "run_delta":     int(p[5]),
                        "eff_tickets":   int(p[6]),
                        "ready_threads": int(p[7]),
                    })
                elif tag == "S" and len(p) >= 4:
                    # S, win, 0, jain_q, ...
                    cur["S"].append({"jain_q": int(p[3])})
                elif tag == "K" and len(p) >= 5:
                    cur["K"].append({"name": p[2], "work": int(p[4])})
            except (IndexError, ValueError):
                continue
    if cur:
        trials.append(cur)
    return trials


# ── Compute per-trial stats ───────────────────────────────────
def compute(tr, tasks):
    s = {"alpha": tr["alpha"]}
    # skip first 3 windows (warmup)
    ws = [w for w in tr["W"] if w["win"] > 3]
    if not ws:
        return s

    wins = sorted(set(w["win"] for w in ws))
    eff, share = {}, {}
    for t in tasks:
        tw = [w for w in ws if w["name"] == t]
        if not tw:
            continue
        eff[t] = np.mean([w["eff_tickets"] for w in tw])
        sh = []
        for win in wins:
            ww = [w for w in ws if w["win"] == win]
            tot = sum(w["run_delta"] for w in ww)
            mine = next((w["run_delta"] for w in ww if w["name"] == t), 0)
            sh.append(mine / tot * 100 if tot > 0 else 0)
        share[t] = np.mean(sh)

    s["eff"]   = eff
    s["share"] = share
    if tr["S"]:
        s["jain"] = np.mean([x["jain_q"] for x in tr["S"]]) / 1000.0
    for k in tr["K"]:
        s.setdefault("work", {})[k["name"]] = k["work"]
    return s


# ── Summary table ─────────────────────────────────────────────
def print_summary(stats, tasks):
    print("=" * 90)
    print("EXPERIMENT 1: alpha MECHANISM VERIFICATION  (step=1)")
    print(f"  tasks: {', '.join(tasks)}")
    print("=" * 90)

    hdr = f"{'α':>4}"
    for t in tasks:
        hdr += f"  {t+'_eff':>8}"
    for t in tasks:
        hdr += f"  {t+'%':>6}"
    hdr += f"  {'Jain':>6}"
    print(hdr)
    print("-" * 90)

    for s in stats:
        e  = s.get("eff", {})
        sh = s.get("share", {})
        row = f"{s['alpha']:>4}"
        for t in tasks:
            row += f"  {e.get(t, 0):>8.0f}"
        for t in tasks:
            row += f"  {sh.get(t, 0):>6.1f}"
        row += f"  {s.get('jain', 0):>6.3f}"
        print(row)
    print("=" * 90)


# ── Plots ─────────────────────────────────────────────────────
def plot_all(stats, tasks, prefix="exp1"):
    alphas = np.array([s["alpha"] for s in stats])
    colors = {t: PALETTE[i % len(PALETTE)] for i, t in enumerate(tasks)}

    # 1) eff_tickets
    fig, ax = plt.subplots(figsize=(9, 4.5))
    for t in tasks:
        ax.plot(alphas, [s.get("eff", {}).get(t, 0) for s in stats],
                "-", color=colors[t], lw=1.2, label=t)
    ax.set_xlabel("α"); ax.set_ylabel("Effective Tickets (mean)")
    ax.set_title("Exp 1: Effective Tickets vs α")
    ax.legend(); fig.tight_layout()
    fig.savefig(f"{prefix}_eff_tickets.png")
    print(f"[saved] {prefix}_eff_tickets.png"); plt.close(fig)

    # 2) CPU share
    fig, ax = plt.subplots(figsize=(9, 4.5))
    for t in tasks:
        ax.plot(alphas, [s.get("share", {}).get(t, 0) for s in stats],
                "-", color=colors[t], lw=1.2, label=t)
    ax.axhline(100.0 / len(tasks), color="gray", ls="--", lw=0.8,
               label=f"fair {100.0/len(tasks):.1f}%")
    ax.set_xlabel("α"); ax.set_ylabel("CPU Share (%)")
    ax.set_title("Exp 1: CPU Share vs α")
    ax.legend(); ax.set_ylim(-2, 105); fig.tight_layout()
    fig.savefig(f"{prefix}_cpu_share.png")
    print(f"[saved] {prefix}_cpu_share.png"); plt.close(fig)

    # 3) Jain
    fig, ax = plt.subplots(figsize=(9, 4.5))
    ax.plot(alphas, [s.get("jain", 0) for s in stats],
            "-", color="#7c3aed", lw=1.2)
    ax.axhline(1.0, color="gray", ls="--", lw=0.8)
    ax.set_xlabel("α"); ax.set_ylabel("Jain Fairness Index")
    ax.set_title("Exp 1: Jain vs α")
    ax.set_ylim(0, 1.05); fig.tight_layout()
    fig.savefig(f"{prefix}_jain.png")
    print(f"[saved] {prefix}_jain.png"); plt.close(fig)

    # 4) throughput (if K lines exist)
    has_work = any(s.get("work") for s in stats)
    if has_work:
        fig, ax = plt.subplots(figsize=(9, 4.5))
        for t in tasks:
            ax.plot(alphas, [s.get("work", {}).get(t, 0) for s in stats],
                    "-", color=colors[t], lw=1.2, label=t)
        ax.set_xlabel("α"); ax.set_ylabel("Spin Work (iterations)")
        ax.set_title("Exp 1: Throughput vs α")
        ax.legend(); fig.tight_layout()
        fig.savefig(f"{prefix}_throughput.png")
        print(f"[saved] {prefix}_throughput.png"); plt.close(fig)


# ── Main ──────────────────────────────────────────────────────
def main():
    if len(sys.argv) < 2:
        print(f"Usage: {sys.argv[0]} <sexp1_mech.csv>")
        sys.exit(1)

    trials = parse(sys.argv[1])
    print(f"[parsed] {len(trials)} trials")

    # auto-detect task names from first trial's W lines
    tasks = []
    for tr in trials:
        for w in tr["W"]:
            if w["name"] not in tasks:
                tasks.append(w["name"])
        if tasks:
            break
    print(f"[tasks]  {tasks}")

    stats = [compute(t, tasks) for t in trials]
    stats = [s for s in stats if s.get("eff")]
    if not stats:
        print("No valid data.")
        sys.exit(1)

    print()
    print_summary(stats, tasks)
    plot_all(stats, tasks)
    print("\nDone.")


if __name__ == "__main__":
    main()