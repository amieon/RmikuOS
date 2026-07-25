#!/usr/bin/env python3
"""
stat_exp2.py -- parse sexp2_edge output, draw trade-off curve.

usage (on HOST, not the target OS):
    ./run.sh sexp2_edge 2>&1 | tee logs/sched/edge.csv
    python3 scripts/sched/stat_exp2.py logs/sched/edge/edge.csv
    python3 scripts/sched/stat_exp2.py logs/sched/edge/edge.csv --detail 50

output:
    exp2_tradeoff.png      miss rate + ai throughput vs alpha
    exp2_detail_a<N>.png   window-level detail for one alpha
"""

import sys
import os
import re
from collections import defaultdict

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np


# ---- parse -----------------------------------------------------------

def parse(path):
    """
    returns (E, K, S, D, W):
      E: list of (alpha, jobs, miss, late_sum, late_max,
                  resp_sum, resp_sumsq, resp_min, resp_max)
      K: {alpha: {name: work}}
      S: {alpha: [(win, jain_q, max_slowdown_q)]}
      D: {alpha: [(win, jobs_d, miss_d, late_d)]}
      W: {alpha: [(win, pid, name, run_delta, eff_tickets, ready_threads)]}
    """
    E = []
    K = defaultdict(dict)
    S = defaultdict(list)
    D = defaultdict(list)
    W = defaultdict(list)
    cur_alpha = None

    with open(path) as f:
        for raw in f:
            line = raw.strip()
            if not line:
                continue

            m = re.match(r"^# --- alpha=(\d+) ---", line)
            if m:
                cur_alpha = int(m.group(1))
                continue
            if line.startswith("#"):
                continue

            p = line.split(",")
            tag = p[0]
            try:
                if tag == "E" and cur_alpha is not None:
                    E.append(tuple(int(x) for x in p[1:10]))
                elif tag == "K" and cur_alpha is not None:
                    K[cur_alpha][p[2]] = int(p[4])
                elif tag == "S" and cur_alpha is not None:
                    S[cur_alpha].append((int(p[1]), int(p[3]), int(p[4])))
                elif tag == "D" and cur_alpha is not None:
                    D[cur_alpha].append((int(p[1]), int(p[3]), int(p[4]), int(p[5])))
                elif tag == "W" and cur_alpha is not None:
                    W[cur_alpha].append((int(p[1]), int(p[3]), p[4],
                                         int(p[5]), int(p[6]), int(p[7])))
            except (ValueError, IndexError):
                continue
    return E, K, S, D, W


# ---- main plot: trade-off -------------------------------------------

def plot_tradeoff(E, K, out="logs/sched/edge/exp2_tradeoff.png"):
    alphas = np.array([e[0] for e in E])
    jobs = np.array([e[1] for e in E], dtype=float)
    miss = np.array([e[2] for e in E], dtype=float)
    miss_rate = np.where(jobs > 0, miss / jobs * 100.0, 0.0)

    ai_works = np.array([K.get(a, {}).get("ai", 0) for a in alphas], dtype=float)
    base = ai_works[0] if ai_works[0] > 0 else 1.0
    ai_norm = ai_works / base

    fig, ax1 = plt.subplots(figsize=(9, 5.5))
    c1 = "#d62728"
    ax1.plot(alphas, miss_rate, "o-", color=c1, lw=2, ms=7, label="ctrl miss rate (%)")
    ax1.set_xlabel("alpha (fixed)", fontsize=12)
    ax1.set_ylabel("ctrl miss rate (%)", color=c1, fontsize=12)
    ax1.tick_params(axis="y", labelcolor=c1)
    ax1.set_ylim(bottom=0)
    ax1.set_xlim(-2, 105)
    ax1.grid(True, alpha=0.3)

    ax2 = ax1.twinx()
    c2 = "#1f77b4"
    ax2.plot(alphas, ai_norm, "s--", color=c2, lw=2, ms=7, label="ai throughput (norm)")
    ax2.set_ylabel("ai throughput (norm to alpha=1)", color=c2, fontsize=12)
    ax2.tick_params(axis="y", labelcolor=c2)
    ax2.set_ylim(bottom=0)

    h1, l1 = ax1.get_legend_handles_labels()
    h2, l2 = ax2.get_legend_handles_labels()
    ax1.legend(h1 + h2, l1 + l2, loc="center left", fontsize=11)
    ax1.set_title("Exp2: fixed-alpha trade-off (ctrl deadline vs ai throughput)", fontsize=13)
    fig.tight_layout()
    fig.savefig(out, dpi=150)
    print("[saved]", out)
    plt.close(fig)


