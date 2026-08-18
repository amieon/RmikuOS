#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""stat_exp6_cubic.py -- Exp6: CUBIC vs AIMD (网络协议演化 vs 启发式)

核心假设(可证伪): CUBIC 逼近 w_max 时减速缓行 => 过冲更少 => n_down 显著少于 AIMD,
而非吞吐碾压(α 值域 0-100 太小, 三次曲线被压扁)。

数据合并:
  exp6 cubic 数据 (3 ratios x 3 modes)  + exp5 AIMD(800/200) + exp4 AIMD/fixed(500, ratio=0 等价)

用法: python3 stat_exp6_cubic.py <sexp6_cubic.csv> [sexp5_phase.csv] [sexp4_dyn.csv]
"""
import sys
import os

import numpy as np
import matplotlib.pyplot as plt

from schedlab_stat import (
    RATIOS, RATIO_LABELS, RATIO_L_PCT, PHASE_NAMES, COLORS, color_of,
    phase_bounds_for_ratio, add_phase_shading, parse_csv, compute_run,
    aggregate_runs, fmt_err, plot_miss_traj,
)

CUBIC_MODES = ["cubic0", "cubic50", "cubic100"]
AIMD_MODES = ["aimd0", "aimd50", "aimd100"]


def print_summary(stats):
    print("=" * 120)
    print("EXPERIMENT 6: CUBIC vs AIMD")
    print("=" * 120)
    for ratio in RATIOS:
        print(f"\n--- RATIO {RATIO_LABELS[ratio]} (L={RATIO_L_PCT[ratio]}%) ---")
        print(f"{'mode':>9}  {'miss%':>18}  {'sh_ai':>7}  {'ai_work':>9}  "
              f"{'α_steady':>8}  {'n_down':>7}  {'n_up':>6}")
        print("-" * 120)
        for mode in ["fixed0", "fixed25", "fixed50"] + AIMD_MODES + CUBIC_MODES:
            s = stats.get((ratio, mode))
            if not s:
                continue
            print(f"{mode:>9}  {fmt_err(s.get('miss_rate_mean',0), s.get('miss_rate_hi',0), s.get('miss_rate_lo',0))}  "
                  f"{s.get('share',{}).get('ai',0) if 'share' in s else 0:>7.1f}  "
                  f"{s.get('ai_work_mean',0):>9.0f}  {s.get('alpha_steady_mean',0):>8.1f}  "
                  f"{s.get('n_down_mean',0):>7.0f}  {s.get('n_up_mean',0):>6.0f}")


def plot_traj(computed, outdir):
    """cubic vs aimd α 轨迹(每 ratio 一张)。"""
    n = len(RATIOS)
    fig, axes = plt.subplots(1, n, figsize=(6 * n, 5))
    if n == 1:
        axes = [axes]
    for idx, ratio in enumerate(RATIOS):
        ax = axes[idx]
        for mode in AIMD_MODES + CUBIC_MODES:
            reps = [r for r in computed if r["ratio"] == ratio and r["mode"] == mode
                    and len(r.get("alpha_traj", [])) > 0]
            if not reps:
                continue
            # NaN 填充到最长 rep: 短 rep 只造成断点, 不再截断均值曲线
            maxlen = max(len(r["alpha_traj"]) for r in reps)
            aligned = np.full((len(reps), maxlen), np.nan)
            for i, r in enumerate(reps):
                aligned[i, :len(r["alpha_traj"])] = r["alpha_traj"]
            mean = np.nanmean(aligned, axis=0)
            ls = "-" if mode.startswith("aimd") else "--"
            ax.plot(np.arange(maxlen), mean, ls, color=color_of(mode), lw=2, label=mode)
        max_w = max((max(r["alpha_wins"]) for r in computed
                     if r["ratio"] == ratio and len(r.get("alpha_wins", [])) > 0), default=100)
        add_phase_shading(ax, phase_bounds_for_ratio(ratio, max_w), 105)
        ax.set_title(f"Ratio {RATIO_LABELS[ratio]} (L={RATIO_L_PCT[ratio]}%)")
        ax.set_xlabel("Window"); ax.set_ylabel("α"); ax.set_ylim(-2, 105)
        ax.legend(fontsize=7)
    fig.suptitle("Exp6: CUBIC (dashed) vs AIMD (solid) α trajectory", fontsize=13)
    fig.tight_layout(rect=[0, 0, 1, 0.95])
    out = os.path.join(outdir, "exp6_cubic_vs_aimd_traj.png")
    fig.savefig(out); print(f"[saved] {out}"); plt.close(fig)


def plot_overshoot(stats, outdir):
    """过冲指标: n_down (退避次数) 对比, 核心假设图。"""
    fig, ax = plt.subplots(figsize=(8, 5))
    x = np.arange(len(RATIOS))
    w = 0.2
    for gi, (label, modes) in enumerate([("AIMD", AIMD_MODES), ("CUBIC", CUBIC_MODES)]):
        for mi, mode in enumerate(modes):
            vals = []
            for ratio in RATIOS:
                s = stats.get((ratio, mode))
                vals.append(s.get("n_down_mean", 0) if s else 0)
            off = (gi * len(CUBIC_MODES) + mi - (2 * len(CUBIC_MODES) - 1) / 2) * w
            ax.bar(x + off, vals, w * 0.9, color=color_of(mode),
                   edgecolor="black", linewidth=0.5, label=mode)
    ax.set_xticks(x); ax.set_xticklabels([RATIO_LABELS[r] for r in RATIOS])
    ax.set_ylabel("n_down (back-off count)")
    ax.set_title("Exp6 core hypothesis: CUBIC overshoots less (fewer α-down moves)")
    ax.legend(fontsize=8)
    fig.tight_layout()
    out = os.path.join(outdir, "exp6_cubic_overshoot.png")
    fig.savefig(out); print(f"[saved] {out}"); plt.close(fig)


def plot_burn_vs_miss(stats, outdir):
    """burn vs miss 散点(含 cubic)。"""
    n = len(RATIOS)
    fig, axes = plt.subplots(1, n, figsize=(6 * n, 5))
    if n == 1:
        axes = [axes]
    for idx, ratio in enumerate(RATIOS):
        ax = axes[idx]
        for mode in ["fixed0", "fixed25", "fixed50"] + AIMD_MODES + CUBIC_MODES:
            s = stats.get((ratio, mode))
            if not s:
                continue
            x = s.get("ai_work_mean", 0); y = s.get("miss_rate_mean", 0)
            marker = "s" if mode.startswith("cubic") else ("o" if mode.startswith("aimd") else "^")
            ax.plot(x, y, marker, color=color_of(mode), ms=9,
                    markeredgecolor="black", markeredgewidth=0.8, label=mode)
        ax.set_title(f"Ratio {RATIO_LABELS[ratio]} (L={RATIO_L_PCT[ratio]}%)")
        ax.set_xlabel("ai work"); ax.set_ylabel("ctrl miss %")
        ax.legend(fontsize=7)
    fig.suptitle("Exp6: CUBIC (square) vs AIMD (circle) vs fixed (triangle)", fontsize=13)
    fig.tight_layout(rect=[0, 0, 1, 0.95])
    out = os.path.join(outdir, "exp6_cubic_burn_vs_miss.png")
    fig.savefig(out); print(f"[saved] {out}"); plt.close(fig)


def main():
    if len(sys.argv) < 2:
        print(f"Usage: {sys.argv[0]} <sexp6_cubic.csv> [sexp5_phase.csv] [sexp4_dyn.csv]")
        sys.exit(1)
    all_runs, outdir = [], None
    for path in sys.argv[1:]:
        base = os.path.basename(path)
        is_exp4 = "sexp4" in base
        runs = parse_csv(path, default_ratio=500 if is_exp4 else None)
        print(f"[parsed] {len(runs)} runs from {base}")
        all_runs.extend(runs)
        if outdir is None:
            outdir = os.path.dirname(path) or "."
    os.makedirs(outdir, exist_ok=True)
    computed = [compute_run(r) for r in all_runs]
    stats = aggregate_runs(computed)

    # 只保留有数据的 ratio
    global RATIOS
    present = sorted(set(r["ratio"] for r in computed))
    if present:
        RATIOS = [r for r in [500, 800, 200] if r in present] or present

    print_summary(stats)
    plot_traj(computed, outdir)
    plot_overshoot(stats, outdir)
    plot_burn_vs_miss(stats, outdir)
    plot_miss_traj(computed, ["fixed25", "aimd50", "aimd100", "cubic50", "cubic100"],
                   outdir, "exp6_cubic_miss_traj.png",
                   "Exp6: CUBIC vs AIMD miss-rate trajectory")
    print("\nDone.")


if __name__ == "__main__":
    main()
