#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""stat_exp7_optims.py -- Exp7: 优化器家族 ablation (SGD-M/RMSProp/AdaGrad/AdamW)

核心(定理3): 四个优化器共用 SPSA 梯度/loss/lr, 只换更新公式; AdamW 多一个 weight decay。
  - 分量分解: 每窗 α_f(t+1) = α_f(t) - step + decay (恒等式级核对)
  - 贡献分解: Σdecay vs Σstep 的净位移贡献 (AdamW 稳定性来自 decay 的假设)
  - 安全区冻结: miss=0 段三兄弟(无 decay)α 应冻结, AdamW 有系统性向 target 漂移

用法: python3 stat_exp7_optims.py <sexp7_optims.csv>
"""
import sys
import os
import math
from collections import defaultdict

import numpy as np
import matplotlib.pyplot as plt

from schedlab_stat import (
    RATIOS, RATIO_LABELS, RATIO_L_PCT, color_of, add_phase_shading,
    phase_bounds_for_ratio, parse_csv, compute_run, aggregate_runs, fmt_err,
    plot_miss_traj,
)

OPTIMS = ["sgdm", "rmsprop", "adagrad", "adamw"]


def print_summary(stats):
    print("=" * 130)
    print("EXPERIMENT 7: OPTIMIZER FAMILY ABLATION  (定理3 分量分解)")
    print("=" * 130)
    for ratio in RATIOS:
        print(f"\n--- RATIO {RATIO_LABELS[ratio]} (L={RATIO_L_PCT[ratio]}%) ---")
        print(f"{'mode':>9}  {'miss%':>18}  {'sh_ai':>7}  {'ai_burn':>9}  {'ai_run':>9}  "
              f"{'α_steady':>8}  {'Σstep':>10}  {'Σdecay':>10}  {'Σstep/窗':>9}  {'Σdecay/窗':>10}")
        print("-" * 130)
        for mode in OPTIMS:
            s = stats.get((ratio, mode))
            if not s:
                continue
            print(f"{mode:>9}  {fmt_err(s.get('miss_rate_mean',0), s.get('miss_rate_hi',0), s.get('miss_rate_lo',0))}  "
                  f"{s.get('share_ai_mean',0):>7.1f}  {s.get('ai_burn_mean',0):>9.0f}  "
                  f"{s.get('run_ai_mean',0):>9.0f}  "
                  f"{s.get('alpha_steady_mean',0):>8.1f}  "
                  f"{s.get('sum_step_mean',0):>10.0f}  {s.get('sum_decay_mean',0):>10.0f}  "
                  f"{s.get('sum_step_mean',0)/2400 if s.get('nreps',1) else 0:>9.1f}  "
                  f"{s.get('sum_decay_mean',0)/2400 if s.get('nreps',1) else 0:>10.1f}")


def plot_alpha_traj(computed, outdir):
    """四优化器 α 轨迹对比(每 ratio)。"""
    n = len(RATIOS)
    fig, axes = plt.subplots(1, n, figsize=(6 * n, 5))
    if n == 1:
        axes = [axes]
    for idx, ratio in enumerate(RATIOS):
        ax = axes[idx]
        for mode in OPTIMS:
            reps = [r for r in computed if r["ratio"] == ratio and r["mode"] == mode
                    and len(r.get("alpha_traj", [])) > 0]
            if not reps:
                continue
            # NaN 填充到最长 rep: 短 rep 只造成断点, 不再截断均值曲线
            maxlen = max(len(r["alpha_traj"]) for r in reps)
            aligned = np.full((len(reps), maxlen), np.nan)
            for i, r in enumerate(reps):
                aligned[i, :len(r["alpha_traj"])] = r["alpha_traj"]
            ax.plot(np.arange(maxlen), np.nanmean(aligned, axis=0),
                    color=color_of(mode), lw=2, label=mode)
        max_w = max((max(r["alpha_wins"]) for r in computed
                     if r["ratio"] == ratio and len(r.get("alpha_wins", [])) > 0), default=100)
        add_phase_shading(ax, phase_bounds_for_ratio(ratio, max_w), 105)
        ax.axhline(25, color="gray", ls=":", lw=1)
        ax.set_title(f"Ratio {RATIO_LABELS[ratio]} (L={RATIO_L_PCT[ratio]}%)")
        ax.set_xlabel("Window"); ax.set_ylabel("α"); ax.set_ylim(-2, 105)
        ax.legend(fontsize=8)
    fig.suptitle("Exp7: optimizer family α trajectory (adamw has decay=25)", fontsize=13)
    fig.tight_layout(rect=[0, 0, 1, 0.95])
    out = os.path.join(outdir, "exp7_optims_alpha_traj.png")
    fig.savefig(out); print(f"[saved] {out}"); plt.close(fig)


def plot_decay_decomposition(computed, outdir):
    """定理3 核心图: AdamW 的 α 轨迹 + step/decay 分量分解(每 ratio)。"""
    n = len(RATIOS)
    fig, axes = plt.subplots(n, 1, figsize=(10, 4 * n))
    if n == 1:
        axes = [axes]
    for idx, ratio in enumerate(RATIOS):
        ax = axes[idx]
        reps = [r for r in computed if r["ratio"] == ratio and r["mode"] == "adamw"
                and "step" in r and "decay" in r]
        if not reps:
            ax.set_title(f"Ratio {RATIO_LABELS[ratio]}: no adamw data")
            continue
        r = reps[0]
        wins = r["alpha_wins"]
        ax.plot(wins, r["alpha_traj"], color="#111", lw=1.5, label="α")
        ax.plot(wins, r["step"] / 1024.0, color="#dc2626", lw=1, label="step (α/win)")
        ax.plot(wins, r["decay"] / 1024.0, color="#059669", lw=1, label="decay (α/win)")
        ax.axhline(0, color="gray", ls="--", lw=0.8)
        ax.axhline(25, color="#7c3aed", ls=":", lw=1, label="target=25")
        ax.set_title(f"Ratio {RATIO_LABELS[ratio]}: AdamW α + step/decay components (x1/1024)")
        ax.set_xlabel("Window"); ax.legend(fontsize=8)
    fig.tight_layout()
    out = os.path.join(outdir, "exp7_adamw_decay_decomp.png")
    fig.savefig(out); print(f"[saved] {out}"); plt.close(fig)


def verify_identity(computed):
    """离线重放核对: α_f(t+1) = α_f(t) - step + decay (×1024 定点, clamp [0,102400])。"""
    print("\n定理3 分量恒等式核对(离线重放 A 行):")
    for r in computed:
        if r["mode"] != "adamw" or "step" not in r:
            continue
        alpha_f = r["alpha0"] * 1024
        mismatches = 0
        for i in range(len(r["alpha_traj"])):
            st = int(r["step"][i]); dc = int(r["decay"][i])
            alpha_f -= (st - dc)
            alpha_f = max(0, min(102400, alpha_f))
            if int(alpha_f // 1024) != int(r["alpha_traj"][i]):
                mismatches += 1
        print(f"  ratio={r['ratio']} rep={r['rep']}: {len(r['alpha_traj'])} 窗, "
              f"整数α失配 {mismatches} (定点截断误差, 应 ≤ 少量)")
        break  # 只核对第一个 adamw


def plot_safe_zone_drift(computed, outdir):
    """安全区(连续 miss=0)内 α 漂移: AdamW(decay) vs 三兄弟(无 decay)。"""
    fig, ax = plt.subplots(figsize=(8, 5))
    # 找每个 mode 首个 ratio 的最长 miss=0 连续段, 画段内 α 轨迹
    for ratio in RATIOS:
        for mode in OPTIMS:
            reps = [r for r in computed if r["ratio"] == ratio and r["mode"] == mode]
            if not reps:
                continue
            r = reps[0]
            if len(r.get("win_miss", [])) == 0 or len(r.get("alpha_traj", [])) == 0:
                continue
            # miss=0 连续段
            zero = r["win_miss"] == 0
            best, cur, segs = [], [], []
            for i, z in enumerate(zero):
                if z:
                    cur.append(i)
                else:
                    if len(cur) >= 15:
                        segs.append(cur)
                    cur = []
            if len(cur) >= 15:
                segs.append(cur)
            if not segs:
                continue
            seg = max(segs, key=len)
            alpha_seg = r["alpha_traj"][seg]
            ax.plot(range(len(alpha_seg)), alpha_seg, color=color_of(mode),
                    lw=1.5, label=f"{mode}@R{RATIO_LABELS[ratio]}")
            break  # 每 mode 一个 ratio 的段即可
    ax.axhline(25, color="gray", ls=":", lw=1, label="target=25")
    ax.set_xlabel("window # in miss=0 segment")
    ax.set_ylabel("α")
    ax.set_title("Exp7 theorem-3: safe-zone drift (adamw vs no-decay optimizers)")
    ax.legend(fontsize=8)
    fig.tight_layout()
    out = os.path.join(outdir, "exp7_safe_zone_drift.png")
    fig.savefig(out); print(f"[saved] {out}"); plt.close(fig)


def plot_decay_step_contrib(stats, outdir):
    """定理3 核心图: Σdecay vs Σstep 累计位移分量 (AdamW 独有的 decay 通道)。"""
    fig, axes = plt.subplots(1, len(RATIOS), figsize=(6 * len(RATIOS), 5))
    if len(RATIOS) == 1:
        axes = [axes]
    for idx, ratio in enumerate(RATIOS):
        ax = axes[idx]
        x = np.arange(len(OPTIMS))
        w = 0.35
        decays = [stats.get((ratio, m), {}).get("sum_decay_mean", 0) / 1024.0 for m in OPTIMS]
        steps = [stats.get((ratio, m), {}).get("sum_step_mean", 0) / 1024.0 for m in OPTIMS]
        ax.bar(x - w / 2, decays, w, color="#059669", label="Σdecay (pull to target=25)")
        ax.bar(x + w / 2, steps, w, color="#dc2626", label="Σstep (gradient)")
        ax.set_xticks(x); ax.set_xticklabels(OPTIMS, fontsize=9)
        ax.set_title(f"Ratio {RATIO_LABELS[ratio]} (L={RATIO_L_PCT[ratio]}%)")
        ax.set_ylabel("cumulative displacement (α)")
        ax.axhline(0, color="gray", lw=1)
        ax.legend(fontsize=8)
    fig.suptitle("Exp7 theorem-3: decay is AdamW's exclusive stabilization channel", fontsize=13)
    fig.tight_layout(rect=[0, 0, 1, 0.95])
    out = os.path.join(outdir, "exp7_decay_step_contrib.png")
    fig.savefig(out); print(f"[saved] {out}"); plt.close(fig)


def main():
    if len(sys.argv) < 2:
        print(f"Usage: {sys.argv[0]} <sexp7_optims.csv>")
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

    print_summary(stats)
    verify_identity(computed)
    plot_alpha_traj(computed, outdir)
    plot_decay_decomposition(computed, outdir)
    plot_safe_zone_drift(computed, outdir)
    plot_miss_traj(computed, OPTIMS, outdir, "exp7_optims_miss_traj.png",
                   "Exp7: optimizer family miss-rate trajectory")
    plot_decay_step_contrib(stats, outdir)
    print("\nDone.")


if __name__ == "__main__":
    main()