# ---- window-level detail --------------------------------------------

def plot_detail(alpha, S, D, W, out=None):
    if out is None:
        out = "logs/sched/edge/exp2_detail_a%d.png" % alpha
    d_rows = D.get(alpha, [])
    s_rows = S.get(alpha, [])
    w_rows = W.get(alpha, [])
    if not d_rows:
        print("[warn] no D rows for alpha=%d, skip" % alpha)
        return

    wins_d = [r[0] for r in d_rows]
    miss_d = [r[2] for r in d_rows]
    wins_s = [r[0] for r in s_rows]
    jain = [r[1] / 1000.0 for r in s_rows]

    ai_run = defaultdict(int)
    for (w, pid, name, rd, et, rt) in w_rows:
        if name == "ai":
            ai_run[w] += rd
    wins_w = sorted(ai_run.keys())
    ai_rd = [ai_run[w] for w in wins_w]

    fig, axes = plt.subplots(3, 1, figsize=(11, 9))
    axes[0].bar(wins_d, miss_d, color="#d62728", alpha=0.7, width=1.0)
    axes[0].set_ylabel("ctrl miss / window")
    axes[0].set_title("Exp2 detail: alpha=%d" % alpha)
    axes[0].grid(True, alpha=0.3)
    axes[1].plot(wins_w, ai_rd, "-", color="#1f77b4", lw=1.5)
    axes[1].set_ylabel("ai run_delta (ticks)")
    axes[1].grid(True, alpha=0.3)
    axes[2].plot(wins_s, jain, "-", color="#2ca02c", lw=1.5)
    axes[2].set_ylabel("Jain index")
    axes[2].set_xlabel("window")
    axes[2].set_ylim(0, 1.05)
    axes[2].grid(True, alpha=0.3)
    fig.tight_layout()
    fig.savefig(out, dpi=150)
    print("[saved]", out)
    plt.close(fig)


# ---- summary table ---------------------------------------------------

def print_table(E, K):
    print()
    print("%6s %7s %7s %7s %9s %9s %8s %9s" % (
        "alpha", "jobs", "miss", "miss%", "late_max", "ai_work", "ai_norm", "resp_avg"))
    print("-" * 72)
    base_ai = K.get(E[0][0], {}).get("ai", 1) or 1
    for e in E:
        alpha, jobs, miss, ls, lm, rs, rss, rmin, rmax = e
        mr = miss / jobs * 100.0 if jobs > 0 else 0.0
        ai_w = K.get(alpha, {}).get("ai", 0)
        ai_n = ai_w / base_ai if base_ai > 0 else 0.0
        ravg = rs / jobs if jobs > 0 else 0.0
        print("%6d %7d %7d %6.1f%% %9d %9d %7.2fx %9.1f" % (
            alpha, jobs, miss, mr, lm, ai_w, ai_n, ravg))
    print()


# ---- main ------------------------------------------------------------

def main():
    if len(sys.argv) < 2:
        print("usage: python3 stat_exp2.py <edge.csv> [--detail <alpha>]")
        sys.exit(1)
    path = sys.argv[1]
    if not os.path.isfile(path):
        print("error: %s not found" % path)
        sys.exit(1)

    detail_alpha = None
    if "--detail" in sys.argv:
        i = sys.argv.index("--detail")
        if i + 1 < len(sys.argv):
            detail_alpha = int(sys.argv[i + 1])

    E, K, S, D, W = parse(path)
    if not E:
        print("error: no E rows found")
        sys.exit(1)
    print("parsed %d alpha points from %s" % (len(E), path))

    print_table(E, K)
    plot_tradeoff(E, K)
    if detail_alpha is not None:
        plot_detail(detail_alpha, S, D, W)
    elif 50 in D:
        plot_detail(50, S, D, W)


if __name__ == "__main__":
    main()