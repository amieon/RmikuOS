#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""stat_exp8_pid.py -- Exp8: 增量式 PI vs AIMD (经典控制 vs 启发式)

核心假设(可证伪): PI 轻相位爬升 ≈ +0.6/窗 << AIMD +5/窗 => ratio=800(L 占 80%)时
PI 的 ai 吞吐显著落后于 AIMD; 相位切换后 PI 因 windup(积分累积)呆滞。

用法: python3 stat_exp8_pid.py <sexp8_pid.csv> [sexp5_phase.csv] [sexp4_dyn.csv]
"""
import sys
import os

import numpy as np
import matplotlib.pyplot as plt

from schedlab_stat import (
    RATIOS, RATIO_LABELS, RATIO_L_PCT, color_of, add_phase_shading,
    phase_bounds_for_ratio, parse_csv, compute_run, aggregate_runs, fmt_err,
    plot_miss_traj,
)

PID_MODES = ["pid0", "pid100"]
AIMD_MODES = ["aimd0", "aimd50", "aimd100"]


def print_summary(stats):
    print("=" * 110)
    print("EXPERIMENT 8: PI vs AIMD")
    print("=" * 110)
    for ratio in RATIOS:
        print(f"\n--- RATIO {RATIO_LABELS[ratio]} (L={RATIO_L_PCT[ratio]}%) ---")
        print(f"{'mode':>9}  {'miss%':>18}  {'ai_work':>9}  {'α_steady':>8}")
        print("-" * 110)
        for mode in ["fixed25"] + AIMD_MODES + PID_MODES:
            s = stats.get((ratio, mode))
            if not s:
                continue
            print(f"{mode:>9}  {fmt_err(s.get('miss_rate_mean',0), s.get('miss_rate_hi',0), s.get('miss_rate_lo',0))}  "
                  f"{s.get('ai_work_mean',0):>9.0f}  {s.get('alpha_steady_mean',0):>8.1f}")


def plot_traj(computed, outdir):
    """pid vs aimd α 轨迹(每 ratio)。"""
    n = len(RATIOS)
    fig, axes = plt.subplots(1, n, figsize=(6 * n, 5))
    if n == 1:
        axes = [axes]
    for idx, ratio in enumerate(RATIOS):
        ax = axes[idx]
        for mode in AIMD_MODES + PID_MODES:
            reps = [r for r in computed if r["ratio"] == ratio and r["mode"] == mode
                    and len(r.get("alpha_traj", [])) > 0]
            if not reps:
                continue
            gmin = min(len(r["alpha_traj"]) for r in reps)
            aligned = np.array([r["alpha_traj"][:gmin] for r in reps])
            ls = "-" if mode.startswith("aimd") else "--"
            ax.plot(np.arange(gmin), aligned.mean(axis=0), ls, color=color_of(mode), lw=2, label=mode)
        max_w = max((max(r["alpha_wins"]) for r in computed
                     if r["ratio"] == ratio and len(r.get("alpha_wins", [])) > 0), default=100)
        add_phase_shading(ax, phase_bounds_for_ratio(ratio, max_w), 105)
        ax.set_title(f"Ratio {RATIO_LABELS[ratio]} (L={RATIO_L_PCT[ratio]}%)")
        ax.set_xlabel("Window"); ax.set_ylabel("α"); ax.set_ylim(-2, 105)
        ax.legend(fontsize=7)
    fig.suptitle("Exp8: PI (dashed) vs AIMD (solid) α trajectory", fontsize=13)
    fig.tight_layout(rect=[0, 0, 1, 0.95])
    out = os.path.join(outdir, "exp8_pid_vs_aimd_traj.png")
    fig.savefig(out); print(f"[saved] {out}"); plt.close(fig)


def plot_light_phase_slope(computed, outdir):
    """轻相位爬升速率: PI vs AIMD (核心假设图)。"""
    fig, ax = plt.subplots(figsize=(8, 5))
    # 对 ratio=800, 量 L 段(第一段)内 α 的平均每窗爬升
    for mode in AIMD_MODES + PID_MODES:
        xs, ys = [], []
        for ratio in RATIOS:
            reps = [r for r in computed if r["ratio"] == ratio and r["mode"] == mode]
            if not reps:
                continue
            r = reps[0]
            traj = r.get("alpha_traj", [])
            wins = r.get("alpha_wins", [])
            if len(traj) < 10:
                continue
            max_w = max(wins)
            b1 = phase_bounds_for_ratio(ratio, max_w)[1]  # L1 段结束
            seg = [traj[i] for i in range(len(traj)) if 0 <= wins[i] < b1 and traj[i] > 0]
            if len(seg) >= 5:
                slope = (seg[-1] - seg[0]) / max(1, len(seg) - 1)
            else:
                slope = 0
            xs.append(RATIO_L_PCT[ratio]); ys.append(slope)
        if xs:
            ax.plot(xs, ys, "o-", color=color_of(mode), ms=8, label=mode)
    ax.axhline(5, color="gray", ls=":", lw=1, label="AIMD +5/窗")
    ax.set_xlabel("L-segment Proportion (%)")
    ax.set_ylabel("轻相位爬升速率 (α点/窗)")
    ax.set_title("Exp8 core: PI climbs light phase far slower than AIMD")
    ax.legend(fontsize=8)
    fig.tight_layout()
    out = os.path.join(outdir, "exp8_light_phase_slope.png")
    fig.savefig(out); print(f"[saved] {out}"); plt.close(fig)


def main():
    if len(sys.argv) < 2:
        print(f"Usage: {sys.argv[0]} <sexp8_pid.csv> [sexp5_phase.csv] [sexp4_dyn.csv]")
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

    global RATIOS
    present = sorted(set(r["ratio"] for r in computed))
    if present:
        RATIOS = [r for r in [500, 800, 200] if r in present] or present

    print_summary(stats)
    plot_traj(computed, outdir)
    plot_light_phase_slope(computed, outdir)
    plot_miss_traj(computed, ["fixed25", "aimd50", "aimd100", "pid0", "pid100"],
                   outdir, "exp8_pid_miss_traj.png",
                   "Exp8: PI vs AIMD miss-rate trajectory")
    print("\nDone.")


if __name__ == "__main__":
    main()
