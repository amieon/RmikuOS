#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""stat_exp9_ucb.py -- Exp9: 滑窗 UCB (在线学习, 11 臂离散决策)

核心问题:
  - 臂选择分布: UCB 是否收敛到某几个臂? 还是均匀乱扫?
  - 稳态臂 vs 理论临界带: 由 exp4 校准, 真实 miss 临界带在 α≈25-50(H 段)。
    UCB 在 H 段应偏好 ≤50 的臂, L 段可安全上探更高臂。
  - 相位切换: 无上下文 => 切换后 τ=32 窗内臂分布换血, 检验滞后。

用法: python3 stat_exp9_ucb.py <sexp9_ucb.csv>
"""
import sys
import os
from collections import Counter

import numpy as np
import matplotlib.pyplot as plt

from schedlab_stat import (
    RATIOS, RATIO_LABELS, RATIO_L_PCT, color_of, add_phase_shading,
    phase_bounds_for_ratio, parse_csv, compute_run, aggregate_runs, fmt_err,
)


def print_summary(stats, computed):
    print("=" * 100)
    print("EXPERIMENT 9: SW-UCB (11 arms)")
    print("=" * 100)
    for ratio in RATIOS:
        s = stats.get((ratio, "ucb"))
        if not s:
            continue
        print(f"\n--- RATIO {RATIO_LABELS[ratio]} (L={RATIO_L_PCT[ratio]}%) ---")
        print(f"  miss% {fmt_err(s.get('miss_rate_mean',0), s.get('miss_rate_hi',0), s.get('miss_rate_lo',0))}"
              f"  α_steady {s.get('alpha_steady_mean',0):.1f}")
        # 臂分布(所有 rep 合并)
        arms = []
        for r in computed:
            if r["ratio"] == ratio and r["mode"] == "ucb" and "arms" in r:
                arms.extend(r["arms"])
        if arms:
            c = Counter(arms)
            dist = ", ".join(f"α{arm*10}:{c.get(arm,0)}" for arm in range(11))
            top = c.most_common(3)
            print(f"  臂选择分布: {dist}")
            print(f"  最常选: {[(a*10, n) for a, n in top]}")


def plot_arm_trajectory(computed, outdir):
    """臂选择时间序列(step plot), 每 ratio 一张, 叠加相位阴影。"""
    n = len(RATIOS)
    fig, axes = plt.subplots(1, n, figsize=(6 * n, 5))
    if n == 1:
        axes = [axes]
    for idx, ratio in enumerate(RATIOS):
        ax = axes[idx]
        reps = [r for r in computed if r["ratio"] == ratio and r["mode"] == "ucb"
                and "arms" in r]
        if not reps:
            continue
        r = reps[0]
        wins = r["alpha_wins"]
        arms_alpha = [a * 10 for a in r["arms"]]
        ax.step(wins, arms_alpha, where="post", color="#7c3aed", lw=1.2)
        max_w = max(wins) if len(wins) else 100
        add_phase_shading(ax, phase_bounds_for_ratio(ratio, max_w), 105)
        # 理论临界带标记
        ax.axhspan(25, 50, color="#fde68a", alpha=0.35, label="真实临界带(exp4 校准)")
        ax.set_title(f"Ratio {RATIO_LABELS[ratio]} (L={RATIO_L_PCT[ratio]}%)")
        ax.set_xlabel("Window"); ax.set_ylabel("α (选中的臂)")
        ax.set_ylim(-5, 105)
        ax.legend(fontsize=7)
    fig.suptitle("Exp9: SW-UCB arm selection trajectory", fontsize=13)
    fig.tight_layout(rect=[0, 0, 1, 0.95])
    out = os.path.join(outdir, "exp9_ucb_arm_traj.png")
    fig.savefig(out); print(f"[saved] {out}"); plt.close(fig)


def plot_arm_hist(computed, outdir):
    """臂选择频次直方图(每 ratio 一张)。"""
    n = len(RATIOS)
    fig, axes = plt.subplots(1, n, figsize=(6 * n, 4))
    if n == 1:
        axes = [axes]
    for idx, ratio in enumerate(RATIOS):
        ax = axes[idx]
        arms = []
        for r in computed:
            if r["ratio"] == ratio and r["mode"] == "ucb" and "arms" in r:
                arms.extend(r["arms"])
        if not arms:
            continue
        c = Counter(arms)
        xs = [a * 10 for a in range(11)]
        ys = [c.get(a, 0) for a in range(11)]
        colors = ["#fbbf24" if 25 <= a * 10 <= 50 else "#94a3b8" for a in range(11)]
        ax.bar(xs, ys, width=8, color=colors, edgecolor="black", linewidth=0.5)
        ax.set_title(f"Ratio {RATIO_LABELS[ratio]} (L={RATIO_L_PCT[ratio]}%)")
        ax.set_xlabel("α (臂)"); ax.set_ylabel("选择次数")
        ax.axvspan(25, 50, color="#fde68a", alpha=0.3)
    fig.suptitle("Exp9: SW-UCB arm selection histogram (黄带=理论临界带)", fontsize=13)
    fig.tight_layout(rect=[0, 0, 1, 0.95])
    out = os.path.join(outdir, "exp9_ucb_arm_hist.png")
    fig.savefig(out); print(f"[saved] {out}"); plt.close(fig)


def plot_arm_by_phase(computed, outdir):
    """分相位(L vs H)臂选择分布对比——检验无上下文的滞后(UCB 的已知弱点)。"""
    fig, axes = plt.subplots(1, len(RATIOS), figsize=(6 * len(RATIOS), 4))
    if len(RATIOS) == 1:
        axes = [axes]
    for idx, ratio in enumerate(RATIOS):
        ax = axes[idx]
        cnt_L = [0] * 11
        cnt_H = [0] * 11
        for r in computed:
            if r["ratio"] != ratio or r["mode"] != "ucb" or "arms" not in r:
                continue
            wins = r["alpha_wins"]
            if len(wins) == 0:
                continue
            pb = phase_bounds_for_ratio(ratio, max(wins))
            for i, a in enumerate(r["arms"]):
                w = wins[i]
                if pb[0] <= w < pb[1] or pb[2] <= w < pb[3]:
                    cnt_L[a] += 1
                else:
                    cnt_H[a] += 1
        x = np.arange(11) * 10
        w = 3.5
        ax.bar(x - w / 2, cnt_L, w, color="#16a34a", label="L 相")
        ax.bar(x + w / 2, cnt_H, w, color="#dc2626", label="H 相")
        ax.axvspan(25, 50, color="#fde68a", alpha=0.3)
        ax.set_xticks(x); ax.set_title(f"Ratio {RATIO_LABELS[ratio]}")
        ax.set_xlabel("α (臂)"); ax.legend(fontsize=8)
    fig.suptitle("Exp9: SW-UCB arm distribution by phase (黄带=理论临界带)", fontsize=13)
    fig.tight_layout(rect=[0, 0, 1, 0.95])
    out = os.path.join(outdir, "exp9_ucb_arm_by_phase.png")
    fig.savefig(out); print(f"[saved] {out}"); plt.close(fig)


def main():
    if len(sys.argv) < 2:
        print(f"Usage: {sys.argv[0]} <sexp9_ucb.csv>")
        sys.exit(1)
    path = sys.argv[1]
    runs = parse_csv(path)
    print(f"[parsed] {len(runs)} runs from {os.path.basename(path)}")
    outdir = os.path.dirname(path) or "."
    os.makedirs(outdir, exist_ok=True)

    computed = [compute_run(r) for r in runs]
    stats = aggregate_runs(computed)

    global RATIOS
    present = sorted(set(r["ratio"] for r in computed))
    if present:
        RATIOS = [r for r in [500, 800, 200] if r in present] or present

    print_summary(stats, computed)
    plot_arm_trajectory(computed, outdir)
    plot_arm_hist(computed, outdir)
    plot_arm_by_phase(computed, outdir)
    print("\nDone.")


if __name__ == "__main__":
    main()
