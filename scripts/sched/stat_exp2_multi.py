#!/usr/bin/env python3
"""
stat_exp2.py -- Exp 2: Edge Deadline Trade-off (multi-config).

Parses sexp2_multi.c output:
  # RUN config=<name> alpha=<N> rep=<M>/<total>
  W,win,alpha,pid,name,run_delta,eff_tickets,ready_threads
  D,win,alpha,jobs_delta,miss_delta,late_delta
  J,pid,name,threads,jobs,miss,late_sum,late_max,...
  S,win,alpha,jain_q,max_slowdown_q

Groups by (config, alpha), aggregates across reps (mean ± std).
Outputs per-config tables + plots, plus cross-config comparison.

Usage:
    python3 stat_exp2.py ./logs/sched/edge/sexp2_multi.csv
    python3 stat_exp2.py ./logs/sched/edge/sexp2_multi.csv --detail medium 40
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
CONFIG_COLORS = ["#dc2626", "#2563eb", "#16a34a", "#ea580c", "#7c3aed"]


# ------------------------------------------------------------------ parse
def parse(path):
    """Parse multi-config output. Returns list of runs, each tagged with config."""
    runs = []
    cur = None
    in_warmup = False

    with open(path) as f:
        for line in f:
            line = line.strip()
            if not line:
                continue

            # warmup marker
            m_warm = re.search(r'# WARMUP config=(\S+) alpha=(\d+)', line)
            if m_warm:
                # save previous formal run before starting warmup
                if cur is not None and not in_warmup:
                    runs.append(cur)
                in_warmup = True
                cur = None
                continue

            # formal run marker
            m_run = re.search(r'# RUN config=(\S+) alpha=(\d+) rep=(\d+)/(\d+)', line)
            if m_run:
                # save previous formal run before starting new one
                if cur is not None and not in_warmup:
                    runs.append(cur)
                in_warmup = False
                cur = {
                    "config": m_run.group(1),
                    "alpha": int(m_run.group(2)),
                    "rep": int(m_run.group(3)),
                    "W": [], "J": [], "S": [], "D": [], "K": [],
                }
                continue

            # skip warmup data lines
            if in_warmup or cur is None:
                # but don't skip config header lines
                if line.startswith("#"):
                    continue
                if in_warmup:
                    continue
                if cur is None:
                    continue

            if line.startswith("#"):
                continue

            p = line.split(",")
            tag = p[0]
            try:
                if tag == "W" and len(p) >= 8:
                    cur["W"].append({
                        "win": int(p[1]),
                        "name": p[4],
                        "run_delta": int(p[5]),
                        "eff_tickets": int(p[6]),
                    })
                elif tag == "J" and len(p) >= 12:
                    cur["J"].append({
                        "name": p[2],
                        "jobs": int(p[4]),
                        "miss": int(p[5]),
                        "late_sum": int(p[6]),
                        "late_max": int(p[7]),
                        "resp_sum": int(p[8]),
                        "resp_sumsq": int(p[9]),
                        "resp_min": int(p[10]),
                        "resp_max": int(p[11]),
                    })
                elif tag == "S" and len(p) >= 5:
                    cur["S"].append({
                        "win": int(p[1]),
                        "jain_q": int(p[3]),
                        "max_slowdown_q": int(p[4]),
                    })
                elif tag == "D" and len(p) >= 6:
                    cur["D"].append({
                        "win": int(p[1]),
                        "jobs_delta": int(p[3]),
                        "miss_delta": int(p[4]),
                        "late_delta": int(p[5]),
                    })
                elif tag == "K" and len(p) >= 5:
                    cur["K"].append({
                        "name": p[2],
                        "work": int(p[4]),
                    })
            except (IndexError, ValueError):
                continue

    # append last run
    if cur is not None and not in_warmup:
        runs.append(cur)

    return runs


# ------------------------------------------------------------------ compute
def compute(run):
    """Per-run statistics."""
    s = {
        "config": run["config"],
        "alpha": run["alpha"],
        "rep": run["rep"],
    }

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
            break

    # --- throughput from W (sum run_delta per task, skip first 3 windows) ---
    ws = [w for w in run["W"] if w["win"] > 3]
    for name in ["ctrl", "ai", "log"]:
        total_rd = sum(w["run_delta"] for w in ws if w["name"] == name)
        s.setdefault("work", {})[name] = total_rd

    # --- CPU share from W ---
    total_all = sum(w["run_delta"] for w in ws)
    for name in ["ctrl", "ai", "log"]:
        total_name = sum(w["run_delta"] for w in ws if w["name"] == name)
        s.setdefault("share", {})[name] = total_name / total_all * 100 if total_all > 0 else 0

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
    else:
        s["win_miss_mean"] = 0
        s["win_miss_std"] = 0

    # --- Jain from S ---
    if run["S"]:
        s["jain"] = np.mean([x["jain_q"] for x in run["S"]]) / 1000.0
    else:
        s["jain"] = 0

    return s


def aggregate(runs):
    """Group by (config, alpha), compute mean ± std across reps."""
    from collections import defaultdict
    by_key = defaultdict(list)
    for r in runs:
        by_key[(r["config"], r["alpha"])].append(r)

    stats = []
    for (config, alpha) in sorted(by_key.keys()):
        reps = by_key[(config, alpha)]
        row = {"config": config, "alpha": alpha, "nreps": len(reps)}

        for key in ["miss_rate", "avg_late", "max_late", "avg_resp", "resp_std",
                     "win_miss_mean", "win_miss_std", "jain"]:
            vals = [r.get(key, 0) for r in reps]
            row[key + "_mean"] = np.mean(vals)
            row[key + "_std"] = np.std(vals)

        for name in ["ctrl", "ai", "log"]:
            vals_w = [r.get("work", {}).get(name, 0) for r in reps]
            row["work_" + name + "_mean"] = np.mean(vals_w)
            row["work_" + name + "_std"] = np.std(vals_w)

            vals_s = [r.get("share", {}).get(name, 0) for r in reps]
            row["share_" + name + "_mean"] = np.mean(vals_s)
            row["share_" + name + "_std"] = np.std(vals_s)

        stats.append(row)
    return stats


# ------------------------------------------------------------------ print
def print_summary(stats):
    configs = sorted(set(s["config"] for s in stats))

    for config in configs:
        cs = [s for s in stats if s["config"] == config]
        print("=" * 110)
        print(f"CONFIG: {config}  ({len(cs)} alphas × {cs[0]['nreps']} reps)")
        print("=" * 110)
        hdr = (f"{'α':>4}  {'miss%':>7} {'±':>5}  "
               f"{'sh_ctrl':>7} {'±':>5}  {'sh_ai':>7} {'±':>5}  {'sh_log':>7} {'±':>5}  "
               f"{'avg_late':>8}  {'max_late':>8}  {'Jain':>6}")
        print(hdr)
        print("-" * 110)

        for s in cs:
            print(f"{s['alpha']:>4}"
                  f"  {s['miss_rate_mean']:>7.1f} {s['miss_rate_std']:>5.1f}"
                  f"  {s['share_ctrl_mean']:>7.1f} {s['share_ctrl_std']:>5.1f}"
                  f"  {s['share_ai_mean']:>7.1f} {s['share_ai_std']:>5.1f}"
                  f"  {s['share_log_mean']:>7.1f} {s['share_log_std']:>5.1f}"
                  f"  {s['avg_late_mean']:>8.1f}"
                  f"  {s['max_late_mean']:>8.0f}"
                  f"  {s['jain_mean']:>6.3f}")
        print("=" * 110)
        print()


# ------------------------------------------------------------------ plots
def plot_per_config(stats, outdir="./logs/sched/edge"):
    """Per-config plots: miss_rate, share, tardiness, jain."""
    configs = sorted(set(s["config"] for s in stats))

    for ci, config in enumerate(configs):
        cs = sorted([s for s in stats if s["config"] == config],
                    key=lambda x: x["alpha"])
        alphas = np.array([s["alpha"] for s in cs])
        prefix = f"{outdir}/exp2_{config}"

        # 1) miss rate + share dual axis
        fig, ax1 = plt.subplots(figsize=(9, 5))
        miss = [s["miss_rate_mean"] for s in cs]
        miss_err = [s["miss_rate_std"] for s in cs]
        ax1.errorbar(alphas, miss, yerr=miss_err, fmt="o-",
                     color=COLORS["ctrl"], capsize=4, lw=2, ms=6,
                     label="ctrl miss rate (%)")
        ax1.set_xlabel("α")
        ax1.set_ylabel("ctrl Miss Rate (%)", color=COLORS["ctrl"])
        ax1.tick_params(axis="y", labelcolor=COLORS["ctrl"])
        ax1.set_ylim(-2, 105)
        ax1.set_xlim(-2, 105)

        ax2 = ax1.twinx()
        for name, color in [("ai", COLORS["ai"]), ("log", COLORS["log"])]:
            sh = [s[f"share_{name}_mean"] for s in cs]
            ax2.plot(alphas, sh, "s--", color=color, lw=1.2, ms=4, label=f"{name} share %")
        ax2.set_ylabel("CPU Share (%)")
        ax2.set_ylim(0, 80)

        h1, l1 = ax1.get_legend_handles_labels()
        h2, l2 = ax2.get_legend_handles_labels()
        ax1.legend(h1 + h2, l1 + l2, loc="center left", fontsize=9)
        ax1.set_title(f"Exp 2 [{config}]: Miss Rate & CPU Share vs α")
        fig.tight_layout()
        fig.savefig(f"{prefix}_miss_share.png")
        print(f"[saved] {prefix}_miss_share.png")
        plt.close(fig)

        # 2) tardiness
        fig, ax = plt.subplots(figsize=(9, 4))
        avg_l = [s["avg_late_mean"] for s in cs]
        max_l = [s["max_late_mean"] for s in cs]
        ax.plot(alphas, avg_l, "o-", color="#ea580c", lw=1.2, label="avg late")
        ax.plot(alphas, max_l, "s--", color="#7c3aed", lw=1.2, label="max late")
        ax.set_xlabel("α")
        ax.set_ylabel("Late Ticks")
        ax.set_title(f"Exp 2 [{config}]: ctrl Tardiness vs α")
        ax.legend()
        fig.tight_layout()
        fig.savefig(f"{prefix}_tardiness.png")
        print(f"[saved] {prefix}_tardiness.png")
        plt.close(fig)

        # 3) Jain
        fig, ax = plt.subplots(figsize=(9, 4))
        jain = [s["jain_mean"] for s in cs]
        ax.plot(alphas, jain, "o-", color="#0891b2", lw=1.2)
        ax.axhline(1.0, color="gray", ls="--", lw=0.8)
        ax.set_xlabel("α")
        ax.set_ylabel("Jain Fairness Index")
        ax.set_title(f"Exp 2 [{config}]: Jain vs α")
        ax.set_ylim(0, 1.05)
        fig.tight_layout()
        fig.savefig(f"{prefix}_jain.png")
        print(f"[saved] {prefix}_jain.png")
        plt.close(fig)


def plot_comparison(stats, outdir="./logs/sched/edge"):
    """Cross-config comparison: miss rate vs α, all configs on one plot."""
    configs = sorted(set(s["config"] for s in stats))
    fig, ax = plt.subplots(figsize=(10, 5.5))

    for ci, config in enumerate(configs):
        cs = sorted([s for s in stats if s["config"] == config],
                    key=lambda x: x["alpha"])
        alphas = [s["alpha"] for s in cs]
        miss = [s["miss_rate_mean"] for s in cs]
        miss_err = [s["miss_rate_std"] for s in cs]
        color = CONFIG_COLORS[ci % len(CONFIG_COLORS)]
        ax.errorbar(alphas, miss, yerr=miss_err, fmt="o-", color=color,
                    capsize=3, lw=1.5, ms=5, label=config)

    ax.set_xlabel("α", fontsize=12)
    ax.set_ylabel("ctrl Miss Rate (%)", fontsize=12)
    ax.set_title("Exp 2: Miss Rate vs α — All Configs", fontsize=13)
    ax.set_ylim(-2, 105)
    ax.set_xlim(-2, 105)
    ax.legend(fontsize=10)
    fig.tight_layout()
    fig.savefig(f"{outdir}/exp2_compare_miss.png")
    print(f"[saved] {outdir}/exp2_compare_miss.png")
    plt.close(fig)

    # Trade-off: miss rate vs ai share
    fig, ax = plt.subplots(figsize=(10, 5.5))
    for ci, config in enumerate(configs):
        cs = sorted([s for s in stats if s["config"] == config],
                    key=lambda x: x["alpha"])
        miss = [s["miss_rate_mean"] for s in cs]
        ai_sh = [s["share_ai_mean"] for s in cs]
        color = CONFIG_COLORS[ci % len(CONFIG_COLORS)]
        ax.plot(ai_sh, miss, "o-", color=color, lw=1.5, ms=5, label=config)
        # annotate alpha at each point
        for s in cs:
            ax.annotate(f"{s['alpha']}", 
                        (s["share_ai_mean"], s["miss_rate_mean"]),
                        fontsize=6, alpha=0.7, ha="center", va="bottom")

    ax.set_xlabel("ai CPU Share (%)", fontsize=12)
    ax.set_ylabel("ctrl Miss Rate (%)", fontsize=12)
    ax.set_title("Exp 2: Trade-off — ctrl Miss vs ai Share", fontsize=13)
    ax.set_ylim(-2, 105)
    ax.legend(fontsize=10)
    fig.tight_layout()
    fig.savefig(f"{outdir}/exp2_compare_tradeoff.png")
    print(f"[saved] {outdir}/exp2_compare_tradeoff.png")
    plt.close(fig)


# ------------------------------------------------------------------ detail
def plot_detail(runs, config, alpha, outdir="./logs/sched/edge"):
    """Window-level detail for one (config, alpha) across reps."""
    reps = [r for r in runs if r["config"] == config and r["alpha"] == alpha]
    if not reps:
        print(f"[warn] no reps for config={config} alpha={alpha}")
        return

    fig, axes = plt.subplots(3, 1, figsize=(11, 9), sharex=True)

    for ri, run in enumerate(reps):
        # per-window miss rate from D
        ds = sorted(run.get("D", []), key=lambda x: x["win"])
        if ds:
            wins = [d["win"] for d in ds]
            rates = [d["miss_delta"] / d["jobs_delta"] * 100
                     if d["jobs_delta"] > 0 else 0 for d in ds]
            axes[0].plot(wins, rates, ".-", lw=0.8, ms=3, alpha=0.7,
                         label=f"rep{ri+1}")

        # ai run_delta from W
        ws = sorted([w for w in run.get("W", []) if w["name"] == "ai"],
                    key=lambda x: x["win"])
        if ws:
            axes[1].plot([w["win"] for w in ws], [w["run_delta"] for w in ws],
                         ".-", lw=0.8, ms=3, alpha=0.7, label=f"rep{ri+1}")

        # Jain from S
        ss = sorted(run.get("S", []), key=lambda x: x["win"])
        if ss:
            axes[2].plot([s["win"] for s in ss],
                         [s["jain_q"] / 1000.0 for s in ss],
                         ".-", lw=0.8, ms=3, alpha=0.7, label=f"rep{ri+1}")

    axes[0].set_ylabel("ctrl miss rate (%)")
    axes[0].set_title(f"Exp 2 Detail: config={config} α={alpha}")
    axes[0].legend(fontsize=8, ncol=3)

    axes[1].set_ylabel("ai run_delta")

    axes[2].set_ylabel("Jain index")
    axes[2].set_xlabel("Window")
    axes[2].set_ylim(0, 1.05)

    for ax in axes:
        ax.grid(True, alpha=0.3)

    fig.tight_layout()
    out = f"{outdir}/exp2_detail_{config}_a{alpha}.png"
    fig.savefig(out, dpi=150)
    print(f"[saved] {out}")
    plt.close(fig)


# ------------------------------------------------------------------ main
def main():
    if len(sys.argv) < 2:
        print(f"Usage: {sys.argv[0]} <sexp2_multi.csv> [--detail <config> <alpha>]")
        sys.exit(1)

    path = sys.argv[1]
    detail_config = None
    detail_alpha = None
    if "--detail" in sys.argv:
        i = sys.argv.index("--detail")
        if i + 2 < len(sys.argv):
            detail_config = sys.argv[i + 1]
            detail_alpha = int(sys.argv[i + 2])

    runs = parse(path)
    print(f"[parsed] {len(runs)} runs (warmup discarded)")

    if not runs:
        print("No runs found."); sys.exit(1)

    computed = [compute(r) for r in runs]
    stats = aggregate(computed)
    if not stats:
        print("No valid data."); sys.exit(1)

    configs = sorted(set(s["config"] for s in stats))
    print(f"[configs] {configs}")
    print(f"[computed] {len(stats)} (config, alpha) groups\n")

    print_summary(stats)
    plot_per_config(stats)
    plot_comparison(stats)

    if detail_config and detail_alpha is not None:
        plot_detail(runs, detail_config, detail_alpha)

    print("\nDone.")


if __name__ == "__main__":
    main()
