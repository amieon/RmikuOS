#!/usr/bin/env python3
"""
stat_exp3.py -- Exp 3: AIMD constant load, multi-config, multi-start.

Handles 4 configs (light/medlo/medium/heavy) x fixed50 + aimd0/50/100.
Medium gets dedicated deep-dive plots; others get summary.

Usage:
    python3 stat_exp3.py ./logs/sched/aimd/sexp3_aimd.csv
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

COLORS_CFG = {
    "light":   "#16a34a",
    "medlo":   "#2563eb",
    "medium":  "#d97706",
    "heavy":   "#dc2626",
}
COLORS_MODE = {
    "fixed0":   "#94a3b8",
    "fixed50":  "#64748b",
    "fixed100": "#475569",
    "aimd0":    "#0891b2",
    "aimd50":   "#2563eb",
    "aimd100":  "#dc2626",
}

# exp2 实测的 edge 参考值（不同配置的 miss 急剧上升点）
EDGE_REF = {
    "light":  55,
    "medlo":  45,
    "medium": 35,
    "heavy":  25,
}


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

            # warmup marker
            if line.startswith("# WARMUP"):
                if cur is not None and not in_warmup:
                    runs.append(cur)
                in_warmup = True
                cur = None
                continue

            # formal run marker
            m = re.search(r'# RUN config=(\w+) mode=(\w+)(?: alpha0=(\d+)| alpha=(\d+)) rep=(\d+)/\d+', line)
            if m:
                if cur is not None and not in_warmup:
                    runs.append(cur)
                in_warmup = False
                config = m.group(1)
                mode = m.group(2)
                alpha = int(m.group(3)) if m.group(3) else int(m.group(4))
                rep = int(m.group(5))
                mode_key = f"aimd{alpha}" if mode == "aimd" else f"fixed{alpha}"
                cur = {
                    "config": config, "mode": mode_key, "alpha0": alpha, "rep": rep,
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
                                   "run_delta": int(p[5]), "eff_tickets": int(p[6])})
                elif tag == "D" and len(p) >= 6:
                    cur["D"].append({"win": int(p[1]), "jobs_delta": int(p[3]),
                                     "miss_delta": int(p[4]), "late_delta": int(p[5])})
                elif tag == "A" and len(p) >= 5:
                    cur["A"].append({"win": int(p[1]), "before": int(p[2]),
                                     "after": int(p[3]), "action": p[4]})
                elif tag == "S" and len(p) >= 4:
                    cur["S"].append({"win": int(p[1]), "jain_q": int(p[3]),
                                     "max_slowdown_q": int(p[4])})
                elif tag == "J" and len(p) >= 12:
                    cur["J"].append({"name": p[2], "jobs": int(p[4]), "miss": int(p[5]),
                                     "late_sum": int(p[6]), "late_max": int(p[7]),
                                     "resp_sum": int(p[8]), "resp_sumsq": int(p[9]),
                                     "resp_min": int(p[10]), "resp_max": int(p[11])})
                elif tag == "K" and len(p) >= 5:
                    cur["K"].append({"name": p[2], "work": int(p[4])})
            except (IndexError, ValueError):
                continue

    if cur is not None and not in_warmup:
        runs.append(cur)
    return runs


# ------------------------------------------------------------------ compute
def compute(run):
    s = {"config": run["config"], "mode": run["mode"], "alpha0": run["alpha0"], "rep": run["rep"]}

    for j in run["J"]:
        if j["name"] == "ctrl":
            s["jobs"] = j["jobs"]
            s["miss"] = j["miss"]
            s["miss_rate"] = j["miss"] / j["jobs"] * 100.0 if j["jobs"] > 0 else 0.0
            s["avg_late"] = j["late_sum"] / j["miss"] if j["miss"] > 0 else 0.0
            s["max_late"] = j["late_max"]
            n = j["jobs"]
            if n > 0:
                mean = j["resp_sum"] / n
                var = j["resp_sumsq"] / n - mean * mean
                s["resp_std"] = np.sqrt(max(var, 0))
            else:
                s["resp_std"] = 0.0
            break

    ws = [w for w in run["W"] if w["win"] > 3]
    for name in ["ctrl", "ai", "log"]:
        s.setdefault("work", {})[name] = sum(w["run_delta"] for w in ws if w["name"] == name)

    total_all = sum(w["run_delta"] for w in ws)
    for name in ["ctrl", "ai", "log"]:
        total_name = sum(w["run_delta"] for w in ws if w["name"] == name)
        s.setdefault("share", {})[name] = total_name / total_all * 100 if total_all > 0 else 0

    if run["D"]:
        rates = []
        for d in run["D"]:
            if d["jobs_delta"] > 0:
                rates.append(d["miss_delta"] / d["jobs_delta"] * 100.0)
            else:
                rates.append(0.0)
        s["win_miss"] = np.array(rates)
    else:
        s["win_miss"] = np.array([])

    if run["A"]:
        s["alpha_traj"] = np.array([a["after"] for a in run["A"]])
        s["alpha_win"] = np.array([a["win"] for a in run["A"]])
        actions = {}
        for a in run["A"]:
            actions[a["action"]] = actions.get(a["action"], 0) + 1
        s["actions"] = actions
    else:
        s["alpha_traj"] = np.array([])

    if run["S"]:
        s["jain"] = np.mean([x["jain_q"] for x in run["S"]]) / 1000.0
    else:
        s["jain"] = 0

    return s


def aggregate(runs, group_keys):
    from collections import defaultdict
    by_group = defaultdict(list)
    for r in runs:
        key = tuple(r[k] for k in group_keys)
        by_group[key].append(r)

    stats = []
    for key, reps in sorted(by_group.items()):
        row = dict(zip(group_keys, key))
        row["nreps"] = len(reps)

        for field in ["miss_rate", "avg_late", "max_late", "resp_std", "jain"]:
            vals = [r.get(field, 0) for r in reps]
            m = np.mean(vals)
            row[f"{field}_mean"] = m
            row[f"{field}_hi"] = max(vals) - m
            row[f"{field}_lo"] = m - min(vals)

        for name in ["ctrl", "ai", "log"]:
            vals = [r.get("work", {}).get(name, 0) for r in reps]
            m = np.mean(vals)
            row.setdefault("work", {})[f"{name}_mean"] = m
            row.setdefault("work", {})[f"{name}_hi"] = max(vals) - m
            row.setdefault("work", {})[f"{name}_lo"] = m - min(vals)

            vals_s = [r.get("share", {}).get(name, 0) for r in reps]
            m = np.mean(vals_s)
            row.setdefault("share", {})[f"{name}_mean"] = m
            row.setdefault("share", {})[f"{name}_hi"] = max(vals_s) - m
            row.setdefault("share", {})[f"{name}_lo"] = m - min(vals_s)

        if "mode" in row and str(row.get("mode", "")).startswith("aimd"):
            traj_list = [r["alpha_traj"] for r in reps if len(r.get("alpha_traj", [])) > 0]
            if traj_list:
                min_len = min(len(t) for t in traj_list)
                aligned = np.array([t[:min_len] for t in traj_list])
                row["alpha_mean"] = aligned.mean(axis=0)
                row["alpha_hi"] = aligned.max(axis=0) - aligned.mean(axis=0)
                row["alpha_lo"] = aligned.mean(axis=0) - aligned.min(axis=0)
                # 稳态 α：后半段平均
                half = min_len // 2
                steady = aligned[:, half:].mean()
                row["alpha_steady"] = steady
                row["alpha_steady_hi"] = aligned[:, half:].max() - steady
                row["alpha_steady_lo"] = steady - aligned[:, half:].min()
            else:
                row["alpha_steady"] = 0
                row["alpha_steady_hi"] = 0
                row["alpha_steady_lo"] = 0
        else:
            row["alpha_steady"] = row.get("alpha0", 0)
            row["alpha_steady_hi"] = 0
            row["alpha_steady_lo"] = 0

        # AIMD actions 汇总
        all_actions = {}
        for r in reps:
            for a, c in r.get("actions", {}).items():
                all_actions[a] = all_actions.get(a, 0) + c
        row["actions"] = all_actions

        stats.append(row)
    return stats


# ------------------------------------------------------------------ print
def fmt_err(mean, hi, lo, wm=7, we=5):
    return f"{mean:>{wm}.1f} +{hi:>{we}.1f}/-{lo:>{we}.1f}"


def print_summary(stats):
    print("=" * 165)
    print("EXPERIMENT 3: AIMD CONSTANT LOAD (multi-config, multi-start)")
    print("=" * 165)
    hdr = (f"{'config':>8} {'mode':>8} {'α0':>4}  {'miss%':>20}  "
           f"{'α_steady':>14}  {'sh_ctrl':>20}  {'sh_ai':>20}  "
           f"{'ai_work':>8}  {'ctrl_work':>9}  {'avg_late':>8}  {'max_late':>8}  {'Jain':>6}  {'actions':>20}")
    print(hdr)
    print("-" * 165)
    for s in stats:
        miss_s = fmt_err(s.get('miss_rate_mean',0), s.get('miss_rate_hi',0), s.get('miss_rate_lo',0))
        alpha_s = fmt_err(s.get('alpha_steady',0), s.get('alpha_steady_hi',0), s.get('alpha_steady_lo',0), wm=5, we=3)
        ctrl_s = fmt_err(s.get('share',{}).get('ctrl_mean',0), s.get('share',{}).get('ctrl_hi',0), s.get('share',{}).get('ctrl_lo',0))
        ai_s = fmt_err(s.get('share',{}).get('ai_mean',0), s.get('share',{}).get('ai_hi',0), s.get('share',{}).get('ai_lo',0))
        ai_w = s.get('work',{}).get('ai_mean',0)
        ctrl_w = s.get('work',{}).get('ctrl_mean',0)
        avg_l = s.get('avg_late_mean',0)
        max_l = s.get('max_late_mean',0)
        acts = s.get('actions', {})
        act_str = "/".join(f"{k}:{v}" for k, v in sorted(acts.items())) if acts else "-"
        print(f"{s.get('config',''):>8} {s.get('mode',''):>8} {s.get('alpha0',0):>4}  "
              f"{miss_s}  {alpha_s}  {ctrl_s}  {ai_s}  "
              f"{ai_w:>8.0f}  {ctrl_w:>9.0f}  {avg_l:>8.1f}  {max_l:>8.0f}  "
              f"{s.get('jain_mean',0):>6.3f}  {act_str:>20}")
    print("=" * 165)


# ------------------------------------------------------------------ plots
def plot_miss_all(stats, outdir):
    """Fig 1: miss rate comparison across 4 configs."""
    configs = ["light", "medlo", "medium", "heavy"]
    modes = ["fixed0", "fixed50", "fixed100", "aimd0", "aimd50", "aimd100"]

    fig, axes = plt.subplots(2, 2, figsize=(12, 10))
    axes = axes.flatten()

    for idx, cfg in enumerate(configs):
        ax = axes[idx]
        for mode in modes:
            pts = [s for s in stats if s.get("config") == cfg and s.get("mode") == mode]
            if not pts:
                continue
            color = COLORS_MODE.get(mode, "#666")
            val = pts[0].get("miss_rate_mean", 0)
            hi = pts[0].get("miss_rate_hi", 0)
            lo = pts[0].get("miss_rate_lo", 0)
            ax.bar(mode, val, yerr=[[lo], [hi]], capsize=4, color=color,
                   edgecolor="black", linewidth=0.8, width=0.6)
        ax.set_title(f"{cfg}")
        ax.set_ylabel("ctrl Miss Rate (%)")
        ax.set_ylim(0, 105)
        ax.grid(True, alpha=0.3, axis="y")

    fig.suptitle("Exp 3: Miss Rate by Config & Mode", fontsize=14)
    fig.tight_layout(rect=[0, 0, 1, 0.96])
    out = os.path.join(outdir, "exp3_miss_all.png")
    fig.savefig(out); print(f"[saved] {out}"); plt.close(fig)


def plot_alpha_traj_all(runs, outdir):
    """Fig 2: α trajectory for all configs, 3 AIMD starts."""
    configs = ["light", "medlo", "medium", "heavy"]
    fig, axes = plt.subplots(2, 2, figsize=(14, 10))
    axes = axes.flatten()

    for idx, cfg in enumerate(configs):
        ax = axes[idx]
        for mode, color in [("aimd0", COLORS_MODE["aimd0"]),
                             ("aimd50", COLORS_MODE["aimd50"]),
                             ("aimd100", COLORS_MODE["aimd100"])]:
            reps = [r for r in runs if r["config"] == cfg and r["mode"] == mode and len(r.get("alpha_traj",[])) > 0]
            if not reps:
                continue
            min_len = min(len(r["alpha_traj"]) for r in reps)
            aligned = np.array([r["alpha_traj"][:min_len] for r in reps])
            mean = aligned.mean(axis=0)
            lo = aligned.mean(axis=0) - aligned.min(axis=0)
            hi = aligned.max(axis=0) - aligned.mean(axis=0)
            x = np.arange(len(mean))
            ax.plot(x, mean, "-", color=color, lw=1.5, label=f"α0={mode.replace('aimd','')}")
            ax.fill_between(x, mean - lo, mean + hi, alpha=0.2, color=color)
        edge = EDGE_REF.get(cfg, 40)
        ax.axhline(edge, color="gray", ls="--", lw=1, label=f"exp2 edge≈{edge}")
        ax.set_title(f"{cfg}")
        ax.set_xlabel("Window")
        ax.set_ylabel("α")
        ax.legend(fontsize=8)
        ax.set_ylim(-2, 105)

    fig.suptitle("Exp 3: α Trajectory (3 starts, all configs)", fontsize=14)
    fig.tight_layout(rect=[0, 0, 1, 0.96])
    out = os.path.join(outdir, "exp3_alpha_traj_all.png")
    fig.savefig(out); print(f"[saved] {out}"); plt.close(fig)


def plot_miss_traj_medium(runs, outdir):
    """Fig 3: medium per-window miss rate."""
    fig, ax = plt.subplots(figsize=(12, 4.5))
    for mode, color in [("fixed0", COLORS_MODE["fixed0"]),
                         ("fixed50", COLORS_MODE["fixed50"]),
                         ("fixed100", COLORS_MODE["fixed100"]),
                         ("aimd0", COLORS_MODE["aimd0"]),
                         ("aimd50", COLORS_MODE["aimd50"]),
                         ("aimd100", COLORS_MODE["aimd100"])]:
        reps = [r for r in runs if r["config"] == "medium" and r["mode"] == mode and len(r.get("win_miss",[])) > 0]
        if not reps:
            continue
        min_len = min(len(r["win_miss"]) for r in reps)
        aligned = np.array([r["win_miss"][:min_len] for r in reps])
        mean = aligned.mean(axis=0)
        lo = aligned.mean(axis=0) - aligned.min(axis=0)
        hi = aligned.max(axis=0) - aligned.mean(axis=0)
        x = np.arange(len(mean))
        ax.plot(x, mean, "-", color=color, lw=1.0, label=mode)
        ax.fill_between(x, mean - lo, mean + hi, alpha=0.15, color=color)
    ax.set_xlabel("Window")
    ax.set_ylabel("ctrl Miss Rate (%)")
    ax.set_title("Exp 3: Medium Per-window Miss Rate")
    ax.legend(); ax.set_ylim(-2, 105)
    fig.tight_layout()
    out = os.path.join(outdir, "exp3_miss_traj_medium.png")
    fig.savefig(out); print(f"[saved] {out}"); plt.close(fig)


def plot_comparison_medium(stats, outdir):
    """Fig 4: medium fixed vs AIMD bar comparison."""
    fig, axes = plt.subplots(1, 2, figsize=(10, 4))
    modes = ["fixed0", "fixed50", "fixed100", "aimd0", "aimd50", "aimd100"]
    colors = [COLORS_MODE[m] for m in modes]

    ax = axes[0]
    vals, los, his = [], [], []
    for m in modes:
        s = next((x for x in stats if x.get("config") == "medium" and x.get("mode") == m), None)
        vals.append(s.get("miss_rate_mean", 0) if s else 0)
        his.append(s.get("miss_rate_hi", 0) if s else 0)
        los.append(s.get("miss_rate_lo", 0) if s else 0)
    bars = ax.bar(range(len(modes)), vals, yerr=[los, his], capsize=4, color=colors,
                  edgecolor="black", linewidth=0.8, width=0.5)
    for bar, v in zip(bars, vals):
        ax.text(bar.get_x() + bar.get_width() / 2, v + 1.5,
                f"{v:.1f}%", ha="center", fontsize=10, fontweight="bold")
    ax.set_xticks(range(len(modes)))
    ax.set_xticklabels(modes)
    ax.set_ylabel("ctrl Miss Rate (%)")
    ax.set_title("Medium: Miss Rate")

    ax = axes[1]
    vals, los, his = [], [], []
    for m in modes:
        s = next((x for x in stats if x.get("config") == "medium" and x.get("mode") == m), None)
        vals.append(s.get("share", {}).get("ai_mean", 0) if s else 0)
        his.append(s.get("share", {}).get("ai_hi", 0) if s else 0)
        los.append(s.get("share", {}).get("ai_lo", 0) if s else 0)
    bars = ax.bar(range(len(modes)), vals, yerr=[los, his], capsize=4, color=colors,
                  edgecolor="black", linewidth=0.8, width=0.5)
    for bar, v in zip(bars, vals):
        ax.text(bar.get_x() + bar.get_width() / 2, v + max(vals) * 0.02,
                f"{v:.0f}%", ha="center", fontsize=10, fontweight="bold")
    ax.set_xticks(range(len(modes)))
    ax.set_xticklabels(modes)
    ax.set_ylabel("ai CPU Share (%)")
    ax.set_title("Medium: ai Share")

    fig.suptitle("Exp 3: Medium Fixed vs AIMD", fontsize=13)
    fig.tight_layout(rect=[0, 0, 1, 0.96])
    out = os.path.join(outdir, "exp3_comparison_medium.png")
    fig.savefig(out); print(f"[saved] {out}"); plt.close(fig)


def plot_convergence_medium(runs, outdir):
    """Fig 5: medium 3-start convergence overlay."""
    fig, ax = plt.subplots(figsize=(12, 4.5))
    for mode, color in [("aimd0", COLORS_MODE["aimd0"]),
                         ("aimd50", COLORS_MODE["aimd50"]),
                         ("aimd100", COLORS_MODE["aimd100"])]:
        reps = [r for r in runs if r["config"] == "medium" and r["mode"] == mode and len(r.get("alpha_traj",[])) > 0]
        if not reps:
            continue
        for r in reps:
            alpha = r["alpha_traj"]
            x = np.arange(len(alpha))
            ax.plot(x, alpha, "-", color=color, lw=0.5, alpha=0.35)
        min_len = min(len(r["alpha_traj"]) for r in reps)
        aligned = np.array([r["alpha_traj"][:min_len] for r in reps])
        mean = aligned.mean(axis=0)
        ax.plot(np.arange(len(mean)), mean, "-", color=color, lw=2.5,
                label=f"α0={mode.replace('aimd','')} mean (n={len(reps)})")
    ax.axhline(EDGE_REF.get("medium", 35), color="gray", ls="--", lw=1.2,
               label=f"exp2 edge≈{EDGE_REF.get('medium',35)}")
    ax.set_xlabel("Window", fontsize=12)
    ax.set_ylabel("α", fontsize=12)
    ax.set_title("Exp 3: Medium Convergence from 3 Starts", fontsize=13)
    ax.legend(loc="upper right", fontsize=10)
    ax.set_ylim(-2, 105)
    fig.tight_layout()
    out = os.path.join(outdir, "exp3_convergence_medium.png")
    fig.savefig(out); print(f"[saved] {out}"); plt.close(fig)


def plot_summary_config(runs, config, outdir):
    """One summary figure per non-medium config."""
    fig, axes = plt.subplots(2, 2, figsize=(11, 9))
    modes = ["fixed0", "fixed50", "fixed100", "aimd0", "aimd50", "aimd100"]

    # miss rate bars
    ax = axes[0, 0]
    vals, los, his = [], [], []
    for m in modes:
        reps = [r for r in runs if r["config"] == config and r["mode"] == m]
        if reps:
            v = [x.get("miss_rate", 0) for x in reps]
            vals.append(np.mean(v)); his.append(max(v)-np.mean(v)); los.append(np.mean(v)-min(v))
        else:
            vals.append(0); his.append(0); los.append(0)
    colors = [COLORS_MODE[m] for m in modes]
    ax.bar(range(len(modes)), vals, yerr=[los, his], capsize=4, color=colors,
           edgecolor="black", linewidth=0.8, width=0.5)
    ax.set_xticks(range(len(modes))); ax.set_xticklabels(modes, fontsize=9)
    ax.set_ylabel("miss%"); ax.set_title("Miss Rate")

    # ai share
    ax = axes[0, 1]
    vals, los, his = [], [], []
    for m in modes:
        reps = [r for r in runs if r["config"] == config and r["mode"] == m]
        if reps:
            v = [x.get("share", {}).get("ai", 0) for x in reps]
            vals.append(np.mean(v)); his.append(max(v)-np.mean(v)); los.append(np.mean(v)-min(v))
        else:
            vals.append(0); his.append(0); los.append(0)
    ax.bar(range(len(modes)), vals, yerr=[los, his], capsize=4, color=colors,
           edgecolor="black", linewidth=0.8, width=0.5)
    ax.set_xticks(range(len(modes))); ax.set_xticklabels(modes, fontsize=9)
    ax.set_ylabel("ai share%"); ax.set_title("ai Share")

    # alpha trajectory
    ax = axes[1, 0]
    for m in ["aimd0", "aimd50", "aimd100"]:
        reps = [r for r in runs if r["config"] == config and r["mode"] == m and len(r.get("alpha_traj",[])) > 0]
        if not reps:
            continue
        min_len = min(len(r["alpha_traj"]) for r in reps)
        aligned = np.array([r["alpha_traj"][:min_len] for r in reps])
        mean = aligned.mean(axis=0)
        ax.plot(np.arange(len(mean)), mean, "-", color=COLORS_MODE[m], lw=1.5,
                label=m.replace("aimd", "α0="))
    ax.axhline(EDGE_REF.get(config, 40), color="gray", ls="--", lw=1,
               label=f"edge≈{EDGE_REF.get(config,40)}")
    ax.set_xlabel("Window"); ax.set_ylabel("α")
    ax.set_title("α Trajectory"); ax.legend(fontsize=9)

    # actions pie
    ax = axes[1, 1]
    reps = [r for r in runs if r["config"] == config and r["mode"] == "aimd50"]
    actions = {}
    for r in reps:
        for a, c in r.get("actions", {}).items():
            actions[a] = actions.get(a, 0) + c
    if actions:
        labels = list(actions.keys())
        sizes = list(actions.values())
        ax.pie(sizes, labels=labels, autopct="%1.1f%%", startangle=90)
    ax.set_title("AIMD50 Actions")

    fig.suptitle(f"Exp 3: {config} Summary", fontsize=13)
    fig.tight_layout(rect=[0, 0, 1, 0.96])
    out = os.path.join(outdir, f"exp3_summary_{config}.png")
    fig.savefig(out); print(f"[saved] {out}"); plt.close(fig)


def plot_throughput_vs_miss(stats, outdir):
    """每个配置两张图：横轴 ai work / ai share，纵轴 ctrl miss%。6 个 mode 各一个点。
    坐标轴自适应数据范围，留 10% 边距。"""
    configs = ["light", "medlo", "medium", "heavy"]
    modes = ["fixed0", "fixed50", "fixed100", "aimd0", "aimd50", "aimd100"]

    for metric, xlabel, fname in [
        ("work",  "ai Throughput (run_delta ticks)", "exp3_work_vs_miss.png"),
        ("share", "ai CPU Share (%)",                "exp3_share_vs_miss.png"),
    ]:
        fig, axes = plt.subplots(2, 2, figsize=(12, 10))
        axes = axes.flatten()

        for idx, cfg in enumerate(configs):
            ax = axes[idx]
            xs, ys = [], []
            for mode in modes:
                pts = [s for s in stats if s.get("config") == cfg and s.get("mode") == mode]
                if not pts:
                    continue
                s = pts[0]
                d = s.get(metric, {})
                x = d.get("ai_mean", 0)
                y = s.get("miss_rate_mean", 0)
                x_hi = d.get("ai_hi", 0)
                x_lo = d.get("ai_lo", 0)
                y_hi = s.get("miss_rate_hi", 0)
                y_lo = s.get("miss_rate_lo", 0)
                xs.append(x); ys.append(y)
                color = COLORS_MODE.get(mode, "#666")
                ax.errorbar(x, y, xerr=[[x_lo], [x_hi]], yerr=[[y_lo], [y_hi]],
                            fmt="o", color=color, ms=8, capsize=3,
                            markeredgecolor="black", markeredgewidth=0.8, zorder=5)
                ax.annotate(mode, (x, y), fontsize=8, ha="left", va="bottom",
                            xytext=(6, 4), textcoords="offset points")

            ax.set_title(f"{cfg}")
            ax.set_xlabel(xlabel)
            ax.set_ylabel("ctrl Miss Rate (%)")

            # 自适应坐标轴：留 10% 边距
            if xs:
                x_min, x_max = min(xs), max(xs)
                x_span = x_max - x_min if x_max > x_min else x_max * 0.2
                ax.set_xlim(x_min - x_span * 0.15, x_max + x_span * 0.25)
            y_min, y_max = min(ys) if ys else 0, max(ys) if ys else 100
            y_span = y_max - y_min if y_max > y_min else 10
            ax.set_ylim(max(-5, y_min - y_span * 0.15), min(105, y_max + y_span * 0.25))
            ax.grid(True, alpha=0.3)

        fig.suptitle(f"Exp 3: {metric.capitalize()} vs Miss Rate (per config)", fontsize=14)
        fig.tight_layout(rect=[0, 0, 1, 0.96])
        out = os.path.join(outdir, fname)
        fig.savefig(out); print(f"[saved] {out}"); plt.close(fig)


# ------------------------------------------------------------------ main
def main():
    if len(sys.argv) < 2:
        print(f"Usage: {sys.argv[0]} <sexp3_aimd.csv>"); sys.exit(1)

    path = sys.argv[1]
    outdir = os.path.dirname(path) or "./logs/sched/aimd"
    os.makedirs(outdir, exist_ok=True)

    runs = parse(path)
    print(f"[parsed] {len(runs)} runs (warmup discarded)")

    computed = [compute(r) for r in runs]
    stats = aggregate(computed, ["config", "mode", "alpha0"])

    print()
    print_summary(stats)

    # global
    plot_miss_all(stats, outdir)

    # all configs alpha trajectory
    plot_alpha_traj_all(computed, outdir)

    # medium deep dive
    plot_miss_traj_medium(computed, outdir)
    plot_comparison_medium(stats, outdir)
    plot_convergence_medium(computed, outdir)

    # other configs summary
    for cfg in ["light", "medlo", "medium", "heavy"]:
        plot_summary_config(computed, cfg, outdir)

    # throughput vs miss scatter (per config)
    plot_throughput_vs_miss(stats, outdir)

    print("\nDone.")


if __name__ == "__main__":
    main()
