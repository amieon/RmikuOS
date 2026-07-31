#!/usr/bin/env python3
"""
stat_exp4.py -- Exp 4: Dynamic load (light-heavy-light-heavy).

Compares fixed0/fixed50 vs AIMD under phased load.
Key plots: α trajectory overlaid with phase boundaries,
per-window miss rate, and phase-segmented summary.

Usage:
    python3 stat_exp4.py ./logs/sched/dyn/sexp4_dyn.csv
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

# 四段相位边界(动态计算,默认 960 窗口)
PHASE_BOUNDS = [0, 240, 480, 720, 960]
PHASE_NAMES = ["L1", "H1", "L2", "H2"]
PHASE_COLORS = ["#bbf7d0", "#fecaca", "#bbf7d0", "#fecaca"]


# ------------------------------------------------------------------ parse
def parse(path):
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

            m = re.search(r'# RUN mode=(\w+) alpha0=(\d+) rep=(\d+)/\d+', line)
            if m:
                if cur is not None and not in_warmup:
                    runs.append(cur)
                in_warmup = False
                cur = {
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
    s = {"mode": run["mode"], "alpha0": run["alpha0"], "rep": run["rep"]}

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
        s.setdefault("work", {})[name] = sum(w["run_delta"] for w in ws if w["name"] == name)
    total_all = sum(w["run_delta"] for w in ws)
    for name in ["ctrl", "ai", "log"]:
        total_name = sum(w["run_delta"] for w in ws if w["name"] == name)
        s.setdefault("share", {})[name] = total_name / total_all * 100 if total_all > 0 else 0

    # 逐窗口 miss rate
    if run["D"]:
        s["win_miss"] = np.array([
            d["miss_delta"] / d["jobs_delta"] * 100.0 if d["jobs_delta"] > 0 else 0.0
            for d in run["D"]
        ])
        s["win_late"] = np.array([d["late_delta"] for d in run["D"]])
        s["win_jobs"] = np.array([d["jobs_delta"] for d in run["D"]])
    else:
        s["win_miss"] = np.array([])
        s["win_late"] = np.array([])
        s["win_jobs"] = np.array([])

    # ai 逐窗口 run_delta
    ai_ws = sorted([w for w in run["W"] if w["name"] == "ai"], key=lambda x: x["win"])
    if ai_ws:
        s["win_ai_rd"] = np.array([w["run_delta"] for w in ai_ws])
        s["win_ai_wins"] = np.array([w["win"] for w in ai_ws])
    else:
        s["win_ai_rd"] = np.array([])
        s["win_ai_wins"] = np.array([])

    # alpha 轨迹
    if run["A"]:
        s["alpha_traj"] = np.array([a["after"] for a in run["A"]])
        s["alpha_wins"] = np.array([a["win"] for a in run["A"]])
        actions = {}
        for a in run["A"]:
            actions[a["action"]] = actions.get(a["action"], 0) + 1
        s["actions"] = actions
    else:
        s["alpha_traj"] = np.array([])

    # Jain
    if run["S"]:
        s["jain"] = np.mean([x["jain_q"] for x in run["S"]]) / 1000.0
    else:
        s["jain"] = 0

    # K 行 (ai work)
    for k in run["K"]:
        if k["name"] == "ai":
            s["ai_work_k"] = k["work"]

    return s


def aggregate(runs):
    from collections import defaultdict
    by_mode = defaultdict(list)
    for r in runs:
        by_mode[r["mode"]].append(r)

    stats = []
    for mode, reps in sorted(by_mode.items()):
        row = {"mode": mode, "alpha0": reps[0]["alpha0"], "nreps": len(reps)}

        for field in ["miss_rate", "avg_late", "max_late", "jain"]:
            vals = [r.get(field, 0) for r in reps]
            m = np.mean(vals)
            row[f"{field}_mean"] = m
            row[f"{field}_hi"] = max(vals) - m
            row[f"{field}_lo"] = m - min(vals)

        for name in ["ctrl", "ai", "log"]:
            vals_w = [r.get("work", {}).get(name, 0) for r in reps]
            m = np.mean(vals_w)
            row.setdefault("work", {})[f"{name}_mean"] = m
            row.setdefault("work", {})[f"{name}_hi"] = max(vals_w) - m
            row.setdefault("work", {})[f"{name}_lo"] = m - min(vals_w)

            vals_s = [r.get("share", {}).get(name, 0) for r in reps]
            m = np.mean(vals_s)
            row.setdefault("share", {})[f"{name}_mean"] = m
            row.setdefault("share", {})[f"{name}_hi"] = max(vals_s) - m
            row.setdefault("share", {})[f"{name}_lo"] = m - min(vals_s)

        # phase-segmented miss rate
        if reps and len(reps[0].get("win_miss", [])) > 0:
            for pi, pname in enumerate(PHASE_NAMES):
                lo = PHASE_BOUNDS[pi]
                hi = PHASE_BOUNDS[pi + 1]
                phase_vals = []
                for r in reps:
                    wm = r.get("win_miss", [])
                    if len(wm) > lo:
                        seg = wm[lo:min(hi, len(wm))]
                        if len(seg) > 0:
                            phase_vals.append(np.mean(seg))
                if phase_vals:
                    row[f"miss_{pname}"] = np.mean(phase_vals)
                    row[f"miss_{pname}_hi"] = max(phase_vals) - np.mean(phase_vals)
                    row[f"miss_{pname}_lo"] = np.mean(phase_vals) - min(phase_vals)

        stats.append(row)
    return stats


# ------------------------------------------------------------------ print
def fmt_err(mean, hi, lo, wm=7, we=5):
    return f"{mean:>{wm}.1f} +{hi:>{we}.1f}/-{lo:>{we}.1f}"


def print_summary(stats):
    print("=" * 130)
    print("EXPERIMENT 4: DYNAMIC LOAD (light-heavy-light-heavy)")
    print("=" * 130)
    hdr = (f"{'mode':>8} {'α0':>4}  {'miss%':>20}  "
           f"{'miss_L1':>8} {'miss_H1':>8} {'miss_L2':>8} {'miss_H2':>8}  "
           f"{'sh_ai':>8}  {'ai_work':>8}  {'Jain':>6}")
    print(hdr)
    print("-" * 130)
    for s in stats:
        miss_s = fmt_err(s.get('miss_rate_mean', 0), s.get('miss_rate_hi', 0), s.get('miss_rate_lo', 0))
        l1 = s.get('miss_L1', 0)
        h1 = s.get('miss_H1', 0)
        l2 = s.get('miss_L2', 0)
        h2 = s.get('miss_H2', 0)
        ai_sh = s.get('share', {}).get('ai_mean', 0)
        ai_w = s.get('work', {}).get('ai_mean', 0)
        print(f"{s.get('mode',''):>8} {s.get('alpha0',0):>4}  {miss_s}  "
              f"{l1:>8.1f} {h1:>8.1f} {l2:>8.1f} {h2:>8.1f}  "
              f"{ai_sh:>8.1f}  {ai_w:>8.0f}  {s.get('jain_mean',0):>6.3f}")
    print("=" * 130)


# ------------------------------------------------------------------ plots
def add_phase_shading(ax, ymax=105):
    """在图上添加四段相位背景色 + 边界虚线。"""
    for pi in range(4):
        ax.axvspan(PHASE_BOUNDS[pi], PHASE_BOUNDS[pi + 1],
                   color=PHASE_COLORS[pi], alpha=0.4, zorder=0)
    # 相位边界虚线
    for b in PHASE_BOUNDS[1:-1]:
        ax.axvline(b, color="#333", ls="--", lw=1, alpha=0.6, zorder=1)
    # 相位标签
    for pi, pname in enumerate(PHASE_NAMES):
        mid = (PHASE_BOUNDS[pi] + PHASE_BOUNDS[pi + 1]) / 2
        ax.text(mid, ymax * 0.97, pname, ha="center", va="top",
                fontsize=10, color="#333", fontweight="bold")


def plot_alpha_traj(runs, outdir):
    """α 轨迹 + 相位背景。所有线统一到全局最小长度。"""
    fig, ax = plt.subplots(figsize=(14, 5))
    # 全局最小长度
    all_lens = []
    for mode in ["aimd0", "aimd50", "aimd100"]:
        reps = [r for r in runs if r["mode"] == mode and len(r.get("alpha_traj", [])) > 0]
        for r in reps:
            all_lens.append(len(r["alpha_traj"]))
    gmin = min(all_lens) if all_lens else 0

    for mode, color in [("aimd0", COLORS_MODE["aimd0"]),
                         ("aimd50", COLORS_MODE["aimd50"]),
                         ("aimd100", COLORS_MODE["aimd100"])]:
        reps = [r for r in runs if r["mode"] == mode and len(r.get("alpha_traj", [])) > 0]
        if not reps:
            continue
        aligned = np.array([r["alpha_traj"][:gmin] for r in reps])
        mean = aligned.mean(axis=0)
        x = np.arange(gmin)
        for r in reps:
            ax.plot(x, r["alpha_traj"][:gmin], "-", color=color, lw=0.4, alpha=0.3)
        ax.plot(x, mean, "-", color=color, lw=2.5,
                label=f"{mode} mean (n={len(reps)})")

    add_phase_shading(ax, 105)
    ax.set_xlabel("Window")
    ax.set_ylabel("α")
    ax.set_title("Exp 4: AIMD α Trajectory with Phase Boundaries")
    ax.legend(loc="upper right")
    ax.set_ylim(-2, 105)
    ax.set_xlim(0, gmin)
    fig.tight_layout()
    out = os.path.join(outdir, "exp4_alpha_traj.png")
    fig.savefig(out); print(f"[saved] {out}"); plt.close(fig)


def plot_miss_traj(runs, outdir):
    """逐窗口 miss rate + 相位背景。所有线统一到全局最小长度。"""
    all_modes = [("fixed0", COLORS_MODE["fixed0"]),
                 ("fixed25", COLORS_MODE["fixed25"]),
                 ("fixed50", COLORS_MODE["fixed50"]),
                 ("fixed75", COLORS_MODE["fixed75"]),
                 ("fixed100", COLORS_MODE["fixed100"]),
                 ("aimd0", COLORS_MODE["aimd0"]),
                 ("aimd50", COLORS_MODE["aimd50"]),
                 ("aimd100", COLORS_MODE["aimd100"])]
    # 全局最小长度
    all_lens = []
    for mode, _ in all_modes:
        reps = [r for r in runs if r["mode"] == mode and len(r.get("win_miss", [])) > 0]
        for r in reps:
            all_lens.append(len(r["win_miss"]))
    gmin = min(all_lens) if all_lens else 0

    fig, ax = plt.subplots(figsize=(14, 5))
    for mode, color in all_modes:
        reps = [r for r in runs if r["mode"] == mode and len(r.get("win_miss", [])) > 0]
        if not reps:
            continue
        aligned = np.array([r["win_miss"][:gmin] for r in reps])
        mean = aligned.mean(axis=0)
        x = np.arange(gmin)
        ax.plot(x, mean, "-", color=color, lw=1.5, label=mode)

    add_phase_shading(ax, 105)
    ax.set_xlabel("Window")
    ax.set_ylabel("ctrl Miss Rate (%)")
    ax.set_title("Exp 4: Per-window Miss Rate with Phase Boundaries")
    ax.legend(loc="upper right")
    ax.set_xlim(0, gmin)
    ax.set_ylim(-2, 105)
    fig.tight_layout()
    out = os.path.join(outdir, "exp4_miss_traj.png")
    fig.savefig(out); print(f"[saved] {out}"); plt.close(fig)


def plot_ai_throughput(runs, outdir):
    """逐窗口 ai run_delta + 相位背景。所有线统一到全局最小长度。"""
    all_modes = [("fixed0", COLORS_MODE["fixed0"]),
                 ("fixed25", COLORS_MODE["fixed25"]),
                 ("fixed50", COLORS_MODE["fixed50"]),
                 ("fixed75", COLORS_MODE["fixed75"]),
                 ("fixed100", COLORS_MODE["fixed100"]),
                 ("aimd0", COLORS_MODE["aimd0"]),
                 ("aimd50", COLORS_MODE["aimd50"]),
                 ("aimd100", COLORS_MODE["aimd100"])]
    # 全局最小长度
    all_lens = []
    for mode, _ in all_modes:
        reps = [r for r in runs if r["mode"] == mode and len(r.get("win_ai_rd", [])) > 0]
        for r in reps:
            all_lens.append(len(r["win_ai_rd"]))
    gmin = min(all_lens) if all_lens else 0

    fig, ax = plt.subplots(figsize=(14, 5))
    for mode, color in all_modes:
        reps = [r for r in runs if r["mode"] == mode and len(r.get("win_ai_rd", [])) > 0]
        if not reps:
            continue
        aligned = np.array([r["win_ai_rd"][:gmin] for r in reps])
        mean = aligned.mean(axis=0)
        x = np.arange(gmin)
        ax.plot(x, mean, "-", color=color, lw=1.5, label=mode)

    add_phase_shading(ax)
    ax.set_xlabel("Window")
    ax.set_ylabel("ai run_delta (ticks)")
    ax.set_title("Exp 4: ai Throughput per Window")
    ax.legend(loc="upper right")
    ax.set_xlim(0, gmin)
    fig.tight_layout()
    out = os.path.join(outdir, "exp4_ai_throughput.png")
    fig.savefig(out); print(f"[saved] {out}"); plt.close(fig)


def plot_phase_summary(stats, outdir):
    """四段相位的 miss rate 柱状图对比。"""
    modes = ["fixed0", "fixed25", "fixed50", "fixed75", "fixed100", "aimd0", "aimd50", "aimd100"]
    phases = PHASE_NAMES
    x = np.arange(len(phases))
    n = len(modes)
    width = 0.10  # 缩窄柱子，8 个柱子总宽 0.8，留间隙

    fig, ax = plt.subplots(figsize=(12, 5))
    for mi, mode in enumerate(modes):
        s = next((x for x in stats if x.get("mode") == mode), None)
        if not s:
            continue
        vals = [s.get(f"miss_{ph}", 0) for ph in phases]
        errs_lo = [s.get(f"miss_{ph}_lo", 0) for ph in phases]
        errs_hi = [s.get(f"miss_{ph}_hi", 0) for ph in phases]
        color = COLORS_MODE.get(mode, "#666")
        offset = (mi - (n - 1) / 2) * width
        ax.bar(x + offset, vals, width * 0.9,
               yerr=[errs_lo, errs_hi], capsize=2, color=color,
               edgecolor="black", linewidth=0.4, label=mode, zorder=3)

    ax.set_xticks(x)
    ax.set_xticklabels(phases)
    ax.set_ylabel("ctrl Miss Rate (%)")
    ax.set_title("Exp 4: Miss Rate by Phase")
    ax.legend()
    ax.set_ylim(0, 105)
    fig.tight_layout()
    out = os.path.join(outdir, "exp4_phase_summary.png")
    fig.savefig(out); print(f"[saved] {out}"); plt.close(fig)


def plot_work_vs_miss(stats, outdir):
    """throughput vs miss 散点。"""
    modes = ["fixed0", "fixed25", "fixed50", "fixed75", "fixed100", "aimd0", "aimd50", "aimd100"]
    fig, ax = plt.subplots(figsize=(8, 6))

    xs, ys = [], []
    for mode in modes:
        s = next((x for x in stats if x.get("mode") == mode), None)
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
                    fmt="o", color=color, ms=10, capsize=4,
                    markeredgecolor="black", markeredgewidth=0.8, label=mode)
        ax.annotate(mode, (x, y), fontsize=9, ha="left", va="bottom",
                    xytext=(8, 5), textcoords="offset points")

    ax.set_xlabel("ai Throughput (run_delta ticks)")
    ax.set_ylabel("ctrl Miss Rate (%)")
    ax.set_title("Exp 4: Throughput vs Miss Rate")
    if xs:
        x_span = max(xs) - min(xs) if max(xs) > min(xs) else max(xs) * 0.2
        ax.set_xlim(min(xs) - x_span * 0.15, max(xs) + x_span * 0.25)
    y_span = max(ys) - min(ys) if max(ys) > min(ys) else 10
    ax.set_ylim(max(-5, min(ys) - y_span * 0.15), min(105, max(ys) + y_span * 0.25))
    ax.legend()
    ax.grid(True, alpha=0.3)
    fig.tight_layout()
    out = os.path.join(outdir, "exp4_work_vs_miss.png")
    fig.savefig(out); print(f"[saved] {out}"); plt.close(fig)


# ------------------------------------------------------------------ main
def main():
    if len(sys.argv) < 2:
        print(f"Usage: {sys.argv[0]} <sexp4_dyn.csv>"); sys.exit(1)

    path = sys.argv[1]
    outdir = os.path.dirname(path) or "./logs/sched/dyn"
    os.makedirs(outdir, exist_ok=True)

    runs = parse(path)
    print(f"[parsed] {len(runs)} runs (warmup discarded)")

    # 动态计算相位边界：从实际数据推断最大窗口数
    global PHASE_BOUNDS
    max_win = 0
    for r in runs:
        for w in r.get("W", []):
            if w["win"] > max_win:
                max_win = w["win"]
    if max_win > 0:
        PHASE_BOUNDS = [0, max_win // 4, max_win // 2, max_win * 3 // 4, max_win]
        print(f"[phase] max_win={max_win}, bounds={PHASE_BOUNDS}")

    computed = [compute(r) for r in runs]
    stats = aggregate(computed)

    print()
    print_summary(stats)

    plot_alpha_traj(computed, outdir)
    plot_miss_traj(computed, outdir)
    plot_ai_throughput(computed, outdir)
    plot_phase_summary(stats, outdir)
    plot_work_vs_miss(stats, outdir)

    print("\nDone.")


if __name__ == "__main__":
    main()
