#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""stat_exp10_tradeoff.py -- Exp10: miss-吞吐 tradeoff 账本

对应 report.md §5.2.5「miss-吞吐 tradeoff 的定量账」:
  1. 兑换率表: 以后验 oracle fixed25 为基准, exchange = Δburn% / Δmiss(pp)
     —— 每付 1pp miss 换回多少 % 吞吐; 只有 AIMD 家族为正;
  2. 帕累托前沿图: burn-miss 平面全 21 模式散点 + 虚线前沿 + 红星 oracle
     (exp10_pareto_burn_vs_miss.png);
  3. 分相位账: ai 的 CPU 份额与 ctrl miss 按 L/H 段拆开, 验证
     「吞吐红利赚自 L 段冲高, miss 账单付在 H 段逗留」。

流式解析: 汇总用 schedlab_stat.compute_file; 分相位账用 parse_csv 回调
逐段算完即弃, 300 万行不驻内存。

用法: python3 stat_exp10_tradeoff.py <sexp10_all.csv>
"""
import os
import sys

import numpy as np
import matplotlib.pyplot as plt

from schedlab_stat import (
    RATIOS, RATIO_LABELS, RATIO_L_PCT,
    compute_file, aggregate_runs, parse_csv, phase_bounds_for_ratio,
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
MODE2FAM = {m: fam for fam, ms in FAMILIES for m in ms}
FAM_COLOR = {
    "fixed": "#64748b", "aimd": "#2563eb", "cubic": "#db2777",
    "optim": "#dc2626", "adamw": "#059669", "pid": "#0891b2", "ucb": "#7c3aed",
}
# 兑换率表只列自适应控制器(fixed 是静态基线本身, 不参与"交易")
ADAPTIVE_MODES = [m for fam, ms in FAMILIES if fam != "fixed" for m in ms]


# ---------------------------------------------------------------- 兑换率表
def print_exchange_table(stats):
    """exchange = Δburn% / Δmiss(pp), 基准 = 同 ratio 的 fixed25(后验 oracle)。

    Δburn% = (burn_mode / burn_fixed25 - 1) * 100
    Δmiss  = miss_mode - miss_fixed25            (百分点)
    exch > 0: 这笔交易赚了; exch <= 0: 双输(miss 更贵还倒贴吞吐)。
    """
    print("=" * 100)
    print("EXCHANGE TABLE (baseline = fixed25, 后验 oracle)")
    print("=" * 100)
    for ratio in RATIOS:
        base = stats.get((ratio, "fixed25"))
        if not base:
            continue
        b0, m0 = base["ai_burn_mean"], base["miss_rate_mean"]
        print(f"\n--- RATIO {RATIO_LABELS[ratio]} (L={RATIO_L_PCT[ratio]}%) "
              f"| fixed25: miss={m0:.1f}%  burn={b0:.0f} ---")
        print(f"{'mode':>9}  {'burn':>10}  {'Δburn%':>7}  {'miss%':>6}  "
              f"{'Δmiss(pp)':>9}  {'exch':>7}  评价")
        print("-" * 100)
        for mode in ADAPTIVE_MODES:
            s = stats.get((ratio, mode))
            if not s:
                continue
            db = (s["ai_burn_mean"] / b0 - 1.0) * 100.0
            dm = s["miss_rate_mean"] - m0
            exch = db / dm if abs(dm) > 1e-9 else float("nan")
            if db > 0 and dm <= 10:
                verdict = "划算交易"
            elif db <= 0:
                verdict = "双输"
            else:
                verdict = "miss 卖到不可用区"
            print(f"{mode:>9}  {s['ai_burn_mean']:>10.0f}  {db:>+7.1f}  "
                  f"{s['miss_rate_mean']:>6.1f}  {dm:>+9.1f}  {exch:>+7.2f}  {verdict}")


# ---------------------------------------------------------------- 帕累托前沿
def pareto_frontier(points):
    """burn-miss 平面的帕累托前沿(miss 越小越好, burn 越大越好)。

    按 burn 降序扫, 保留 miss 严格低于所有更高 burn 点的点(非支配解);
    返回前沿点(按 burn 升序)。扫错方向会把被支配的低 burn 点(如 fixed0)
    误收进前沿。
    """
    pts = sorted(points, key=lambda p: (-p[0], p[1]))
    front, best_miss = [], float("inf")
    for burn, miss, mode in pts:
        if miss < best_miss - 1e-12:
            front.append((burn, miss, mode))
            best_miss = miss
    return sorted(front)


def plot_pareto(stats, outdir):
    """burn-miss 平面: 全 21 模式散点(家族着色) + 虚线前沿 + 红星 fixed25。

    报告原话:「AIMD 家族贴着前沿, AdamW/UCB/PI 全部落在前沿左下方的被支配区」。
    """
    n = len(RATIOS)
    fig, axes = plt.subplots(1, n, figsize=(7 * n, 6))
    if n == 1:
        axes = [axes]
    for idx, ratio in enumerate(RATIOS):
        ax = axes[idx]
        points = []
        for mode in ALL_MODES:
            s = stats.get((ratio, mode))
            if not s:
                continue
            x, y = s["ai_burn_mean"], s["miss_rate_mean"]
            points.append((x, y, mode))
            ax.plot(x, y, "o", color=FAM_COLOR[MODE2FAM[mode]], ms=9,
                    markeredgecolor="black", markeredgewidth=0.8, zorder=3)
            ax.annotate(mode, (x, y), fontsize=7, ha="left", va="bottom",
                        xytext=(5, 3), textcoords="offset points", zorder=4)
        # 前沿虚线
        front = pareto_frontier(points)
        if front:
            fx = [p[0] for p in front]
            fy = [p[1] for p in front]
            ax.plot(fx, fy, "--", color="#0f172a", lw=1.4, alpha=0.7,
                    zorder=2, label="Pareto frontier")
        # 后验 oracle fixed25 红星
        s = stats.get((ratio, "fixed25"))
        if s:
            ax.plot(s["ai_burn_mean"], s["miss_rate_mean"], "*",
                    color="#dc2626", ms=20, markeredgecolor="black",
                    markeredgewidth=0.8, zorder=5,
                    label="fixed25 (posterior oracle)")
        for fam, c in FAM_COLOR.items():
            ax.plot([], [], "o", color=c, ms=8, label=fam)
        ax.set_title(f"Ratio {RATIO_LABELS[ratio]} (L={RATIO_L_PCT[ratio]}%)")
        ax.set_xlabel("ai_burn (iterations, same batch)")
        ax.set_ylabel("ctrl miss %")
        ax.legend(fontsize=8)
        ax.grid(True, alpha=0.3)
    fig.suptitle("Exp10: burn-miss Pareto frontier (same batch, 21 modes)",
                 fontsize=13)
    fig.tight_layout(rect=[0, 0, 1, 0.94])
    out = os.path.join(outdir, "exp10_pareto_burn_vs_miss.png")
    fig.savefig(out)
    print(f"[saved] {out}")
    plt.close(fig)
    # 前沿成员打印出来, 便于核对报告「前沿 = fixed25 ← aimd100 ← …」的断言
    for ratio in RATIOS:
        front = pareto_frontier([
            (stats[(ratio, m)]["ai_burn_mean"], stats[(ratio, m)]["miss_rate_mean"], m)
            for m in ALL_MODES if (ratio, m) in stats])
        chain = " <- ".join(m for _, _, m in front)
        print(f"[frontier] {RATIO_LABELS[ratio]}: {chain}")


# ---------------------------------------------------------------- 分相位账
def per_phase_stats(path):
    """分相位吞吐账: 每 run 算 ai 份额与 ctrl miss 的 L 段 / H 段拆分。

    L 段 = L1+L2(轻负载), H 段 = H1+H2(重负载)。
    份额 = ai run_delta / 全组 run_delta(同相位窗口内); miss = Σmiss/Σjobs。
    与 compute_run 一致跳过前 3 窗启动期。逐段关段即弃, 不驻留原始行。

    返回 {(ratio, mode): [(ai_sh_L, ai_sh_H, miss_L, miss_H), ...reps]}
    """
    acc = {}

    def handle(run):
        ratio, mode = run["ratio"], run["mode"]
        max_win = 0
        for w in run["W"]:
            max_win = max(max_win, w["win"])
        for d in run["D"]:
            max_win = max(max_win, d["win"])
        if max_win <= 0:
            return
        bounds = phase_bounds_for_ratio(ratio, max_win)

        def is_light(win):
            return bounds[0] <= win < bounds[1] or bounds[2] <= win < bounds[3]

        # W 行: 每窗各组 run_delta
        wl, wh = {}, {}   # win -> {name: run_delta}
        for w in run["W"]:
            if w["win"] <= 3:
                continue  # 启动期, 与 compute_run 一致
            tgt = wl if is_light(w["win"]) else wh
            tgt.setdefault(w["win"], {})[w["name"]] = \
                tgt.get(w["win"], {}).get(w["name"], 0) + w["run_delta"]

        def ai_share(tbl):
            ai = sum(v.get("ai", 0) for v in tbl.values())
            tot = sum(sum(v.values()) for v in tbl.values())
            return ai / tot * 100.0 if tot > 0 else 0.0

        # D 行: 每窗 ctrl jobs/miss
        jl = ml = jh = mh = 0
        for d in run["D"]:
            if is_light(d["win"]):
                jl += d["jobs"]; ml += d["miss"]
            else:
                jh += d["jobs"]; mh += d["miss"]
        miss_l = ml / jl * 100.0 if jl > 0 else 0.0
        miss_h = mh / jh * 100.0 if jh > 0 else 0.0

        acc.setdefault((ratio, mode), []).append(
            (ai_share(wl), ai_share(wh), miss_l, miss_h))

    parse_csv(path, on_run=handle)
    return acc


def print_phase_ledger(path):
    """分相位账: aimd50 vs fixed25(报告 §5.2.5 的 「L 段赚 / H 段付」画像)。

    25/25 目标值(report.md): aimd50 - fixed25 =
      L 段 ai 份额 +3.3pp(miss +3.2pp), H 段份额 -1.4pp(miss +5.8pp)。
    """
    print("\n" + "=" * 100)
    print("PHASE LEDGER: ai share / ctrl miss by phase (aimd50 vs fixed25)")
    print("=" * 100)
    acc = per_phase_stats(path)
    print(f"{'ratio':>7}  {'mode':>8}  {'shL%':>6}  {'shH%':>6}  "
          f"{'missL%':>7}  {'missH%':>7}")
    print("-" * 100)
    for ratio in RATIOS:
        for mode in ("fixed25", "aimd50"):
            reps = acc.get((ratio, mode))
            if not reps:
                continue
            a = np.mean([r[0] for r in reps]); b = np.mean([r[1] for r in reps])
            c = np.mean([r[2] for r in reps]); d = np.mean([r[3] for r in reps])
            print(f"{RATIO_LABELS[ratio]:>7}  {mode:>8}  {a:>6.1f}  {b:>6.1f}  "
                  f"{c:>7.1f}  {d:>7.1f}")
        fa = acc.get((ratio, "fixed25")); aa = acc.get((ratio, "aimd50"))
        if fa and aa:
            dsl = np.mean([r[0] for r in aa]) - np.mean([r[0] for r in fa])
            dsh = np.mean([r[1] for r in aa]) - np.mean([r[1] for r in fa])
            dml = np.mean([r[2] for r in aa]) - np.mean([r[2] for r in fa])
            dmh = np.mean([r[3] for r in aa]) - np.mean([r[3] for r in fa])
            print(f"{RATIO_LABELS[ratio]:>7}  {'Δ diff':>8}  {dsl:>+6.1f}  "
                  f"{dsh:>+6.1f}  {dml:>+7.1f}  {dmh:>+7.1f}")
        print("-" * 100)
    print("读法: Δsh_L > 0 = 轻载段冲高兑现的吞吐红利; "
          "Δmiss_H = 重载段逗留悬崖付的账单。")


def main():
    if len(sys.argv) < 2:
        print(f"Usage: {sys.argv[0]} <sexp10_all.csv>")
        sys.exit(1)
    path = sys.argv[1]
    outdir = os.path.dirname(path) or "."

    print(f"[streaming] parsing + computing {path} ...")
    computed = compute_file(path)
    print(f"[computed] {len(computed)} runs")
    stats = aggregate_runs(computed)
    del computed  # 分相位账走自己的流式通道, 汇总结果用完即弃

    global RATIOS
    present = sorted(set(r for (r, _) in stats.keys()))
    if present:
        RATIOS = [r for r in [500, 800, 200] if r in present] or present

    print_exchange_table(stats)
    plot_pareto(stats, outdir)
    print_phase_ledger(path)
    print("\nDone.")


if __name__ == "__main__":
    main()
