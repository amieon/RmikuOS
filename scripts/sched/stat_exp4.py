#!/usr/bin/env python3
"""
stat_exp4_v2.py -- Exp 4: Dynamic load, x-axis = REAL TICKS.
Fix: phase background & x-axis now use ticks (from S-row tick field),
     so heavy-phase starvation no longer desyncs the plot.
Usage:
    python3 stat_exp4_v2.py ./logs/sched/dyn/sexp4_dyn_v2.csv
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

COLORS = {
    "fixed0":   "#94a3b8",
    "fixed30":  "#64748b",
    "fixed60":  "#475569",
    "fixed100": "#1e293b",
    "aimd15":   "#0891b2",
    "aimd50":   "#2563eb",
    "aimd100":  "#dc2626",
}


# ------------------------------------------------------------------ parse
def parse(path):
    runs = []
    cur = None
    skip = False
    with open(path) as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            if line.startswith("# WARMUP"):
                if cur is not None and not skip:
                    runs.append(cur)
                skip = True
                cur = None
                continue
            m = re.search(r'# RUN config=(\w+) mode=(\w+)(?: alpha=(\d+)| alpha0=(\d+)) rep=(\d+)/\d+', line)
            if m:
                if cur is not None and not skip:
                    runs.append(cur)
                skip = False
                config = m.group(1); mode = m.group(2)
                alpha = int(m.group(3)) if m.group(3) else int(m.group(4))
                rep = int(m.group(5))
                mode_key = f"fixed{alpha}" if mode == "fixed" else f"aimd{alpha}"
                cur = {"config": config, "mode": mode_key, "alpha0": alpha, "rep": rep,
                       "t0": None, "t_end": None,
                       "W": [], "D": [], "A": [], "S": [], "J": []}
                continue
            if skip or cur is None:
                continue
            if line.startswith("# T0 "):
                parts = line.split()
                if len(parts) >= 3:
                    try:
                        cur["t0"] = int(parts[1]); cur["t_end"] = int(parts[2])
                    except ValueError:
                        pass
                continue
            p = line.split(",")
            tag = p[0]
            try:
                if tag == "W" and len(p) >= 8:
                    cur["W"].append({"win": int(p[1]), "name": p[4], "run_delta": int(p[5])})
                elif tag == "D" and len(p) >= 6:
                    cur["D"].append({"win": int(p[1]), "jobs_delta": int(p[3]),
                                     "miss_delta": int(p[4]), "late_delta": int(p[5])})
                elif tag == "A" and len(p) >= 5:
                    cur["A"].append({"win": int(p[1]), "before": int(p[2]),
                                     "after": int(p[3]), "action": p[4]})
                elif tag == "S":
                    ent = {"win": int(p[1]), "alpha": int(p[2]), "jain_q": int(p[3]), "tick": None}
                    if len(p) >= 6:
                        ent["tick"] = int(p[5])
                    cur["S"].append(ent)
                elif tag == "J" and len(p) >= 12:
                    cur["J"].append({"name": p[2], "jobs": int(p[4]), "miss": int(p[5]),
                                     "late_sum": int(p[6]), "late_max": int(p[7])})
            except (IndexError, ValueError):
                continue
    if cur is not None and not skip:
        runs.append(cur)
    return runs


# ------------------------------------------------------------------ phase by ticks
def phase_of_trel(trel, span):
    """Phase by relative tick (trel = tick - t0, span = t_end - t0). Matches C sl_phase_now."""
    if span <= 0:
        return 0
    seg = span / 3.0
    ph = int(trel // seg) if seg > 0 else 0
    if ph < 0: ph = 0
    if ph > 2: ph = 2
    return ph


# ------------------------------------------------------------------ compute
def compute(run):
    s = {"mode": run["mode"], "alpha0": run["alpha0"], "rep": run["rep"]}

    max_win = 0
    for d in run["D"]:
        max_win = max(max_win, d["win"])
    for w in run["W"]:
        max_win = max(max_win, w["win"])
    for a in run["A"]:
        max_win = max(max_win, a["win"])
    total_wins = max_win if max_win > 0 else 720

    tick_by_win = {}
    for ent in run["S"]:
        if ent.get("tick") is not None:
            tick_by_win[ent["win"]] = ent["tick"]
    have_tick = (run.get("t0") is not None and run.get("t_end") is not None
                 and run["t_end"] > run.get("t0", 0))
    t0 = run.get("t0") or 0
    t_end = run.get("t_end") or 0
    span = (t_end - t0) if have_tick else (total_wins * 100)

    miss_by_win = {}; jobs_by_win = {}; ai_rd_by_win = {}; alpha_by_win = {}
    for d in run["D"]:
        miss_by_win[d["win"]] = d["miss_delta"]
        jobs_by_win[d["win"]] = d["jobs_delta"]
    for w in run["W"]:
        if w["name"] == "ai":
            ai_rd_by_win[w["win"]] = w["run_delta"]
    for a in run["A"]:
        alpha_by_win[a["win"]] = a["after"]
    if not alpha_by_win and run["D"]:
        for d in run["D"]:
            alpha_by_win[d["win"]] = run["alpha0"]

    miss_rates = []; alphas = []; ai_rds = []; trels = []
    phase_stats = {0: {"miss": 0, "jobs": 0, "ai_rd": 0, "count": 0},
                   1: {"miss": 0, "jobs": 0, "ai_rd": 0, "count": 0},
                   2: {"miss": 0, "jobs": 0, "ai_rd": 0, "count": 0}}
    for w in range(1, total_wins + 1):
        if have_tick and w in tick_by_win:
            trel = float(tick_by_win[w] - t0)
        else:
            trel = float(w * 100)
        trels.append(trel)
        ph = phase_of_trel(trel, span)
        jobs = jobs_by_win.get(w, 0)
        miss = miss_by_win.get(w, 0)
        mr = (miss / jobs * 100.0) if jobs > 0 else 0.0
        miss_rates.append(mr)
        phase_stats[ph]["miss"] += miss
        phase_stats[ph]["jobs"] += jobs
        phase_stats[ph]["count"] += 1
        ai_rd = ai_rd_by_win.get(w, 0)
        ai_rds.append(ai_rd)
        phase_stats[ph]["ai_rd"] += ai_rd
        alphas.append(alpha_by_win.get(w, run["alpha0"]))

    s["total_wins"] = total_wins
    s["t0"] = t0; s["t_end"] = t_end; s["span"] = span
    s["miss_rates"] = np.array(miss_rates)
    s["alphas"] = np.array(alphas)
    s["ai_rds"] = np.array(ai_rds)
    s["trels"] = np.array(trels)

    for ph in [0, 1, 2]:
        st = phase_stats[ph]
        s[f"ph{ph}_miss_rate"] = (st["miss"] / st["jobs"] * 100.0) if st["jobs"] > 0 else 0.0
        s[f"ph{ph}_ai_rd"] = st["ai_rd"] / st["count"] if st["count"] > 0 else 0.0

    for j in run["J"]:
        if j["name"] == "ctrl":
            s["jobs"] = j["jobs"]
            s["miss"] = j["miss"]
            s["miss_rate"] = j["miss"] / j["jobs"] * 100.0 if j["jobs"] > 0 else 0.0
            s["avg_late"] = j["late_sum"] / j["miss"] if j["miss"] > 0 else 0.0
            s["max_late"] = j["late_max"]

    if run["S"]:
        s["jain"] = np.mean([x["jain_q"] for x in run["S"]]) / 1000.0

    return s


# ------------------------------------------------------------------ aggregate
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
        row["span"] = reps[0].get("span", 0) if reps else 0

        for field in ["miss_rate", "avg_late", "max_late", "jain"]:
            vals = [r.get(field, 0) for r in reps if field in r]
            if vals:
                row[f"{field}_mean"] = np.mean(vals)
                row[f"{field}_std"] = np.std(vals)

        for ph in [0, 1, 2]:
            for metric in ["miss_rate", "ai_rd"]:
                kn = f"ph{ph}_{metric}"
                vals = [r.get(kn, 0) for r in reps]
                if vals:
                    row[f"{kn}_mean"] = np.mean(vals)
                    row[f"{kn}_std"] = np.std(vals)

        for traj in ["miss_rates", "alphas", "ai_rds", "trels"]:
            traj_list = [r[traj] for r in reps if traj in r]
            if traj_list:
                min_len = min(len(t) for t in traj_list)
                aligned = np.array([t[:min_len] for t in traj_list])
                row[f"{traj}_mean"] = aligned.mean(axis=0)
                row[f"{traj}_std"] = aligned.std(axis=0)

        stats.append(row)
    return stats


# ------------------------------------------------------------------ print
def print_summary(stats):
    print("=" * 120)
    print("EXPERIMENT 4: DYNAMIC LOAD (x = real ticks) — Fixed vs AIMD")
    print("=" * 120)
    print(f"{'mode':>10} {'α/α0':>4} {'miss%':>7} {'±':>5} {'P0_miss':>8} {'P1_miss':>8} {'P2_miss':>8} {'Jain':>6}")
    print("-" * 120)
    for s in stats:
        print(f"{s.get('mode',''):>10} {s.get('alpha0',0):>4} "
              f"{s.get('miss_rate_mean',0):>7.2f} {s.get('miss_rate_std',0):>5.2f} "
              f"{s.get('ph0_miss_rate_mean',0):>8.2f} "
              f"{s.get('ph1_miss_rate_mean',0):>8.2f} "
              f"{s.get('ph2_miss_rate_mean',0):>8.2f} "
              f"{s.get('jain_mean',0):>6.3f}")
    print("=" * 120)


# ------------------------------------------------------------------ plots
def add_phase_background(ax, span):
    seg = span / 3.0
    ax.axvspan(0, seg, facecolor="#dbeafe", alpha=0.3, zorder=0)
    ax.axvspan(seg, 2 * seg, facecolor="#fee2e2", alpha=0.3, zorder=0)
    ax.axvspan(2 * seg, span, facecolor="#dbeafe", alpha=0.3, zorder=0)
    ax.axvline(seg, color="gray", ls="--", lw=0.8, alpha=0.5)
    ax.axvline(2 * seg, color="gray", ls="--", lw=0.8, alpha=0.5)


def plot_miss_all(stats, outdir):
    fig, ax = plt.subplots(figsize=(10, 5))
    modes = ["fixed0", "fixed30", "fixed60", "fixed100", "aimd15", "aimd50", "aimd100"]
    colors = [COLORS.get(m, "#666") for m in modes]
    vals, errs = [], []
    for m in modes:
        s = next((x for x in stats if x.get("mode") == m), None)
        vals.append(s.get("miss_rate_mean", 0) if s else 0)
        errs.append(s.get("miss_rate_std", 0) if s else 0)
    bars = ax.bar(range(len(modes)), vals, yerr=errs, capsize=4, color=colors,
                  edgecolor="black", linewidth=0.8, width=0.5)
    for bar, v in zip(bars, vals):
        ax.text(bar.get_x() + bar.get_width() / 2, v + max(vals) * 0.02 if max(vals) > 0 else 1,
                f"{v:.1f}%", ha="center", fontsize=9, fontweight="bold")
    ax.set_xticks(range(len(modes)))
    ax.set_xticklabels(modes, rotation=30, ha="right", fontsize=9)
    ax.set_ylabel("ctrl Miss Rate (%)")
    ax.set_title("Exp 4: Overall Miss Rate (Fixed 0/30/60/100 + AIMD 15/50/100)")
    ax.set_ylim(0, 105)
    fig.tight_layout()
    out = os.path.join(outdir, "exp4v2_miss_all.png")
    fig.savefig(out)
    print(f"[saved] {out}")
    plt.close(fig)


def plot_alpha_traj(runs, outdir):
    fig, ax = plt.subplots(figsize=(14, 4.5))
    span = runs[0]["span"] if runs else 72000
    add_phase_background(ax, span)
    for mode, color in [("aimd15", COLORS["aimd15"]),
                        ("aimd50", COLORS["aimd50"]),
                        ("aimd100", COLORS["aimd100"])]:
        reps = [r for r in runs if r["mode"] == mode and "alphas" in r and "trels" in r]
        if not reps:
            continue
        for r in reps:
            ax.plot(r["trels"], r["alphas"], "-", color=color, lw=0.4, alpha=0.3)
        max_len = max(len(r["alphas"]) for r in reps)
        pad_a = np.full((len(reps), max_len), np.nan)
        pad_t = np.full((len(reps), max_len), np.nan)
        for i, r in enumerate(reps):
            pad_a[i, :len(r["alphas"])] = r["alphas"]
            pad_t[i, :len(r["trels"])] = r["trels"]
        ma = np.nanmean(pad_a, axis=0)
        mt = np.nanmean(pad_t, axis=0)
        valid = ~np.isnan(mt)
        ax.plot(mt[valid], ma[valid], "-", color=color, lw=2,
                label=f"α0={mode.replace('aimd','')} (n={len(reps)})")
    ax.set_xlabel("Time (ticks since t0)")
    ax.set_ylabel("α")
    ax.set_title("Exp 4: AIMD α Trajectory (x = real ticks)")
    ax.legend(loc="upper right")
    ax.set_ylim(-2, 105)
    fig.tight_layout()
    out = os.path.join(outdir, "exp4v2_alpha_traj.png")
    fig.savefig(out); print(f"[saved] {out}"); plt.close(fig)


def plot_miss_traj(runs, outdir):
    fig, ax = plt.subplots(figsize=(14, 4.5))
    span = runs[0]["span"] if runs else 72000
    add_phase_background(ax, span)
    modes = ["fixed0", "fixed30", "fixed60", "fixed100", "aimd15", "aimd50", "aimd100"]
    for mode in modes:
        reps = [r for r in runs if r["mode"] == mode and "miss_rates" in r and "trels" in r]
        if not reps:
            continue
        max_len = max(len(r["miss_rates"]) for r in reps)
        pad_m = np.full((len(reps), max_len), np.nan)
        pad_t = np.full((len(reps), max_len), np.nan)
        for i, r in enumerate(reps):
            pad_m[i, :len(r["miss_rates"])] = r["miss_rates"]
            pad_t[i, :len(r["trels"])] = r["trels"]
        mm = np.nanmean(pad_m, axis=0)
        mt = np.nanmean(pad_t, axis=0)
        valid = ~np.isnan(mt)
        color = COLORS.get(mode, "#666")
        lw = 1.5 if mode.startswith("aimd") else 1.0
        ls = "-" if mode.startswith("aimd") else "--"
        ax.plot(mt[valid], mm[valid], ls, color=color, lw=lw, label=mode)
    ax.set_xlabel("Time (ticks since t0)")
    ax.set_ylabel("ctrl Miss Rate (%)")
    ax.set_title("Exp 4: Per-window Miss Rate (x = real ticks)")
    ax.legend(ncol=2, fontsize=9)
    ax.set_ylim(-2, 105)
    fig.tight_layout()
    out = os.path.join(outdir, "exp4v2_miss_traj.png")
    fig.savefig(out); print(f"[saved] {out}"); plt.close(fig)


def plot_phase_comparison(stats, outdir):
    fig, axes = plt.subplots(1, 2, figsize=(12, 4.5))
    modes = ["fixed0", "fixed30", "fixed60", "fixed100", "aimd15", "aimd50", "aimd100"]
    colors = [COLORS.get(m, "#666") for m in modes]
    x = np.arange(3)
    width = 0.11

    ax = axes[0]
    for i, mode in enumerate(modes):
        s = next((x for x in stats if x.get("mode") == mode), None)
        if not s:
            continue
        vals = [s.get(f"ph{ph}_miss_rate_mean", 0) for ph in [0, 1, 2]]
        errs = [s.get(f"ph{ph}_miss_rate_std", 0) for ph in [0, 1, 2]]
        offset = (i - 3) * width
        ax.bar(x + offset, vals, width, yerr=errs, capsize=2,
               color=colors[i], edgecolor="black", linewidth=0.4, label=mode)
    ax.set_xticks(x)
    ax.set_xticklabels(["P0 Light", "P1 Heavy", "P2 Light"])
    ax.set_ylabel("ctrl Miss Rate (%)")
    ax.set_title("Miss Rate by Phase")
    ax.legend(ncol=2, fontsize=7)

    ax = axes[1]
    for i, mode in enumerate(modes):
        s = next((x for x in stats if x.get("mode") == mode), None)
        if not s:
            continue
        vals = [s.get(f"ph{ph}_ai_rd_mean", 0) for ph in [0, 1, 2]]
        errs = [s.get(f"ph{ph}_ai_rd_std", 0) for ph in [0, 1, 2]]
        offset = (i - 3) * width
        ax.bar(x + offset, vals, width, yerr=errs, capsize=2,
               color=colors[i], edgecolor="black", linewidth=0.4, label=mode)
    ax.set_xticks(x)
    ax.set_xticklabels(["P0 Light", "P1 Heavy", "P2 Light"])
    ax.set_ylabel("ai Avg Run Delta (ticks/window)")
    ax.set_title("ai Throughput by Phase")
    ax.legend(ncol=2, fontsize=7)

    fig.suptitle("Exp 4: Phase-resolved Comparison", fontsize=13)
    fig.tight_layout(rect=[0, 0, 1, 0.96])
    out = os.path.join(outdir, "exp4v2_phase_comparison.png")
    fig.savefig(out)
    print(f"[saved] {out}")
    plt.close(fig)


def plot_transition_zoom(runs, outdir):
    span = runs[0]["span"] if runs else 72000
    seg = span / 3.0
    margin = seg * 0.25
    fig, axes = plt.subplots(1, 2, figsize=(12, 4.5))
    boundaries = [(seg, "Light → Heavy"), (2 * seg, "Heavy → Light")]
    modes = ["fixed30", "fixed60", "fixed100", "aimd15", "aimd50", "aimd100"]
    for ax, (bnd, title) in zip(axes, boundaries):
        add_phase_background(ax, span)
        z0, z1 = bnd - margin, bnd + margin
        for mode in modes:
            reps = [r for r in runs if r["mode"] == mode and "miss_rates" in r and "trels" in r]
            if not reps:
                continue
            max_len = max(len(r["miss_rates"]) for r in reps)
            pad_m = np.full((len(reps), max_len), np.nan)
            pad_t = np.full((len(reps), max_len), np.nan)
            for i, r in enumerate(reps):
                pad_m[i, :len(r["miss_rates"])] = r["miss_rates"]
                pad_t[i, :len(r["trels"])] = r["trels"]
            mm = np.nanmean(pad_m, axis=0)
            mt = np.nanmean(pad_t, axis=0)
            mask = (~np.isnan(mt)) & (mt >= z0) & (mt <= z1)
            if not np.any(mask):
                continue
            color = COLORS.get(mode, "#666")
            lw = 2 if mode.startswith("aimd") else 1.2
            ls = "-" if mode.startswith("aimd") else "--"
            ax.plot(mt[mask], mm[mask], ls, color=color, lw=lw, label=mode)
        ax.axvline(bnd, color="gray", ls="--", lw=1, label="boundary")
        ax.set_xlim(z0, z1)
        ax.set_xlabel("Time (ticks since t0)")
        ax.set_ylabel("ctrl Miss Rate (%)")
        ax.set_title(title)
        ax.legend(fontsize=8)
    fig.suptitle("Exp 4: Phase Transition Zoom (x = real ticks)", fontsize=13)
    fig.tight_layout(rect=[0, 0, 1, 0.96])
    out = os.path.join(outdir, "exp4v2_transition_zoom.png")
    fig.savefig(out); print(f"[saved] {out}"); plt.close(fig)


def plot_summary(runs, stats, outdir):
    fig, axes = plt.subplots(2, 2, figsize=(11, 9))
    modes = ["fixed0", "fixed30", "fixed60", "fixed100", "aimd15", "aimd50", "aimd100"]
    colors = [COLORS.get(m, "#666") for m in modes]
    span = (stats[0]["span"] if stats and "span" in stats[0] and stats[0]["span"]
            else (runs[0]["span"] if runs else 72000))

    ax = axes[0, 0]
    vals, errs = [], []
    for m in modes:
        st = next((x for x in stats if x.get("mode") == m), None)
        vals.append(st.get("miss_rate_mean", 0) if st else 0)
        errs.append(st.get("miss_rate_std", 0) if st else 0)
    ax.bar(range(len(modes)), vals, yerr=errs, capsize=3, color=colors,
           edgecolor="black", linewidth=0.6, width=0.5)
    for bar, v in zip(ax.patches, vals):
        ax.text(bar.get_x() + bar.get_width() / 2,
                v + (max(vals) * 0.02 if max(vals) > 0 else 1),
                f"{v:.1f}%", ha="center", fontsize=8, fontweight="bold")
    ax.set_xticks(range(len(modes)))
    ax.set_xticklabels(modes, rotation=45, ha="right", fontsize=8)
    ax.set_ylabel("miss%"); ax.set_title("Overall Miss Rate")

    ax = axes[0, 1]
    vals, errs = [], []
    for m in modes:
        reps = [r for r in runs if r["mode"] == m]
        if reps:
            totals = [float(np.sum(r.get("ai_rds", []))) for r in reps]
            vals.append(np.mean(totals)); errs.append(np.std(totals))
        else:
            vals.append(0); errs.append(0)
    ax.bar(range(len(modes)), vals, yerr=errs, capsize=3, color=colors,
           edgecolor="black", linewidth=0.6, width=0.5)
    ax.set_xticks(range(len(modes)))
    ax.set_xticklabels(modes, rotation=45, ha="right", fontsize=8)
    ax.set_ylabel("ai total run_delta"); ax.set_title("Overall ai Throughput")

    ax = axes[1, 0]
    add_phase_background(ax, span)
    for mode, color in [("aimd15", COLORS["aimd15"]),
                        ("aimd50", COLORS["aimd50"]),
                        ("aimd100", COLORS["aimd100"])]:
        st = next((x for x in stats if x.get("mode") == mode), None)
        if not st or "alphas_mean" not in st or "trels_mean" not in st:
            continue
        mt = np.asarray(st["trels_mean"]); ma = np.asarray(st["alphas_mean"])
        valid = ~np.isnan(mt)
        ax.plot(mt[valid], ma[valid], "-", color=color, lw=1.5,
                label=f"α0={mode.replace('aimd','')}")
    ax.set_xlabel("Time (ticks since t0)"); ax.set_ylabel("α")
    ax.set_title("α Trajectory (AIMD)"); ax.legend(fontsize=9)

    ax = axes[1, 1]
    vals, errs = [], []
    for m in modes:
        st = next((x for x in stats if x.get("mode") == m), None)
        vals.append(st.get("ph1_miss_rate_mean", 0) if st else 0)
        errs.append(st.get("ph1_miss_rate_std", 0) if st else 0)
    ax.bar(range(len(modes)), vals, yerr=errs, capsize=3, color=colors,
           edgecolor="black", linewidth=0.6, width=0.5)
    for bar, v in zip(ax.patches, vals):
        ax.text(bar.get_x() + bar.get_width() / 2,
                v + (max(vals) * 0.02 if max(vals) > 0 else 1),
                f"{v:.1f}%", ha="center", fontsize=8, fontweight="bold")
    ax.set_xticks(range(len(modes)))
    ax.set_xticklabels(modes, rotation=45, ha="right", fontsize=8)
    ax.set_ylabel("miss%"); ax.set_title("Heavy Phase (P1) Miss Rate")

    fig.suptitle("Exp 4: Summary", fontsize=13)
    fig.tight_layout(rect=[0, 0, 1, 0.96])
    out = os.path.join(outdir, "exp4v2_summary.png")
    fig.savefig(out); print(f"[saved] {out}"); plt.close(fig)


# ------------------------------------------------------------------ main
def main():
    if len(sys.argv) < 2:
        print(f"Usage: {sys.argv[0]} <sexp4_dyn_v2.csv>")
        sys.exit(1)

    path = sys.argv[1]
    outdir = os.path.dirname(path) or "./logs/sched/dyn"
    os.makedirs(outdir, exist_ok=True)

    runs = parse(path)
    print(f"[parsed] {len(runs)} runs (warmup discarded)")

    computed = [compute(r) for r in runs]
    stats = aggregate(computed, ["mode", "alpha0"])

    print()
    print_summary(stats)

    plot_miss_all(stats, outdir)
    plot_alpha_traj(computed, outdir)
    plot_miss_traj(computed, outdir)
    plot_phase_comparison(stats, outdir)
    plot_transition_zoom(computed, outdir)
    plot_summary(computed, stats, outdir)

    print("\nDone.")


if __name__ == "__main__":
    main()