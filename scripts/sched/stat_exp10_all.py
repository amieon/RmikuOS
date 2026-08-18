#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""stat_exp10_all.py -- Exp10: 统一矩阵同批重跑 (21 模式 × 3 ratios, ~300 万行 CSV)

同批的意义: ai_burn 跨批次不可比(依赖主机速度), 只有同批内"方法 vs 方法"
的对比才可信(sexp7 排查实证: 旧 adamw ~1M vs 新 ~800k 是漂移不是差异)。
本脚本的 trade-off 散点图(ai_burn vs miss)是五流派对比的最终交付图。

流式解析: 用 schedlab_stat.compute_file 逐段 compute, 300 万行不驻内存。

用法: python3 stat_exp10_all.py <sexp10_all.csv>
"""
import sys
import os

import numpy as np
import matplotlib.pyplot as plt

from schedlab_stat import (
    RATIOS, RATIO_LABELS, RATIO_L_PCT, color_of, add_phase_shading,
    phase_bounds_for_ratio, compute_file, aggregate_runs, fmt_err,
    plot_miss_traj,
)

FAMILIES = [
    ("fixed", ["fixed0", "fixed25", "fixed50", "fixed75", "fixed100"]),
    ("aimd",  ["aimd0", "aimd50", "aimd100"]),
    ("cubic", ["cubic0", "cubic50", "cubic100"]),
    ("optim", ["sgdm", "rmsprop", "adagrad"]),
    ("adamw", ["adamw0", "adamw50", "adamw100"]),
    ("pid",   ["pid0", "pid50", "pid100"]),
    ("ucb",   ["ucb"]),
]
ALL_MODES = [m for _, ms in FAMILIES for m in ms]
# 同起点 α0=50 的五流派对照(探索/先验轴) —— sgdm/rmsprop/adagrad/ucb 本来就是
# 单起点 50, 其余取各自的 50 变体
MID_MODES = ["aimd50", "cubic50", "sgdm", "rmsprop", "adagrad",
             "adamw50", "pid50", "ucb"]

FAM_COLOR = {
    "fixed": "#64748b", "aimd": "#2563eb", "cubic": "#db2777",
    "optim": "#dc2626", "adamw": "#059669", "pid": "#0891b2", "ucb": "#7c3aed",
}
MODE2FAM = {m: fam for fam, ms in FAMILIES for m in ms}


def print_summary(stats):
    print("=" * 118)
    print("EXPERIMENT 10: UNIFIED MATRIX (same batch, 21 modes)")
    print("=" * 118)
    for ratio in RATIOS:
        print(f"\n--- RATIO {RATIO_LABELS[ratio]} (L={RATIO_L_PCT[ratio]}%) ---")
        print(f"{'mode':>9}  {'miss%':>18}  {'sh_ai':>7}  {'ai_burn':>9}  "
              f"{'ai_run':>9}  {'burn/run':>8}  {'α_steady':>8}")
        print("-" * 118)
        for mode in ALL_MODES:
            s = stats.get((ratio, mode))
            if not s:
                continue
            ai_run = s.get('run_ai_mean', 0)
            ai_burn = s.get('ai_burn_mean', 0)
            br = ai_burn / ai_run if ai_run > 0 else 0.0
            print(f"{mode:>9}  "
                  f"{fmt_err(s.get('miss_rate_mean',0), s.get('miss_rate_hi',0), s.get('miss_rate_lo',0))}  "
                  f"{s.get('share_ai_mean',0):>7.1f}  {ai_burn:>9.0f}  {ai_run:>9.0f}  "
                  f"{br:>8.2f}  {s.get('alpha_steady_mean',0):>8.1f}")

    # 同起点五流派对照(探索/先验轴)
    print("\n" + "=" * 78)
    print("同起点 α0=50 五流派对照 (exploration/prior axis)")
    print("=" * 78)
    for ratio in RATIOS:
        print(f"\n--- RATIO {RATIO_LABELS[ratio]} ---")
        print(f"{'mode':>9}  {'miss%':>18}  {'α_steady':>8}  {'ai_burn':>9}")
        print("-" * 78)
        for mode in MID_MODES:
            s = stats.get((ratio, mode))
            if not s:
                continue
            print(f"{mode:>9}  "
                  f"{fmt_err(s.get('miss_rate_mean',0), s.get('miss_rate_hi',0), s.get('miss_rate_lo',0))}  "
                  f"{s.get('alpha_steady_mean',0):>8.1f}  {s.get('ai_burn_mean',0):>9.0f}")


def plot_steady_bars(stats, outdir):
    """每个 mode 的稳态 α 柱状图(按家族着色)——'每个控制器停在哪'总览。"""
    n = len(RATIOS)
    fig, axes = plt.subplots(1, n, figsize=(13, 5))
    if n == 1:
        axes = [axes]
    for idx, ratio in enumerate(RATIOS):
        ax = axes[idx]
        xs, ys, cs, labels = [], [], [], []
        for i, mode in enumerate(ALL_MODES):
            s = stats.get((ratio, mode))
            if not s:
                continue
            xs.append(i); ys.append(s.get('alpha_steady_mean', 0))
            cs.append(FAM_COLOR[MODE2FAM[mode]]); labels.append(mode)
        ax.bar(xs, ys, color=cs, edgecolor="black", linewidth=0.4)
        ax.set_xticks(xs)
        ax.set_xticklabels(labels, rotation=90, fontsize=7)
        ax.axhspan(25, 50, color="#fde68a", alpha=0.4, label="critical band")
        ax.set_title(f"Ratio {RATIO_LABELS[ratio]} (L={RATIO_L_PCT[ratio]}%)")
        ax.set_ylabel("α_steady"); ax.set_ylim(0, 105)
        ax.legend(fontsize=7, loc="upper left")
    fig.suptitle("Exp10: steady-state α by mode (same batch, colored by family)", fontsize=13)
    fig.tight_layout(rect=[0, 0, 1, 0.94])
    out = os.path.join(outdir, "exp10_steady_bars.png")
    fig.savefig(out); print(f"[saved] {out}"); plt.close(fig)


def plot_mid_traj(computed, outdir):
    """同起点 α0=50 的 8 个控制器 α 轨迹(每 ratio)——'同起点不同命运'。"""
    n = len(RATIOS)
    fig, axes = plt.subplots(1, n, figsize=(6 * n, 5))
    if n == 1:
        axes = [axes]
    for idx, ratio in enumerate(RATIOS):
        ax = axes[idx]
        max_w = 0
        for mode in MID_MODES:
            reps = [r for r in computed if r["ratio"] == ratio and r["mode"] == mode
                    and len(r.get("alpha_traj", [])) > 0]
            if not reps:
                continue
            maxlen = max(len(r["alpha_traj"]) for r in reps)
            aligned = np.full((len(reps), maxlen), np.nan)
            for i, r in enumerate(reps):
                aligned[i, :len(r["alpha_traj"])] = r["alpha_traj"]
            ax.plot(np.arange(maxlen), np.nanmean(aligned, axis=0),
                    color=color_of(mode), lw=1.8, label=mode)
            max_w = max(max_w, int(max(r["alpha_wins"].max() for r in reps
                                       if len(r["alpha_wins"]) > 0)))
        if max_w > 0:
            add_phase_shading(ax, phase_bounds_for_ratio(ratio, max_w), 105)
        ax.set_title(f"Ratio {RATIO_LABELS[ratio]} (L={RATIO_L_PCT[ratio]}%)")
        ax.set_xlabel("Window"); ax.set_ylabel("α"); ax.set_ylim(-2, 105)
        ax.legend(fontsize=7, ncol=2)
    fig.suptitle("Exp10: same start (α0=50), different controllers", fontsize=13)
    fig.tight_layout(rect=[0, 0, 1, 0.94])
    out = os.path.join(outdir, "exp10_alpha_traj_mid.png")
    fig.savefig(out); print(f"[saved] {out}"); plt.close(fig)


def plot_tradeoff(stats, outdir):
    """同批 trade-off 前沿: ai_burn vs miss 散点(按家族着色)——最终交付图。"""
    n = len(RATIOS)
    fig, axes = plt.subplots(1, n, figsize=(7 * n, 6))
    if n == 1:
        axes = [axes]
    for idx, ratio in enumerate(RATIOS):
        ax = axes[idx]
        for mode in ALL_MODES:
            s = stats.get((ratio, mode))
            if not s:
                continue
            x = s.get('ai_burn_mean', 0)
            y = s.get('miss_rate_mean', 0)
            ax.plot(x, y, "o", color=FAM_COLOR[MODE2FAM[mode]], ms=9,
                    markeredgecolor="black", markeredgewidth=0.8)
            ax.annotate(mode, (x, y), fontsize=7, ha="left", va="bottom",
                        xytext=(5, 3), textcoords="offset points")
        # 家族图例(每家族一个 proxy 点)
        for fam, c in FAM_COLOR.items():
            ax.plot([], [], "o", color=c, ms=8, label=fam)
        ax.set_title(f"Ratio {RATIO_LABELS[ratio]} (L={RATIO_L_PCT[ratio]}%)")
        ax.set_xlabel("ai_burn (iterations, same batch)")
        ax.set_ylabel("ctrl miss %")
        ax.legend(fontsize=8)
        ax.grid(True, alpha=0.3)
    fig.suptitle("Exp10: same-batch trade-off frontier (all 21 modes)", fontsize=13)
    fig.tight_layout(rect=[0, 0, 1, 0.94])
    out = os.path.join(outdir, "exp10_tradeoff_scatter.png")
    fig.savefig(out); print(f"[saved] {out}"); plt.close(fig)


def plot_family_alpha_traj(computed, outdir):
    """每家族一张 alpha 轨迹图: 同族各 mode 在 3 ratios 下的 alpha(w) 叠加(相位阴影)。

    看点: 同族不同起点是否殊途同归(aimd/adamw), 还是分道扬镳(pid 双吸引子)。
    """
    for fam, modes in FAMILIES:
        if fam == "fixed":
            continue  # fixed 的 alpha 是水平线, 无信息量
        n = len(RATIOS)
        fig, axes = plt.subplots(1, n, figsize=(5.5 * n, 4.2))
        if n == 1:
            axes = [axes]
        for idx, ratio in enumerate(RATIOS):
            ax = axes[idx]
            max_w = 0
            for mode in modes:
                reps = [r for r in computed if r["ratio"] == ratio and r["mode"] == mode
                        and len(r.get("alpha_traj", [])) > 0]
                if not reps:
                    continue
                maxlen = max(len(r["alpha_traj"]) for r in reps)
                aligned = np.full((len(reps), maxlen), np.nan)
                for i, r in enumerate(reps):
                    aligned[i, :len(r["alpha_traj"])] = r["alpha_traj"]
                ax.plot(np.arange(maxlen), np.nanmean(aligned, axis=0),
                        color=color_of(mode), lw=1.8, label=mode)
                max_w = max(max_w, int(max(r["alpha_wins"].max() for r in reps
                                           if len(r["alpha_wins"]) > 0)))
            if max_w > 0:
                add_phase_shading(ax, phase_bounds_for_ratio(ratio, max_w), 105)
            ax.set_title(f"Ratio {RATIO_LABELS[ratio]} (L={RATIO_L_PCT[ratio]}%)")
            ax.set_xlabel("Window"); ax.set_ylabel("\u03b1"); ax.set_ylim(-2, 105)
            ax.legend(fontsize=8)
        fig.suptitle(f"Exp10 [{fam}]: \u03b1 trajectory (mean over reps)", fontsize=13)
        fig.tight_layout(rect=[0, 0, 1, 0.93])
        out = os.path.join(outdir, f"exp10_fam_{fam}_alpha_traj.png")
        fig.savefig(out); print(f"[saved] {out}"); plt.close(fig)


def plot_family_burn_vs_miss(stats, outdir):
    """每家族一张吞吐-miss 二维图: ai_burn vs miss% (3 ratios x 同族各 mode)。

    fixed25 以 x 基准线入图 —— 静态后验最优 miss 的参考, 自适应值不值一眼可见。
    """
    for fam, modes in FAMILIES:
        n = len(RATIOS)
        fig, axes = plt.subplots(1, n, figsize=(5.5 * n, 4.4))
        if n == 1:
            axes = [axes]
        for idx, ratio in enumerate(RATIOS):
            ax = axes[idx]
            ref = stats.get((ratio, "fixed25"))
            if ref:
                ax.axvline(ref.get("miss_rate_mean", 0), color="#94a3b8",
                           ls="--", lw=1, label="fixed25 miss")
            for mode in modes:
                s = stats.get((ratio, mode))
                if not s:
                    continue
                x = s.get("miss_rate_mean", 0)
                y = s.get("ai_burn_mean", 0)
                ax.plot(x, y, "o", ms=10, color=color_of(mode),
                        markeredgecolor="black", markeredgewidth=0.8)
                ax.annotate(mode, (x, y), fontsize=8, ha="left", va="bottom",
                            xytext=(5, 3), textcoords="offset points")
            ax.set_title(f"Ratio {RATIO_LABELS[ratio]} (L={RATIO_L_PCT[ratio]}%)")
            ax.set_xlabel("ctrl miss %"); ax.set_ylabel("ai_burn (iterations)")
            ax.grid(True, alpha=0.3)
            ax.legend(fontsize=8)
        fig.suptitle(f"Exp10 [{fam}]: throughput vs miss (same batch)", fontsize=13)
        fig.tight_layout(rect=[0, 0, 1, 0.93])
        out = os.path.join(outdir, f"exp10_fam_{fam}_burn_vs_miss.png")
        fig.savefig(out); print(f"[saved] {out}"); plt.close(fig)



def main():
    if len(sys.argv) < 2:
        print(f"Usage: {sys.argv[0]} <sexp10_all.csv>")
        sys.exit(1)
    path = sys.argv[1]
    print(f"[streaming] parsing + computing {path} ...")
    computed = compute_file(path)
    print(f"[computed] {len(computed)} runs from {os.path.basename(path)}")
    outdir = os.path.dirname(path) or "."
    os.makedirs(outdir, exist_ok=True)

    stats = aggregate_runs(computed)

    global RATIOS
    present = sorted(set(r["ratio"] for r in computed))
    if present:
        RATIOS = [r for r in [500, 800, 200] if r in present] or present

    print_summary(stats)
    plot_steady_bars(stats, outdir)
    plot_mid_traj(computed, outdir)
    plot_tradeoff(stats, outdir)
    plot_miss_traj(computed, ["fixed25", "aimd50", "cubic50", "adamw50", "pid50", "ucb"],
                   outdir, "exp10_miss_traj.png",
                   "Exp10: miss-rate trajectory (mid-start modes)", ratios=RATIOS)
    plot_family_alpha_traj(computed, outdir)
    plot_family_burn_vs_miss(stats, outdir)
    print("\nDone.")


if __name__ == "__main__":
    main()
