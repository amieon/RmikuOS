#!/usr/bin/env python3
"""
stat_exp5.py -- Exp 5: Phase ratio impact on AIMD advantage.

Combines exp5 (40/10, 10/40) with exp4 (25/25) data to show 3 ratios.
Key question: does AIMD's advantage over fixed scale with L-segment proportion?

Usage:
    python3 ./scripts/sched/stat_exp5.py ./logs/sched/phase/sexp5_phase.csv ./logs/sched/dyn/sexp4_dyn.csv
    # exp4 CSV (sexp4 in filename) auto-imported as ratio=250 (25/25)
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
}

RATIOS = [250, 800, 200]
RATIO_LABELS = {250: "25/25", 800: "40/10", 200: "10/40"}
RATIO_L_PCT = {250: 50, 800: 80, 200: 20}
RATIO_SOURCE = {250: "exp4", 800: "exp5", 200: "exp5"}

PHASE_NAMES = ["L1", "H1", "L2", "H2"]
ALL_MODES = ["fixed0", "fixed25", "fixed50", "fixed75", "fixed100",
             "aimd0", "aimd50", "aimd100"]
AIMD_MODES = ["aimd0", "aimd50", "aimd100"]


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
    """解析 CSV。支持 exp5 格式(# RUN ratio=...) 和 exp4 格式(# RUN mode=...)。
    exp4 无 ratio 字段时用 default_ratio（默认 250=25/25）。"""
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

            # exp5 格式: # RUN ratio=R mode=M alpha0=A rep=N/3
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

            # exp4 格式: # RUN mode=M alpha0=A rep=N/3 (无 ratio，用 default)
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
                    cur["W"].append({"win": int(p[1]), "name": p[4],
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
            s["max_late"] = j["late_max"]
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

        for field in ["miss_rate", "avg_late", "jain"]:
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
            m = np.mean(vals_s)
            row.setdefault("share", {})[f"{name}_mean"] = m

        # phase miss
        if reps and len(reps[0].get("win_miss", [])) > 0:
            max_w = max(len(r.get("win_miss", [])) for r in reps)
            pb = phase_bounds_for_ratio(ratio, max_w)
            for pi, pname in enumerate(PHASE_NAMES):
                lo = pb[pi]
                hi = pb[pi + 1]
                phase_vals = []
                for r in reps:
                    wm = r.get("win_miss", [])
                    if len(wm) > lo:
                        seg = wm[lo:min(hi, len(wm))]
                        if len(seg) > 0:
                            phase_vals.append(np.mean(seg))
                if phase_vals:
                    row[f"miss_{pname}"] = np.mean(phase_vals)

        # alpha steady (后半段均值)
        if mode.startswith("aimd"):
            trajs = [r["alpha_traj"] for r in reps if len(r.get("alpha_traj", [])) > 0]
            if trajs:
                min_len = min(len(t) for t in trajs)
                aligned = np.array([t[:min_len] for t in trajs])
                half = min_len // 2
                row["alpha_steady"] = aligned[:, half:].mean()
            else:
                row["alpha_steady"] = 0
        else:
            row["alpha_steady"] = row.get("mode", "").startswith("fixed") and int(row["mode"][5:]) or 0

        stats[(ratio, mode)] = row
    return stats


# ------------------------------------------------------------------ print
def fmt_err(mean, hi, lo, wm=7, we=5):
    return f"{mean:>{wm}.1f} +{hi:>{we}.1f}/-{lo:>{we}.1f}"


def print_summary(stats):
    print("=" * 150)
    print("EXPERIMENT 5: PHASE RATIO IMPACT ON AIMD ADVANTAGE")
    print("=" * 150)

    for ratio in RATIOS:
        label = RATIO_LABELS.get(ratio, str(ratio))
        l_pct = RATIO_L_PCT.get(ratio, 50)
        src = RATIO_SOURCE.get(ratio, "?")
        print(f"\n--- RATIO {label} (L={l_pct}%) [{src}] ---")
        hdr = (f"{'mode':>8} {'α0':>4}  {'miss%':>20}  "
               f"{'miss_L1':>8} {'miss_H1':>8} {'miss_L2':>8} {'miss_H2':>8}  "
               f"{'sh_ai':>7}  {'ai_burn':>9}  {'ai_run':>8}  {'α_steady':>8}  {'Jain':>6}")
        print(hdr)
        print("-" * 150)
        for mode in ALL_MODES:
            s = stats.get((ratio, mode))
            if not s:
                continue
            miss_s = fmt_err(s.get('miss_rate_mean', 0), s.get('miss_rate_hi', 0), s.get('miss_rate_lo', 0))
            l1 = s.get('miss_L1', 0)
            h1 = s.get('miss_H1', 0)
            l2 = s.get('miss_L2', 0)
            h2 = s.get('miss_H2', 0)
            ai_sh = s.get('share', {}).get('ai_mean', 0)
            ai_burn = s.get('work', {}).get('ai_mean', 0)
            ai_run = s.get('run', {}).get('ai_mean', 0)
            a_steady = s.get('alpha_steady', 0)
            print(f"{mode:>8} {s.get('alpha0',0):>4}  {miss_s}  "
                  f"{l1:>8.1f} {h1:>8.1f} {l2:>8.1f} {h2:>8.1f}  "
                  f"{ai_sh:>7.1f}  {ai_burn:>9.0f}  {ai_run:>8.0f}  {a_steady:>8.1f}  "
                  f"{s.get('jain_mean',0):>6.3f}")
    print("=" * 150)

    # AIMD vs fixed25 对比
    print("\n--- AIMD vs fixed25: burn lead & miss delta ---")
    hdr = (f"{'ratio':>8}  {'f25_burn':>9}  {'aimd_burn':>9}  {'burn_lead%':>10}  "
           f"{'f25_miss':>8}  {'aimd_miss':>9}  {'miss_delta':>10}")
    print(hdr)
    print("-" * 80)
    for ratio in RATIOS:
        label = RATIO_LABELS[ratio]
        f25 = stats.get((ratio, "fixed25"), {})
        for am in AIMD_MODES:
            aimd = stats.get((ratio, am), {})
            if not f25 or not aimd:
                continue
            f_burn = f25.get("work", {}).get("ai_mean", 0)
            a_burn = aimd.get("work", {}).get("ai_mean", 0)
            lead = (a_burn - f_burn) / f_burn * 100 if f_burn > 0 else 0
            f_miss = f25.get("miss_rate_mean", 0)
            a_miss = aimd.get("miss_rate_mean", 0)
            print(f"{label:>8}  {f_burn:>9.0f}  {a_burn:>9.0f}  {lead:>+9.1f}%  "
                  f"{f_miss:>8.1f}  {a_miss:>9.1f}  {a_miss-f_miss:>+9.1f}")
    print("=" * 80)


# ------------------------------------------------------------------ plots
def plot_advantage_vs_ratio(stats, outdir):
    """核心图: AIMD burn 领先 fixed25 的百分比 vs L段比例。"""
    fig, ax = plt.subplots(figsize=(8, 5))

    for am, color in [("aimd0", COLORS_MODE["aimd0"]),
                       ("aimd50", COLORS_MODE["aimd50"]),
                       ("aimd100", COLORS_MODE["aimd100"])]:
        xs, ys = [], []
        for ratio in RATIOS:
            f25 = stats.get((ratio, "fixed25"), {})
            aimd = stats.get((ratio, am), {})
            if not f25 or not aimd:
                continue
            f_burn = f25.get("work", {}).get("ai_mean", 0)
            a_burn = aimd.get("work", {}).get("ai_mean", 0)
            lead = (a_burn - f_burn) / f_burn * 100 if f_burn > 0 else 0
            xs.append(RATIO_L_PCT[ratio])
            ys.append(lead)
        if xs:
            ax.plot(xs, ys, "o-", color=color, lw=2, ms=8, label=am)

    ax.axhline(0, color="gray", ls="--", lw=1)
    ax.set_xlabel("L-segment Proportion (%)")
    ax.set_ylabel("AIMD burn lead over fixed25 (%)")
    ax.set_title("Exp 5: AIMD Advantage vs L-segment Proportion")
    l_pcts = sorted(set(RATIO_L_PCT.get(r, 50) for r in RATIOS))
    ax.set_xticks(l_pcts)
    ax.legend()
    ax.grid(True, alpha=0.3)
    fig.tight_layout()
    out = os.path.join(outdir, "exp5_advantage_vs_ratio.png")
    fig.savefig(out); print(f"[saved] {out}"); plt.close(fig)


def plot_burn_vs_miss_per_ratio(stats, outdir):
    """每个 ratio 一张 burn vs miss 散点。"""
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
            x_hi = s.get("work", {}).get("ai_hi", 0)
            x_lo = s.get("work", {}).get("ai_lo", 0)
            y_hi = s.get("miss_rate_hi", 0)
            y_lo = s.get("miss_rate_lo", 0)
            xs.append(x); ys.append(y)
            color = COLORS_MODE.get(mode, "#666")
            ax.errorbar(x, y, xerr=[[x_lo], [x_hi]], yerr=[[y_lo], [y_hi]],
                        fmt="o", color=color, ms=8, capsize=3,
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

    fig.suptitle("Exp 5: Burn vs Miss by Phase Ratio", fontsize=13)
    fig.tight_layout(rect=[0, 0, 1, 0.95])
    out = os.path.join(outdir, "exp5_burn_vs_miss.png")
    fig.savefig(out); print(f"[saved] {out}"); plt.close(fig)


def plot_alpha_traj_per_ratio(computed, outdir):
    """每个 ratio 一张 α 轨迹图。"""
    n = len(RATIOS)
    fig, axes = plt.subplots(1, n, figsize=(6*n, 5))
    if n == 1: axes = [axes]
    for idx, ratio in enumerate(RATIOS):
        ax = axes[idx]
        for mode, color in [("aimd0", COLORS_MODE["aimd0"]),
                             ("aimd50", COLORS_MODE["aimd50"]),
                             ("aimd100", COLORS_MODE["aimd100"])]:
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
        ax.set_title(f"Ratio {RATIO_LABELS[ratio]} (L={RATIO_L_PCT[ratio]}%)")
        ax.set_xlabel("Window")
        ax.set_ylabel("α")
        ax.set_ylim(-2, 105)
        ax.legend(fontsize=8)

    fig.suptitle("Exp 5: α Trajectory by Phase Ratio", fontsize=13)
    fig.tight_layout(rect=[0, 0, 1, 0.95])
    out = os.path.join(outdir, "exp5_alpha_traj.png")
    fig.savefig(out); print(f"[saved] {out}"); plt.close(fig)


def plot_miss_traj_per_ratio(computed, outdir):
    """每个 ratio 一张逐窗口 miss rate。"""
    n = len(RATIOS)
    fig, axes = plt.subplots(1, n, figsize=(6*n, 5))
    if n == 1: axes = [axes]
    for idx, ratio in enumerate(RATIOS):
        ax = axes[idx]
        for mode in ALL_MODES:
            reps = [r for r in computed if r["ratio"] == ratio and r["mode"] == mode
                    and len(r.get("win_miss", [])) > 0]
            if not reps:
                continue
            min_len = min(len(r["win_miss"]) for r in reps)
            aligned = np.array([r["win_miss"][:min_len] for r in reps])
            mean = aligned.mean(axis=0)
            x = np.arange(len(mean))
            color = COLORS_MODE.get(mode, "#666")
            ax.plot(x, mean, "-", color=color, lw=1.0, label=mode)

        max_w = min_len
        pb = phase_bounds_for_ratio(ratio, max_w)
        add_phase_shading(ax, pb, 105)
        ax.set_title(f"Ratio {RATIO_LABELS[ratio]} (L={RATIO_L_PCT[ratio]}%)")
        ax.set_xlabel("Window")
        ax.set_ylabel("ctrl Miss Rate (%)")
        ax.set_ylim(-2, 105)
        ax.legend(fontsize=7)
        ax.set_xlim(0, min_len)

    fig.suptitle("Exp 5: Miss Rate Trajectory by Phase Ratio", fontsize=13)
    fig.tight_layout(rect=[0, 0, 1, 0.95])
    out = os.path.join(outdir, "exp5_miss_traj.png")
    fig.savefig(out); print(f"[saved] {out}"); plt.close(fig)


# ------------------------------------------------------------------ main
def main():
    if len(sys.argv) < 2:
        print(f"Usage: {sys.argv[0]} <sexp5_phase.csv> [sexp4_dyn.csv]")
        print(f"  exp5 CSV: 40/10 + 10/40 ratios")
        print(f"  exp4 CSV (optional): 25/25 ratio, 自动作为 ratio=250 导入")
        sys.exit(1)

    all_runs = []
    outdir = None
    for path in sys.argv[1:]:
        # exp4 文件名含 sexp4 → default_ratio=250 (25/25)
        is_exp4 = "sexp4" in os.path.basename(path)
        default_r = 250 if is_exp4 else None
        runs = parse(path, default_ratio=default_r)
        tag = "exp4(25/25)" if is_exp4 else os.path.basename(path)
        print(f"[parsed] {len(runs)} runs from {tag}")
        all_runs.extend(runs)
        if outdir is None:
            outdir = os.path.dirname(path) or "./logs/sched/phase"

    print(f"[total] {len(all_runs)} runs")
    os.makedirs(outdir, exist_ok=True)

    computed = [compute(r) for r in all_runs]
    stats = aggregate(computed)

    print()
    print_summary(stats)

    # 只画存在的 ratio
    found_ratios = sorted(set(r["ratio"] for r in computed))
    global RATIOS
    RATIOS = found_ratios

    plot_advantage_vs_ratio(stats, outdir)
    plot_burn_vs_miss_per_ratio(stats, outdir)
    plot_alpha_traj_per_ratio(computed, outdir)
    plot_miss_traj_per_ratio(computed, outdir)

    print("\nDone.")


if __name__ == "__main__":
    main()
