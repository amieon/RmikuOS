#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""plot_exp10_adamw_decomp.py -- Exp10: AdamW 分量分解四联时序图

对应 report.md §6.3/§6.7:「AdamW 的 decay 是全场唯一不依赖梯度的力」。
取 adamw50 @ 25/25 rep1 单 run, 把 A 行的定点分量直接作图:
  loss (原始值, miss_per_1000 + late 项, 非定点)
  |g| / |step| / |decay| (A 行 ×1024 定点原值 / 1024)

看点(report.md 图注): H 段 loss 冒尖、|g| 与 |step| 跟随发力; L 段三者
集体消失, 只剩 decay(绿)全程存活 —— 「梯度消失时不消失的力」的单 run 特写。

用法: python3 plot_exp10_adamw_decomp.py <sexp10_all.csv>
          [--mode adamw50] [--ratio 500] [--rep 1]
"""
import argparse
import os

import numpy as np
import matplotlib.pyplot as plt

from schedlab_stat import (
    RATIO_LABELS, RATIO_L_PCT, parse_csv,
    phase_bounds_for_ratio, add_phase_shading,
)


def pick_run(path, mode, ratio, rep):
    """流式扫描, 只保留目标 run 的 A 行(8 列分量格式)。"""
    found = {}

    def handle(run):
        if (run["mode"], run["ratio"], run["rep"]) == (mode, ratio, rep):
            if run["A"] and len(run["A"][0]["extra"]) >= 5:
                found["A"] = run["A"]

    parse_csv(path, on_run=handle)
    return found.get("A")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("csv")
    ap.add_argument("--mode", default="adamw50")
    ap.add_argument("--ratio", type=int, default=500)
    ap.add_argument("--rep", type=int, default=1)
    args = ap.parse_args()

    rows = pick_run(args.csv, args.mode, args.ratio, args.rep)
    if not rows:
        print(f"[error] 没找到 {args.mode} ratio={args.ratio} rep={args.rep} "
              f"的 8 列 A 行(分量格式仅 adamw/optim 家族输出)")
        sys.exit(1)

    wins  = np.array([a["win"] for a in rows])
    loss  = np.array([int(a["extra"][1]) for a in rows], dtype=float)
    g     = np.array([int(a["extra"][2]) for a in rows], dtype=float) / 1024.0
    step  = np.array([int(a["extra"][3]) for a in rows], dtype=float) / 1024.0
    decay = np.array([int(a["extra"][4]) for a in rows], dtype=float) / 1024.0
    max_win = int(wins.max())
    bounds = phase_bounds_for_ratio(args.ratio, max_win)

    panels = [
        (loss,          "#0f172a", "loss (miss_per_1000 + late, raw)"),
        (np.abs(g),     "#2563eb", "|g|  (SPSA gradient, fp/1024)"),
        (np.abs(step),  "#dc2626", "|step|  (AdamW update, fp/1024)"),
        (decay,         "#059669", "decay  (weight-decay pull, fp/1024)"),
    ]
    fig, axes = plt.subplots(4, 1, figsize=(13, 10), sharex=True)
    for ax, (series, color, label) in zip(axes, panels):
        ax.plot(wins, series, color=color, lw=0.9)
        add_phase_shading(ax, bounds, ymax=float(np.nanmax(series)) * 1.15 + 1e-9)
        ax.set_ylabel(label.split("(")[0].strip(), fontsize=9)
        ax.legend([label], fontsize=8, loc="upper right")
    axes[-1].set_xlabel("Window")
    axes[-1].set_xlim(0, max_win)
    fig.suptitle(
        f"Exp10 [{args.mode} @{RATIO_LABELS.get(args.ratio, args.ratio)} "
        f"(L={RATIO_L_PCT.get(args.ratio, '?')}%) rep{args.rep}]: "
        "AdamW component decomposition - only decay survives in L phases", fontsize=13)
    fig.tight_layout(rect=[0, 0, 1, 0.96])

    outdir = os.path.dirname(args.csv) or "."
    out = os.path.join(outdir, "exp10_adamw_decomp.png")
    fig.savefig(out)
    print(f"[saved] {out}")
    plt.close(fig)

    # 分量恒等式离线重放自检(report.md §6.3: 2204 窗失配 0)
    # 窗口 t+1 的更新基于窗口 t 的 after: after[t+1] ?= after[t] - step[t+1] + decay[t+1]
    # after 是整数 α, step/decay 是 ×1024 定点; 整数化有 ±1 的 floor 容差。
    after = np.array([a["after"] for a in rows], dtype=float)
    step_raw = np.array([int(a["extra"][3]) for a in rows], dtype=float)
    decay_raw = np.array([int(a["extra"][4]) for a in rows], dtype=float)
    pred = (after[:-1] * 1024.0 - step_raw[1:] + decay_raw[1:]) / 1024.0
    diff = np.abs(np.round(pred) - after[1:])
    mism = int(np.sum(diff > 1))
    print(f"[check] 分量恒等式 α(t+1)=α(t)−step+decay 离线重放: "
          f"{len(after) - 1} 窗, 失配(>±1) {mism}")


if __name__ == "__main__":
    main()
