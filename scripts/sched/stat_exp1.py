#!/usr/bin/env python3
"""
stat_exp1.py -- Summarize & plot Experiment 1 (α mechanism verification).

Parses multi-trial output from sexp1_mech.c:
  - Each trial = one α value, marked by "# TRIAL alpha=N"
  - Skips first 3 windows per trial (warmup)
  - Computes eff_tickets, CPU share, Jain per trial

Usage:
    python3 ./scripts/sched/stat_exp1.py ./logs/sched/mech/sexp1_mech.csv

Output:
    - Text summary to stdout
    - exp1_eff_tickets.png  (eff_tickets vs α, with theory curves)
    - exp1_cpu_share.png    (CPU share vs α)
    - exp1_jain.png         (Jain fairness vs α)
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

COLORS = {"t1": "#dc2626", "t2": "#2563eb", "t3": "#16a34a"}
THREADS = {"t1": 1, "t2": 9, "t3": 25}


def parse_schedlab(path):
    """Parse multi-trial output. Returns list of trials, each with alpha + W/S/K rows."""
    trials = []
    cur = None

    with open(path) as f:
        for line in f:
            line = line.strip()
            if not line:
                continue

            # Trial marker
            m = re.match(r"# TRIAL alpha=(\d+)", line)
            if m:
                cur = {"alpha": int(m.group(1)), "W": [], "S": [], "K": []}
                trials.append(cur)
                continue

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
                elif tag == "S":
                    cur["S"].append({
                        "win": int(parts[1]),
                        "alpha": int(parts[2]),
                        "jain_q": int(parts[3]),
                        "max_slowdown_q": int(parts[4]),
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

    return trials


def compute_trial_stats(trial):
    """Compute stats for one trial. Skip first 3 windows (warmup)."""
    alpha = trial["alpha"]
    W = trial["W"]
    S = trial["S"]

    if not W:
        return None

    # Skip first 3 windows
    # wins 跳过前 3（warmup）和后 2（退出噪声）
    wins = sorted(set(w["win"] for w in W))
    if len(wins) <= 5:
        wins = [w for w in wins if w > 1]  # window 太少时只跳第 1 个
    else:
        wins = [w for w in wins if w > 3 and w < max(wins) - 1]

    # eff_tickets: take last window's value per task
    eff = {}
    for w in W:
        if w["win"] in wins:
            name = w["name"]
            if name not in eff or w["eff_tickets"] > eff[name]:
                eff[name] = w["eff_tickets"]

    # CPU share: mean over warmup-skipped windows
    tasks = sorted(set(w["name"] for w in W))
    share = {t: [] for t in tasks}
    for win in wins:
        ww = [x for x in W if x["win"] == win]
        total_run = sum(x["run_delta"] for x in ww)
        for x in ww:
            s = x["run_delta"] / total_run * 100 if total_run > 0 else 0
            share[x["name"]].append(s)
    share_mean = {t: np.mean(v) if v else 0 for t, v in share.items()}

    # Jain: mean over warmup-skipped windows
    jains = [s["jain_q"] / 1000.0 for s in S if s["win"] in wins]
    jain_mean = np.mean(jains) if jains else 0

    # Work (spin throughput)
    work = {}
    for k in trial["K"]:
        work[k["name"]] = k["work"]

    return {
        "alpha": alpha,
        "eff": eff,
        "share": share_mean,
        "jain": jain_mean,
        "work": work,
    }


def print_summary(stats_list):
    print("=" * 72)
    print("EXPERIMENT 1: α MECHANISM VERIFICATION")
    print("=" * 72)
    print(f"\nTrials: {len(stats_list)} (α=0..100)")
    print(f"Warmup: first 3 windows skipped per trial")
    print(f"Tasks: t1(1thread) t2(9threads) t3(25threads), all tickets=100\n")

    # Table header
    print(f"{'α':>4}  {'eff_t1':>7} {'eff_t2':>7} {'eff_t3':>7}  "
          f"{'sh_t1':>6} {'sh_t2':>6} {'sh_t3':>6}  {'Jain':>6}")
    print("-" * 72)

    for s in stats_list:
        a = s["alpha"]
        e1 = s["eff"].get("t1", 0)
        e2 = s["eff"].get("t2", 0)
        e3 = s["eff"].get("t3", 0)
        sh1 = s["share"].get("t1", 0)
        sh2 = s["share"].get("t2", 0)
        sh3 = s["share"].get("t3", 0)
        j = s["jain"]
        print(f"{a:>4}  {e1:>7} {e2:>7} {e3:>7}  "
              f"{sh1:>6.1f} {sh2:>6.1f} {sh3:>6.1f}  {j:>6.4f}")

    # Checks
    print("\n" + "=" * 72)
    print("CHECKS:")
    print("=" * 72)

    # 1. Monotonicity
    eff_t2 = [s["eff"].get("t2", 0) for s in stats_list]
    eff_t3 = [s["eff"].get("t3", 0) for s in stats_list]
    mono_t2 = all(eff_t2[i] <= eff_t2[i+1] for i in range(len(eff_t2)-1))
    mono_t3 = all(eff_t3[i] <= eff_t3[i+1] for i in range(len(eff_t3)-1))
    print(f"  Monotonicity t2: {'✅ PASS' if mono_t2 else '❌ FAIL'}")
    print(f"  Monotonicity t3: {'✅ PASS' if mono_t3 else '❌ FAIL'}")

    # 2. α=0: all equal
    s0 = stats_list[0] if stats_list else None
    if s0 and s0["alpha"] == 0:
        sh0 = [s0["share"].get(t, 0) for t in ["t1", "t2", "t3"]]
        fair = all(abs(v - 33.3) < 8 for v in sh0)
        print(f"  α=0 fairness: {sh0[0]:.1f}:{sh0[1]:.1f}:{sh0[2]:.1f} "
              f"{'✅ PASS (≈1:1:1)' if fair else '⚠️ CHECK'}")

    # 3. α=100: proportional to threads
    s100 = stats_list[-1] if stats_list else None
    if s100 and s100["alpha"] == 100:
        sh100 = [s100["share"].get(t, 0) for t in ["t1", "t2", "t3"]]
        ratio_21 = sh100[1] / sh100[0] if sh100[0] > 0 else 0
        ratio_31 = sh100[2] / sh100[0] if sh100[0] > 0 else 0
        ok = 7 < ratio_21 < 11 and 22 < ratio_31 < 28
        print(f"  α=100 ratio: t2/t1={ratio_21:.1f} (expect ~9), "
              f"t3/t1={ratio_31:.1f} (expect ~25) "
              f"{'✅ PASS' if ok else '⚠️ CHECK'}")

    # 4. No starvation
    min_share = min(s["share"].get(t, 0) for s in stats_list for t in ["t1", "t2", "t3"])
    print(f"  Min share across all: {min_share:.1f}% "
          f"{'✅ PASS (>1%)' if min_share > 1 else '⚠️ CHECK (starvation?)'}")

    print("=" * 72)


def plot_eff_tickets(stats_list, out="./logs/sched/mech/exp1_eff_tickets.png"):
    fig, ax = plt.subplots(figsize=(10, 5))

    alphas = [s["alpha"] for s in stats_list]

    # Theory curves: eff = 100 × n^(α/100)
    a_arr = np.array(alphas)
    for name, color, n in [("t1", COLORS["t1"], 1), ("t2", COLORS["t2"], 9), ("t3", COLORS["t3"], 25)]:
        # Measured
        eff = [s["eff"].get(name, 0) for s in stats_list]
        ax.plot(alphas, eff, "o", color=color, ms=3, label=f"{name} (measured)")
        # Theory
        theory = 100 * n ** (a_arr / 100.0)
        ax.plot(alphas, theory, "-", color=color, alpha=0.5, lw=1)

    ax.set_xlabel("α")
    ax.set_ylabel("Effective Tickets")
    ax.set_title("Exp 1: Effective Tickets vs α (dots=measured, lines=theory)")
    ax.legend()
    fig.tight_layout()
    fig.savefig(out)
    print(f"[saved] {out}")
    plt.close(fig)


def plot_cpu_share(stats_list, out="./logs/sched/mech/exp1_cpu_share.png"):
    fig, ax = plt.subplots(figsize=(10, 5))

    alphas = [s["alpha"] for s in stats_list]

    for name, color in [("t1", COLORS["t1"]), ("t2", COLORS["t2"]), ("t3", COLORS["t3"])]:
        share = [s["share"].get(name, 0) for s in stats_list]
        ax.plot(alphas, share, "o-", color=color, ms=3, lw=1, label=name)

    ax.set_xlabel("α")
    ax.set_ylabel("CPU Share (%)")
    ax.set_title("Exp 1: CPU Share vs α (t1=1thr, t2=9thr, t3=25thr)")
    ax.legend()
    ax.set_ylim(-2, 105)
    fig.tight_layout()
    fig.savefig(out)
    print(f"[saved] {out}")
    plt.close(fig)


def plot_jain(stats_list, out="./logs/sched/mech/exp1_jain.png"):
    fig, ax = plt.subplots(figsize=(10, 4))

    alphas = [s["alpha"] for s in stats_list]
    jains = [s["jain"] for s in stats_list]

    ax.plot(alphas, jains, "o-", color="#7c3aed", ms=3, lw=1)
    ax.set_xlabel("α")
    ax.set_ylabel("Jain Fairness Index")
    ax.set_title("Exp 1: Jain Fairness vs α")
    ax.set_ylim(0, 1.05)
    ax.axhline(y=0.75, color="gray", ls="--", alpha=0.5, label="0.75 threshold")
    ax.legend()
    fig.tight_layout()
    fig.savefig(out)
    print(f"[saved] {out}")
    plt.close(fig)


def main():
    if len(sys.argv) < 2:
        print(f"Usage: {sys.argv[0]} <sexp1_mech.csv>")
        sys.exit(1)

    path = sys.argv[1]
    trials = parse_schedlab(path)
    print(f"[parsed] {path}: {len(trials)} trials")

    if not trials:
        print("No trials found. Exiting.")
        sys.exit(1)

    stats_list = [compute_trial_stats(t) for t in trials]
    stats_list = [s for s in stats_list if s is not None]

    print(f"[computed] {len(stats_list)} valid trials\n")
    print_summary(stats_list)

    plot_eff_tickets(stats_list)
    plot_cpu_share(stats_list)
    plot_jain(stats_list)
    print("\nDone.")


if __name__ == "__main__":
    main()
