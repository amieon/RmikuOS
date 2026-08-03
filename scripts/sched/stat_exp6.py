#!/usr/bin/env python3
"""
stat_exp6.py -- Exp 6: SPSA-AdamW vs AIMD comparison.

Combines exp6 (AdamW) + exp5 (AIMD 40/10, 10/40) + exp4 (AIMD 25/25, fixed).
Key question: can gradient-based AdamW match heuristic AIMD?

Usage:
    python3 stat_exp6.py ./logs/sched/adamw/sexp6_adamw.csv \
        ./logs/sched/phase/sexp5_phase.csv \
        ./logs/sched/dyn/sexp4_dyn.csv
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

COLORS_MODE = {
    "fixed0":   "#94a3b8",
    "fixed25":  "#788490",
    "fixed50":  "#64748b",
    "fixed75":  "#50545d",
    "fixed100": "#475569",
    "aimd0":    "#0891b2",
    "aimd50":   "#2563eb",
    "aimd100":  "#dc2626",
    "adamw0":   "#059669",
    "adamw50":  "#7c3aed",
    "adamw100": "#db2777",
}

RATIOS = [250, 800, 200]
RATIO_LABELS = {250: "25/25", 800: "40/10", 200: "10/40"}
RATIO_L_PCT = {250: 50, 800: 80, 200: 20}

PHASE_NAMES = ["L1", "H1", "L2", "H2"]
ALL_MODES = ["fixed0", "fixed25", "fixed50", "fixed75", "fixed100",
             "aimd0", "aimd50", "aimd100",
             "adamw0", "adamw50", "adamw100"]
AIMD_MODES = ["aimd0", "aimd50", "aimd100"]
ADAMW_MODES = ["adamw0", "adamw50", "adamw100"]


def phase_bounds_for_ratio(ratio, max_win):
    l_frac = ratio / 1000.0
    b1 = int(max_win * l_frac / 2)
    b2 = max_win // 2
    b3 = int(max_win * (0.5 + l_frac / 2))
    return [0, b1, b2, b3, max_win]


def add_phase_shading(ax, bounds, ymax=105):
    colors = ["#bbf7d0", "#fecaca", "#bbf7d0", "#fecaca"]
    for i in range(4):
        ax.axvspan(bounds[i], bounds[i+1], color=colors[i], alpha=0.4, zorder=0)
    for b in bounds[1:-1]:
        ax.axvline(b, color="#333", ls="--", lw=1, alpha=0.6, zorder=1)
    for i, name in enumerate(PHASE_NAMES):
        mid = (bounds[i] + bounds[i+1]) / 2
        ax.text(mid, ymax * 0.97, name, ha="center", va="top",
                fontsize=10, color="#333", fontweight="bold")


# ------------------------------------------------------------------ parse
def parse(path, default_ratio=None):
    runs = []
    cur = None
    in_warmup = False

    with open(path) as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            if line.startswith("# WARMUP"):
                if cur is not None and not in_warmup:
                    runs.append(cur)
                in_warmup = True
                cur = None
                continue

            m = re.search(r'# RUN ratio=(\d+) mode=(\w+) alpha0=(\d+) rep=(\d+)/\d+', line)
            if m:
                if cur is not None and not in_warmup:
                    runs.append(cur)
                in_warmup = False
                cur = {
                    "ratio": int(m.group(1)),
                    "mode": m.group(2),
                    "alpha0": int(m.group(3)),
                    "rep": int(m.group(4)),
                    "W": [], "D": [], "A": [], "S": [], "J": [], "K": []
                }
                continue

            m = re.search(r'# RUN mode=(\w+) alpha0=(\d+) rep=(\d+)/\d+', line)
            if m:
                if cur is not None and not in_warmup:
                    runs.append(cur)
                in_warmup = False
                cur = {
                    "ratio": default_ratio or 250,
                    "mode": m.group(1),
                    "alpha0": int(m.group(2)),
                    "rep": int(m.group(3)),
                    "W": [], "D": [], "A": [], "S": [], "J": [], "K": []
                }
                continue

            if in_warmup or cur is None:
                continue

            p = line.split(",")
            tag = p[0]
            try:
                if tag == "W" and len(p) >= 8:
                    cur["W"].append({"win": int(p[1]), "alpha": int(p[2]), "name": p[4],
                                   "run_delta": int(p[5]), "eff_tickets": int(p[6]),
                                   "ready_threads": int(p[7])})
                elif tag == "D" and len(p) >= 6:
                    cur["D"].append({"win": int(p[1]), "jobs_delta": int(p[3]),
                                     "miss_delta": int(p[4]), "late_delta": int(p[5])})
                elif tag == "A" and len(p) >= 5:
                    cur["A"].append({"win": int(p[1]), "before": int(p[2]),
                                     "after": int(p[3]), "action": p[4]})
                elif tag == "S" and len(p) >= 5:
                    cur["S"].append({"win": int(p[1]), "jain_q": int(p[3]),
                                     "max_slowdown_q": int(p[4])})
                elif tag == "J" and len(p) >= 12:
                    cur["J"].append({"name": p[2], "jobs": int(p[4]), "miss": int(p[5]),
                                     "late_sum": int(p[6]), "late_max": int(p[7]),
                                     "resp_sum": int(p[8]), "resp_sumsq": int(p[9]),
                                     "resp_min": int(p[10]), "resp_max": int(p[11])})
                elif tag == "K" and len(p) >= 5:
                    cur["K"].append({"name": p[2], "threads": int(p[3]), "work": int(p[4])})
            except (IndexError, ValueError):
                continue

    if cur is not None and not in_warmup:
        runs.append(cur)
    return runs


# ------------------------------------------------------------------ compute
def compute(run):
    s = {"ratio": run["ratio"], "mode": run["mode"],
         "alpha0": run["alpha0"], "rep": run["rep"]}

    for j in run["J"]:
        if j["name"] == "ctrl":
            s["jobs"] = j["jobs"]
            s["miss"] = j["miss"]
            s["miss_rate"] = j["miss"] / j["jobs"] * 100.0 if j["jobs"] > 0 else 0.0
            s["avg_late"] = j["late_sum"] / j["miss"] if j["miss"] > 0 else 0.0
            break

    ws = [w for w in run["W"] if w["win"] > 3]
    for name in ["ctrl", "ai", "log"]:
        s.setdefault("run", {})[name] = sum(w["run_delta"] for w in ws if w["name"] == name)
    total_all = sum(w["run_delta"] for w in ws)
    for name in ["ctrl", "ai", "log"]:
        total_name = sum(w["run_delta"] for w in ws if w["name"] == name)
        s.setdefault("share", {})[name] = total_name / total_all * 100 if total_all > 0 else 0

    for k in run["K"]:
        s.setdefault("work", {})[k["name"]] = k["work"]
    for j in run["J"]:
        if j["name"] == "ctrl":
            s.setdefault("work", {})["ctrl"] = j["jobs"]

    if run["D"]:
        s["win_miss"] = np.array([
            d["miss_delta"] / d["jobs_delta"] * 100.0 if d["jobs_delta"] > 0 else 0.0
            for d in run["D"]
        ])
    else:
        s["win_miss"] = np.array([])

    if run["A"]:
        s["alpha_traj"] = np.array([a["after"] for a in run["A"]])
        s["alpha_wins"] = np.array([a["win"] for a in run["A"]])
    else:
        # Fallback: 从 W 行提取 alpha（probe 值,±delta 震荡 around 实际 α）
        # AdamW 无 A 行时用此路径; 取相邻窗口均值平滑 SPSA 扰动
        w_alpha = [(w["win"], w.get("alpha", 0)) for w in run["W"] if w["name"] == "ctrl"]
        if w_alpha:
            wins = [x[0] for x in w_alpha]
            alphas = [x[1] for x in w_alpha]
            # 2-window 移动平均平滑 probe±delta 扰动
            if len(alphas) >= 2:
                smoothed = [(alphas[i] + alphas[i+1]) // 2 for i in range(len(alphas)-1)]
                s["alpha_traj"] = np.array(smoothed)
                s["alpha_wins"] = np.array(wins[:-1])
            else:
                s["alpha_traj"] = np.array(alphas)
                s["alpha_wins"] = np.array(wins)
        else:
            s["alpha_traj"] = np.array([])
            s["alpha_wins"] = np.array([])

    if run["S"]:
        s["jain"] = np.mean([x["jain_q"] for x in run["S"]]) / 1000.0
    else:
        s["jain"] = 0

    return s


def aggregate(runs):
    from collections import defaultdict
    by_group = defaultdict(list)
    for r in runs:
        key = (r["ratio"], r["mode"])
        by_group[key].append(r)

    stats = {}
    for (ratio, mode), reps in by_group.items():
        row = {"ratio": ratio, "mode": mode, "nreps": len(reps)}

        for field in ["miss_rate", "jain"]:
            vals = [r.get(field, 0) for r in reps]
            m = np.mean(vals)
            row[f"{field}_mean"] = m
            row[f"{field}_hi"] = max(vals) - m
            row[f"{field}_lo"] = m - min(vals)

        for name in ["ai"]:
            vals_b = [r.get("work", {}).get(name, 0) for r in reps]
            m = np.mean(vals_b)
            row.setdefault("work", {})[f"{name}_mean"] = m
            row.setdefault("work", {})[f"{name}_hi"] = max(vals_b) - m
            row.setdefault("work", {})[f"{name}_lo"] = m - min(vals_b)

            vals_r = [r.get("run", {}).get(name, 0) for r in reps]
            m = np.mean(vals_r)
            row.setdefault("run", {})[f"{name}_mean"] = m
            row.setdefault("run", {})[f"{name}_hi"] = max(vals_r) - m
            row.setdefault("run", {})[f"{name}_lo"] = m - min(vals_r)

            vals_s = [r.get("share", {}).get(name, 0) for r in reps]
            row.setdefault("share", {})[f"{name}_mean"] = np.mean(vals_s)

        if mode.startswith("aimd") or mode.startswith("adamw"):
            trajs = [r["alpha_traj"] for r in reps if len(r.get("alpha_traj", [])) > 0]
            if trajs:
                min_len = min(len(t) for t in trajs)
                aligned = np.array([t[:min_len] for t in trajs])
                half = min_len // 2
                row["alpha_steady"] = aligned[:, half:].mean()
            else:
                row["alpha_steady"] = 0
        else:
            row["alpha_steady"] = int(mode[5:]) if mode.startswith("fixed") else 0

        stats[(ratio, mode)] = row
    return stats


# ------------------------------------------------------------------ print
def fmt_err(mean, hi, lo, wm=7, we=5):
    return f"{mean:>{wm}.1f} +{hi:>{we}.1f}/-{lo:>{we}.1f}"


def print_summary(stats):
    print("=" * 155)
    print("EXPERIMENT 6: SPSA-ADAMW vs AIMD")
    print("=" * 155)

    for ratio in RATIOS:
        label = RATIO_LABELS.get(ratio, str(ratio))
        l_pct = RATIO_L_PCT.get(ratio, 50)
        print(f"\n--- RATIO {label} (L={l_pct}%) ---")
        hdr = (f"{'mode':>9}  {'miss%':>20}  "
               f"{'sh_ai':>7}  {'ai_burn':>9}  {'ai_run':>8}  {'α_steady':>8}  {'Jain':>6}")
        print(hdr)
        print("-" * 155)
        for mode in ALL_MODES:
            s = stats.get((ratio, mode))
            if not s:
                continue
            miss_s = fmt_err(s.get('miss_rate_mean', 0), s.get('miss_rate_hi', 0), s.get('miss_rate_lo', 0))
            ai_sh = s.get('share', {}).get('ai_mean', 0)
            ai_burn = s.get('work', {}).get('ai_mean', 0)
            ai_run = s.get('run', {}).get('ai_mean', 0)
            a_steady = s.get('alpha_steady', 0)
            print(f"{mode:>9}  {miss_s}  "
                  f"{ai_sh:>7.1f}  {ai_burn:>9.0f}  {ai_run:>8.0f}  {a_steady:>8.1f}  "
                  f"{s.get('jain_mean',0):>6.3f}")
    print("=" * 155)

    # AdamW vs AIMD vs fixed25
    print("\n--- AdamW vs AIMD vs fixed25: burn lead ---")
    hdr = (f"{'ratio':>8}  {'ctrl':>8}  {'f25_burn':>9}  {'burn':>9}  {'lead%':>8}  "
           f"{'miss':>6}  {'α_steady':>8}")
    print(hdr)
    print("-" * 70)
    for ratio in RATIOS:
        label = RATIO_LABELS[ratio]
        f25 = stats.get((ratio, "fixed25"), {})
        f_burn = f25.get("work", {}).get("ai_mean", 0)
        f_miss = f25.get("miss_rate_mean", 0)
        for ctrl_mode in AIMD_MODES + ADAMW_MODES:
            s = stats.get((ratio, ctrl_mode), {})
            if not s:
                continue
            burn = s.get("work", {}).get("ai_mean", 0)
            lead = (burn - f_burn) / f_burn * 100 if f_burn > 0 else 0
            miss = s.get("miss_rate_mean", 0)
            a_st = s.get("alpha_steady", 0)
            print(f"{label:>8}  {ctrl_mode:>8}  {f_burn:>9.0f}  {burn:>9.0f}  {lead:>+7.1f}%  "
                  f"{miss:>6.1f}  {a_st:>8.1f}")
    print("=" * 70)


# ------------------------------------------------------------------ plots
def plot_alpha_traj_adamw_vs_aimd(computed, outdir):
    """AdamW vs AIMD α 轨迹对比,每个 ratio 一张。"""
    n = len(RATIOS)
    fig, axes = plt.subplots(1, n, figsize=(6*n, 5))
    if n == 1: axes = [axes]

    for idx, ratio in enumerate(RATIOS):
        ax = axes[idx]
        for mode, color in [("aimd0", COLORS_MODE["aimd0"]),
                             ("aimd50", COLORS_MODE["aimd50"]),
                             ("aimd100", COLORS_MODE["aimd100"]),
                             ("adamw0", COLORS_MODE["adamw0"]),
                             ("adamw50", COLORS_MODE["adamw50"]),
                             ("adamw100", COLORS_MODE["adamw100"])]:
            reps = [r for r in computed if r["ratio"] == ratio and r["mode"] == mode
                    and len(r.get("alpha_traj", [])) > 0]
            if not reps:
                continue
            all_lens = [len(r["alpha_traj"]) for r in reps]
            gmin = min(all_lens)
            aligned = np.array([r["alpha_traj"][:gmin] for r in reps])
            mean = aligned.mean(axis=0)
            x = np.arange(gmin)
            ls = "-" if mode.startswith("aimd") else "--"
            ax.plot(x, mean, ls, color=color, lw=2, label=mode)

        max_w = max((max(r.get("alpha_wins", [0]).max() if len(r.get("alpha_wins", [])) > 0 else 0,
                    0) for r in reps if reps), default=100)
        pb = phase_bounds_for_ratio(ratio, max_w)
        add_phase_shading(ax, pb, 105)
        ax.set_title(f"Ratio {RATIO_LABELS[ratio]} (L={RATIO_L_PCT[ratio]}%)")
        ax.set_xlabel("Window")
        ax.set_ylabel("α")
        ax.set_ylim(-2, 105)
        ax.legend(fontsize=7)

    fig.suptitle("Exp 6: AdamW (dashed) vs AIMD (solid) α Trajectory", fontsize=13)
    fig.tight_layout(rect=[0, 0, 1, 0.95])
    out = os.path.join(outdir, "exp6_alpha_traj_adamw_vs_aimd.png")
    fig.savefig(out); print(f"[saved] {out}"); plt.close(fig)


def plot_advantage_adamw_vs_aimd(stats, outdir):
    """AdamW vs AIMD: burn lead over fixed25 vs L段比例。"""
    fig, ax = plt.subplots(figsize=(8, 5))

    for ctrl_modes, label_prefix in [(AIMD_MODES, "aimd"), (ADAMW_MODES, "adamw")]:
        for am in ctrl_modes:
            xs, ys = [], []
            for ratio in RATIOS:
                f25 = stats.get((ratio, "fixed25"), {})
                s = stats.get((ratio, am), {})
                if not f25 or not s:
                    continue
                f_burn = f25.get("work", {}).get("ai_mean", 0)
                a_burn = s.get("work", {}).get("ai_mean", 0)
                lead = (a_burn - f_burn) / f_burn * 100 if f_burn > 0 else 0
                xs.append(RATIO_L_PCT[ratio])
                ys.append(lead)
            if xs:
                color = COLORS_MODE.get(am, "#666")
                ls = "-" if am.startswith("aimd") else "--"
                ax.plot(xs, ys, ls, color=color, lw=2, ms=8, label=am)

    ax.axhline(0, color="gray", ls="--", lw=1)
    ax.set_xlabel("L-segment Proportion (%)")
    ax.set_ylabel("Burn lead over fixed25 (%)")
    ax.set_title("Exp 6: AdamW (dashed) vs AIMD (solid) Advantage")
    l_pcts = sorted(set(RATIO_L_PCT.get(r, 50) for r in RATIOS))
    ax.set_xticks(l_pcts)
    ax.legend()
    ax.grid(True, alpha=0.3)
    fig.tight_layout()
    out = os.path.join(outdir, "exp6_advantage_adamw_vs_aimd.png")
    fig.savefig(out); print(f"[saved] {out}"); plt.close(fig)


def plot_burn_vs_miss_per_ratio(stats, outdir):
    """每个 ratio 一张 burn vs miss 散点,包含 AdamW。"""
    n = len(RATIOS)
    fig, axes = plt.subplots(1, n, figsize=(6*n, 5))
    if n == 1: axes = [axes]

    for idx, ratio in enumerate(RATIOS):
        ax = axes[idx]
        xs, ys = [], []
        for mode in ALL_MODES:
            s = stats.get((ratio, mode))
            if not s:
                continue
            x = s.get("work", {}).get("ai_mean", 0)
            y = s.get("miss_rate_mean", 0)
            xs.append(x); ys.append(y)
            color = COLORS_MODE.get(mode, "#666")
            marker = "s" if mode.startswith("adamw") else "o"
            ax.plot(x, y, marker, color=color, ms=8,
                    markeredgecolor="black", markeredgewidth=0.8, label=mode)
            ax.annotate(mode, (x, y), fontsize=7, ha="left", va="bottom",
                        xytext=(6, 4), textcoords="offset points")
        ax.set_title(f"Ratio {RATIO_LABELS[ratio]} (L={RATIO_L_PCT[ratio]}%)")
        ax.set_xlabel("ai Burn (iterations)")
        ax.set_ylabel("ctrl Miss Rate (%)")
        if xs:
            x_span = max(xs) - min(xs) if max(xs) > min(xs) else max(xs) * 0.2
            ax.set_xlim(min(xs) - x_span * 0.15, max(xs) + x_span * 0.25)
        y_span = max(ys) - min(ys) if max(ys) > min(ys) else 10
        ax.set_ylim(max(-5, min(ys) - y_span * 0.15), min(105, max(ys) + y_span * 0.25))
        ax.legend(fontsize=7)
        ax.grid(True, alpha=0.3)

    fig.suptitle("Exp 6: Burn vs Miss (AdamW=square, AIMD/fixed=circle)", fontsize=13)
    fig.tight_layout(rect=[0, 0, 1, 0.95])
    out = os.path.join(outdir, "exp6_burn_vs_miss.png")
    fig.savefig(out); print(f"[saved] {out}"); plt.close(fig)


# ---------- AdamW 独立图 ----------

def plot_adamw_alpha_traj(computed, outdir):
    """AdamW 三起点 α 轨迹,每个 ratio 一张。"""
    n = len(RATIOS)
    fig, axes = plt.subplots(1, n, figsize=(6*n, 5))
    if n == 1: axes = [axes]

    for idx, ratio in enumerate(RATIOS):
        ax = axes[idx]
        for mode, color in [("adamw0", COLORS_MODE["adamw0"]),
                             ("adamw50", COLORS_MODE["adamw50"]),
                             ("adamw100", COLORS_MODE["adamw100"])]:
            reps = [r for r in computed if r["ratio"] == ratio and r["mode"] == mode
                    and len(r.get("alpha_traj", [])) > 0]
            if not reps:
                continue
            all_lens = [len(r["alpha_traj"]) for r in reps]
            gmin = min(all_lens)
            aligned = np.array([r["alpha_traj"][:gmin] for r in reps])
            mean = aligned.mean(axis=0)
            x = np.arange(gmin)
            for r in reps:
                ax.plot(x, r["alpha_traj"][:gmin], "-", color=color, lw=0.4, alpha=0.3)
            ax.plot(x, mean, "-", color=color, lw=2, label=f"{mode} (n={len(reps)})")

        max_w = max((max(r.get("alpha_wins", [0]).max() if len(r.get("alpha_wins", [])) > 0 else 0,
                    0) for r in reps if reps), default=100)
        pb = phase_bounds_for_ratio(ratio, max_w)
        add_phase_shading(ax, pb, 105)
        ax.axhline(25, color="gray", ls=":", lw=1, label="target=25")
        ax.set_title(f"Ratio {RATIO_LABELS[ratio]} (L={RATIO_L_PCT[ratio]}%)")
        ax.set_xlabel("Window")
        ax.set_ylabel("α")
        ax.set_ylim(-2, 105)
        ax.legend(fontsize=8)

    fig.suptitle("Exp 6: AdamW α Trajectory (3 starts)", fontsize=13)
    fig.tight_layout(rect=[0, 0, 1, 0.95])
    out = os.path.join(outdir, "exp6_adamw_alpha_traj.png")
    fig.savefig(out); print(f"[saved] {out}"); plt.close(fig)


def plot_adamw_miss_traj(computed, outdir):
    """AdamW 逐窗口 miss rate,每个 ratio 一张。"""
    n = len(RATIOS)
    fig, axes = plt.subplots(1, n, figsize=(6*n, 5))
    if n == 1: axes = [axes]

    for idx, ratio in enumerate(RATIOS):
        ax = axes[idx]
        for mode in ["fixed0", "fixed25", "fixed50"] + ADAMW_MODES:
            reps = [r for r in computed if r["ratio"] == ratio and r["mode"] == mode
                    and len(r.get("win_miss", [])) > 0]
            if not reps:
                continue
            min_len = min(len(r["win_miss"]) for r in reps)
            aligned = np.array([r["win_miss"][:min_len] for r in reps])
            mean = aligned.mean(axis=0)
            x = np.arange(len(mean))
            color = COLORS_MODE.get(mode, "#666")
            ls = "--" if mode.startswith("adamw") else "-"
            ax.plot(x, mean, ls, color=color, lw=1.5, label=mode)

        pb = phase_bounds_for_ratio(ratio, min_len)
        add_phase_shading(ax, pb, 105)
        ax.set_title(f"Ratio {RATIO_LABELS[ratio]} (L={RATIO_L_PCT[ratio]}%)")
        ax.set_xlabel("Window")
        ax.set_ylabel("ctrl Miss Rate (%)")
        ax.set_ylim(-2, 105)
        ax.legend(fontsize=7)
        ax.set_xlim(0, min_len)

    fig.suptitle("Exp 6: AdamW Miss Rate Trajectory", fontsize=13)
    fig.tight_layout(rect=[0, 0, 1, 0.95])
    out = os.path.join(outdir, "exp6_adamw_miss_traj.png")
    fig.savefig(out); print(f"[saved] {out}"); plt.close(fig)


def plot_adamw_burn_vs_miss(computed, stats, outdir):
    """AdamW 独立 burn vs miss 散点,每个 ratio 一张。"""
    n = len(RATIOS)
    fig, axes = plt.subplots(1, n, figsize=(6*n, 5))
    if n == 1: axes = [axes]

    for idx, ratio in enumerate(RATIOS):
        ax = axes[idx]
        modes_to_plot = ["fixed0", "fixed25", "fixed50"] + ADAMW_MODES
        xs, ys = [], []
        for mode in modes_to_plot:
            s = stats.get((ratio, mode))
            if not s:
                continue
            x = s.get("work", {}).get("ai_mean", 0)
            y = s.get("miss_rate_mean", 0)
            x_hi = s.get("work", {}).get("ai_hi", 0)
            x_lo = s.get("work", {}).get("ai_lo", 0)
            y_hi = s.get("miss_rate_hi", 0)
            y_lo = s.get("miss_rate_lo", 0)
            xs.append(x); ys.append(y)
            color = COLORS_MODE.get(mode, "#666")
            marker = "s" if mode.startswith("adamw") else "o"
            ax.errorbar(x, y, xerr=[[x_lo], [x_hi]], yerr=[[y_lo], [y_hi]],
                        fmt=marker, color=color, ms=9, capsize=3,
                        markeredgecolor="black", markeredgewidth=0.8, label=mode)
            ax.annotate(mode, (x, y), fontsize=8, ha="left", va="bottom",
                        xytext=(6, 4), textcoords="offset points")
        ax.set_title(f"Ratio {RATIO_LABELS[ratio]} (L={RATIO_L_PCT[ratio]}%)")
        ax.set_xlabel("ai Burn (iterations)")
        ax.set_ylabel("ctrl Miss Rate (%)")
        if xs:
            x_span = max(xs) - min(xs) if max(xs) > min(xs) else max(xs) * 0.2
            ax.set_xlim(min(xs) - x_span * 0.15, max(xs) + x_span * 0.25)
        y_span = max(ys) - min(ys) if max(ys) > min(ys) else 10
        ax.set_ylim(max(-5, min(ys) - y_span * 0.15), min(105, max(ys) + y_span * 0.25))
        ax.legend(fontsize=8)
        ax.grid(True, alpha=0.3)

    fig.suptitle("Exp 6: AdamW Burn vs Miss (standalone)", fontsize=13)
    fig.tight_layout(rect=[0, 0, 1, 0.95])
    out = os.path.join(outdir, "exp6_adamw_burn_vs_miss.png")
    fig.savefig(out); print(f"[saved] {out}"); plt.close(fig)


def plot_adamw_actions(computed, outdir):
    """AdamW actions 分布柱状图(up/down/hold)。"""
    from collections import Counter

    n = len(RATIOS)
    fig, axes = plt.subplots(1, n, figsize=(6*n, 4))
    if n == 1: axes = [axes]

    for idx, ratio in enumerate(RATIOS):
        ax = axes[idx]
        modes_present = []
        up_vals, down_vals, hold_vals = [], [], []
        for mode in ADAMW_MODES:
            reps = [r for r in computed if r["ratio"] == ratio and r["mode"] == mode]
            if not reps:
                continue
            # 从原始 run 的 A 行统计 actions
            total_up = total_down = total_hold = 0
            for r in reps:
                # alpha_traj 长度 = A 行数; actions 从 traj 差分推断
                traj = r.get("alpha_traj", [])
                wins = r.get("alpha_wins", [])
                if len(traj) < 2:
                    continue
                for i in range(1, len(traj)):
                    if traj[i] > traj[i-1]: total_up += 1
                    elif traj[i] < traj[i-1]: total_down += 1
                    else: total_hold += 1
            if total_up + total_down + total_hold == 0:
                continue
            modes_present.append(mode)
            up_vals.append(total_up)
            down_vals.append(total_down)
            hold_vals.append(total_hold)

        if not modes_present:
            ax.set_title(f"Ratio {RATIO_LABELS[ratio]} (no data)")
            continue

        x = np.arange(len(modes_present))
        w = 0.25
        ax.bar(x - w, up_vals, w, color="#16a34a", label="up", edgecolor="black", linewidth=0.5)
        ax.bar(x, down_vals, w, color="#dc2626", label="down", edgecolor="black", linewidth=0.5)
        ax.bar(x + w, hold_vals, w, color="#94a3b8", label="hold", edgecolor="black", linewidth=0.5)
        ax.set_xticks(x)
        ax.set_xticklabels(modes_present, fontsize=9)
        ax.set_ylabel("Count")
        ax.set_title(f"Ratio {RATIO_LABELS[ratio]} (L={RATIO_L_PCT[ratio]}%)")
        ax.legend(fontsize=8)

    fig.suptitle("Exp 6: AdamW Actions Distribution", fontsize=13)
    fig.tight_layout(rect=[0, 0, 1, 0.95])
    out = os.path.join(outdir, "exp6_adamw_actions.png")
    fig.savefig(out); print(f"[saved] {out}"); plt.close(fig)


def plot_adamw_phase_burn(computed, stats, outdir):
    """AdamW vs fixed25 分相位 ai_burn 对比柱状图。"""
    n = len(RATIOS)
    fig, axes = plt.subplots(1, n, figsize=(6*n, 4))
    if n == 1: axes = [axes]

    for idx, ratio in enumerate(RATIOS):
        ax = axes[idx]
        # 从 W 行按相位统计 ai run_delta
        modes_to_plot = ["fixed25"] + ADAMW_MODES
        x = np.arange(4)
        width = 0.8 / len(modes_to_plot)

        for mi, mode in enumerate(modes_to_plot):
            reps = [r for r in computed if r["ratio"] == ratio and r["mode"] == mode]
            if not reps:
                continue
            max_w = max((max(w["win"] for w in r.get("W", [(0,)])) for r in reps), default=100)
            pb = phase_bounds_for_ratio(ratio, max_w)
            phase_burns = []
            for pi in range(4):
                lo, hi = pb[pi], pb[pi+1]
                vals = []
                for r in reps:
                    ws_ai = [w for w in r.get("W", []) if w["name"] == "ai" and lo < w["win"] <= hi]
                    if ws_ai:
                        vals.append(sum(w["run_delta"] for w in ws_ai))
                phase_burns.append(np.mean(vals) if vals else 0)
            offset = (mi - (len(modes_to_plot)-1)/2) * width
            color = COLORS_MODE.get(mode, "#666")
            ax.bar(x + offset, phase_burns, width*0.9, color=color,
                   edgecolor="black", linewidth=0.4, label=mode)

        ax.set_xticks(x)
        ax.set_xticklabels(PHASE_NAMES)
        ax.set_ylabel("ai run_delta (CPU ticks)")
        ax.set_title(f"Ratio {RATIO_LABELS[ratio]} (L={RATIO_L_PCT[ratio]}%)")
        ax.legend(fontsize=8)

    fig.suptitle("Exp 6: AdamW ai CPU Time by Phase", fontsize=13)
    fig.tight_layout(rect=[0, 0, 1, 0.95])
    out = os.path.join(outdir, "exp6_adamw_phase_burn.png")
    fig.savefig(out); print(f"[saved] {out}"); plt.close(fig)


# ------------------------------------------------------------------ main
def main():
    if len(sys.argv) < 2:
        print(f"Usage: {sys.argv[0]} <sexp6_adamw.csv> [sexp5_phase.csv] [sexp4_dyn.csv]")
        print(f"  exp6: AdamW data (3 ratios)")
        print(f"  exp5: AIMD data (40/10, 10/40) — optional")
        print(f"  exp4: AIMD+fixed data (25/25) — optional, auto ratio=250")
        sys.exit(1)

    all_runs = []
    outdir = None
    for path in sys.argv[1:]:
        is_exp4 = "sexp4" in os.path.basename(path)
        default_r = 250 if is_exp4 else None
        runs = parse(path, default_ratio=default_r)
        tag = os.path.basename(path)
        print(f"[parsed] {len(runs)} runs from {tag}")
        all_runs.extend(runs)
        if outdir is None:
            outdir = os.path.dirname(path) or "./logs/sched/adamw"

    print(f"[total] {len(all_runs)} runs")
    os.makedirs(outdir, exist_ok=True)

    computed = [compute(r) for r in all_runs]
    stats = aggregate(computed)

    # 只画存在的 ratio
    global RATIOS
    RATIOS = sorted(set(r["ratio"] for r in computed))

    print()
    print_summary(stats)

    plot_alpha_traj_adamw_vs_aimd(computed, outdir)
    plot_advantage_adamw_vs_aimd(stats, outdir)
    plot_burn_vs_miss_per_ratio(stats, outdir)

    # AdamW 独立图
    plot_adamw_alpha_traj(computed, outdir)
    plot_adamw_miss_traj(computed, outdir)
    plot_adamw_burn_vs_miss(computed, stats, outdir)
    plot_adamw_actions(computed, outdir)
    plot_adamw_phase_burn(computed, stats, outdir)

    print("\nDone.")


if __name__ == "__main__":
    main()
