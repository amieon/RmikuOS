#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""schedlab_stat.py -- 调度实验统计/画图共享库 (exp6-9 复用)

自 stat_exp6.py 提取并扩展:
  - parse_csv: 通用 CSV 段解析(显式关段, 避免跨 RUN/rep 叠加)
  - A 行自适应: 4 列(cubic/pid)、8 列(adamw/optim, 含 loss/g/step/decay)、arm 格式(ucb)
  - compute_run / aggregate_runs
  - 相位边界 + 阴影 + 配色

关键约定:
  - 单核(每窗 run 总和 ≤100); run_ticks 每 tick 只给当前 running 线程 +1
  - ratio=500 与 exp4 的 ratio=0 负载等价(都是 25/25/25/25); exp4 数据无 ratio 字段,
    合并时用 default_ratio=500
  - A 行扩展格式: A,win,before,after,action,loss,g,step,decay (×1024 定点原值)
"""
import os
import re
from collections import defaultdict

import numpy as np
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

plt.rcParams.update({
    "font.family": "DejaVu Sans", "font.size": 11,
    "figure.dpi": 150, "savefig.dpi": 200,
    "savefig.bbox": "tight", "axes.grid": True, "grid.alpha": 0.3,
})

RATIOS = [500, 800, 200]
RATIO_LABELS = {500: "25/25", 800: "40/10", 200: "10/40"}
RATIO_L_PCT = {500: 50, 800: 80, 200: 20}
PHASE_NAMES = ["L1", "H1", "L2", "H2"]

COLORS = {
    "cubic0": "#059669", "cubic50": "#7c3aed", "cubic100": "#db2777",
    "sgdm": "#0891b2", "rmsprop": "#2563eb", "adagrad": "#dc2626", "adamw": "#059669",
    # adamw 三起点(exp10 统一矩阵): 绿色系深浅
    "adamw0": "#34d399", "adamw50": "#059669", "adamw100": "#065f46",
    "pid0": "#0891b2", "pid50": "#a855f7", "pid100": "#dc2626",
    "ucb": "#7c3aed",
    "aimd0": "#0891b2", "aimd50": "#2563eb", "aimd100": "#dc2626",
    "fixed0": "#cbd5e1", "fixed25": "#94a3b8", "fixed50": "#64748b",
    "fixed75": "#50545d", "fixed100": "#475569",
}


def color_of(mode):
    return COLORS.get(mode, "#666666")


def phase_bounds_for_ratio(ratio, max_win):
    """schedlab.h 相位实现: l_seg = half*ratio/1000, half = span/2。"""
    l_frac = ratio / 1000.0
    b1 = int(max_win * l_frac / 2)
    b2 = max_win // 2
    b3 = int(max_win * (0.5 + l_frac / 2))
    return [0, b1, b2, b3, max_win]


def add_phase_shading(ax, bounds, ymax=105):
    colors = ["#bbf7d0", "#fecaca", "#bbf7d0", "#fecaca"]
    for i in range(4):
        ax.axvspan(bounds[i], bounds[i + 1], color=colors[i], alpha=0.4, zorder=0)
    for b in bounds[1:-1]:
        ax.axvline(b, color="#333", ls="--", lw=1, alpha=0.6, zorder=1)
    for i, name in enumerate(PHASE_NAMES):
        mid = (bounds[i] + bounds[i + 1]) / 2
        ax.text(mid, ymax * 0.97, name, ha="center", va="top",
                fontsize=10, color="#333", fontweight="bold")


# ------------------------------------------------------------------ parse
def parse_csv(path, default_ratio=None, run_header_re=None, on_run=None):
    """解析一个 schedlab CSV 为 run 列表。每个 RUN 段一个 dict。

    显式关段: 遇到 # WARMUP / # done / 下一个 # RUN 都会结束当前段。
    A 行自适应: 存 win/before/after + extra(list)。
    on_run: 可选回调 —— 每段关段时调 on_run(run_dict) 而非 append 到返回
    列表(大 CSV 流式处理用, 见 compute_file)。"""
    runs, cur, in_run = [], None, False

    def flush():
        nonlocal cur, in_run
        if cur is not None and in_run:
            if on_run is not None:
                on_run(cur)
            else:
                runs.append(cur)
        cur, in_run = None, False

    with open(path) as f:
        for line in f:
            line = line.rstrip("\n")
            if not line:
                continue
            # 新 RUN 段
            m = re.search(r"# RUN ratio=(\d+) mode=(\w+) alpha0=(\d+) rep=(\d+)/\d+", line)
            if m:
                flush()
                cur = {"ratio": int(m.group(1)), "mode": m.group(2),
                       "alpha0": int(m.group(3)), "rep": int(m.group(4)),
                       "W": [], "D": [], "A": [], "S": [], "J": [], "K": []}
                in_run = True
                continue
            m = re.search(r"# RUN mode=(\w+) alpha0=(\d+) rep=(\d+)/\d+", line)
            if m:
                flush()
                cur = {"ratio": default_ratio if default_ratio is not None else 500,
                       "mode": m.group(1), "alpha0": int(m.group(2)),
                       "rep": int(m.group(3)),
                       "W": [], "D": [], "A": [], "S": [], "J": [], "K": []}
                in_run = True
                continue
            # 结束段
            if line.startswith("#"):
                if "WARMUP" in line or "done" in line:
                    flush()
                continue
            if not in_run or cur is None:
                continue

            p = line.split(",")
            tag = p[0]
            try:
                if tag == "W" and len(p) >= 8:
                    cur["W"].append({"win": int(p[1]), "alpha": int(p[2]), "name": p[4],
                                     "run_delta": int(p[5]), "eff_tickets": int(p[6]),
                                     "ready_threads": int(p[7])})
                elif tag == "D" and len(p) >= 6:
                    cur["D"].append({"win": int(p[1]), "jobs": int(p[3]),
                                     "miss": int(p[4]), "late": int(p[5])})
                elif tag == "A" and len(p) >= 4:
                    # before/after/extra 自适应: extra 可能是 [action] / [action,loss,g,step,decay] / [armN]
                    cur["A"].append({"win": int(p[1]), "before": int(p[2]),
                                     "after": int(p[3]), "extra": p[4:]})
                elif tag == "S" and len(p) >= 5:
                    cur["S"].append({"win": int(p[1]), "alpha": int(p[2]),
                                     "jain_q": int(p[3]), "max_slowdown_q": int(p[4])})
                elif tag == "J" and len(p) >= 8:
                    cur["J"].append({"name": p[2], "jobs": int(p[4]), "miss": int(p[5]),
                                     "late_sum": int(p[6]), "late_max": int(p[7])})
                elif tag == "K" and len(p) >= 5:
                    cur["K"].append({"name": p[2], "threads": int(p[3]), "work": int(p[4])})
            except (IndexError, ValueError):
                continue
    flush()
    return runs


def compute_file(path, default_ratio=None):
    """流式 parse+compute: 大 CSV(如 sexp10_all ~300 万行)不把全部原始行
    驻留内存 —— 每段关段时立即 compute_run 并丢弃原始 dict,
    内存只保留聚合所需的 computed 结果。"""
    out = []

    def _handle(run):
        out.append(compute_run(run))

    parse_csv(path, default_ratio=default_ratio, on_run=_handle)
    return out


# ------------------------------------------------------------------ compute
def compute_run(run):
    s = {"ratio": run["ratio"], "mode": run["mode"],
         "alpha0": run["alpha0"], "rep": run["rep"]}

    # ctrl miss 率(优先 J 行汇总; 否则 D 行累计)
    for j in run["J"]:
        if j["name"] == "ctrl":
            s["jobs"] = j["jobs"]; s["miss"] = j["miss"]
            s["miss_rate"] = j["miss"] / j["jobs"] * 100.0 if j["jobs"] > 0 else 0.0
            break
    else:
        jobs = sum(d["jobs"] for d in run["D"])
        miss = sum(d["miss"] for d in run["D"])
        s["jobs"] = jobs; s["miss"] = miss
        s["miss_rate"] = miss / jobs * 100.0 if jobs > 0 else 0.0

    # 份额(跳过前 3 窗启动期)
    ws = [w for w in run["W"] if w["win"] > 3]
    total = sum(w["run_delta"] for w in ws)
    s["share"] = {}
    for name in ("ctrl", "ai", "log"):
        t = sum(w["run_delta"] for w in ws if w["name"] == name)
        s["share"][name] = t / total * 100.0 if total > 0 else 0.0
    s["run"] = {name: sum(w["run_delta"] for w in ws if w["name"] == name)
                for name in ("ctrl", "ai", "log")}

    # ai 吞吐 = ai 组 work 总量(K 行 burn 迭代数; 旧脚本/readme 里叫 ai_burn)
    for k in run["K"]:
        if k["name"] == "ai":
            s["ai_burn"] = k["work"]

    # 逐窗口 miss 率 + ai run_delta(供分相位)
    if run["D"]:
        s["win_miss"] = np.array([
            d["miss"] / d["jobs"] * 100.0 if d["jobs"] > 0 else 0.0 for d in run["D"]])
    else:
        s["win_miss"] = np.array([])
    ai_ws = sorted([w for w in run["W"] if w["name"] == "ai"], key=lambda x: x["win"])
    s["win_ai_rd"] = [w["run_delta"] for w in ai_ws]
    s["win_ai_wins"] = [w["win"] for w in ai_ws]

    # alpha 轨迹: A 行 after 优先, A 行缺失/截尾的窗口用 S 行 alpha 补齐。
    # S 每窗口无条件输出(策略决策后打印), 对 aimd/cubic/pid/ucb 恒有
    # S.alpha == A.after; 仅 adamw/optim 的 S.alpha 是含 SPSA 扰动的
    # probe 值(±5), 只在 A 偶发缺失的窗口混入, 不影响趋势。
    # (A 行本体在策略里每窗无条件 printf, 缺失一般来自串口丢行/日志截尾)
    if run["A"] or run["S"]:
        amap = {a["win"]: a["after"] for a in run["A"]}
        smap = {x["win"]: x["alpha"] for x in run["S"]}
        wins = sorted(set(amap) | set(smap))
        s["alpha_traj"] = np.array(
            [amap.get(w, smap.get(w, np.nan)) for w in wins], dtype=float)
        s["alpha_wins"] = np.array(wins, dtype=int)

    if run["A"]:
        # action / arm / 分量 序列(按格式)
        s["actions"] = [a["extra"][0] if a["extra"] else "" for a in run["A"]]
        # up/down/hold 计数(从差分, 兼容 action 缺失; NaN 段比较为 False 自动跳过)
        tr = s["alpha_traj"]
        s["n_up"] = int(np.sum(np.diff(tr) > 0))
        s["n_down"] = int(np.sum(np.diff(tr) < 0))
        s["n_hold"] = int(np.sum(np.diff(tr) == 0))

        # 定理3 分量(8 列格式): extra = [action, loss, g, step, decay]
        if run["A"] and len(run["A"][0]["extra"]) >= 5:
            s["loss"] = np.array([int(a["extra"][1]) for a in run["A"]])
            s["g"] = np.array([int(a["extra"][2]) for a in run["A"]])
            s["step"] = np.array([int(a["extra"][3]) for a in run["A"]])
            s["decay"] = np.array([int(a["extra"][4]) for a in run["A"]])
            s["sum_step"] = int(s["step"].sum())
            s["sum_decay"] = int(s["decay"].sum())
        # UCB arm 序列(extra=["armN"])
        if run["A"] and run["A"][0]["extra"] and run["A"][0]["extra"][0].startswith("arm"):
            s["arms"] = [int(a["extra"][0][3:]) for a in run["A"]]
    else:
        # 无 A 行(如 fixed 模式): alpha_traj 已由上面 A∪S 合并给出(S 行),
        # 这里只清 action 统计, 不能抹掉轨迹(否则 fixed 的 α_steady 恒 0)
        s["actions"] = []
        s["n_up"] = s["n_down"] = s["n_hold"] = 0

    # 稳态 alpha = 后半段均值(自适应控制器才有意义; nanmean 跳过补不出的窗)
    if len(s["alpha_traj"]) > 10:
        half = len(s["alpha_traj"]) // 2
        s["alpha_steady"] = float(np.nanmean(s["alpha_traj"][half:]))
    else:
        s["alpha_steady"] = 0.0

    # Jain(偏差版, 均值 /1000)
    if run["S"]:
        s["jain"] = float(np.mean([x["jain_q"] for x in run["S"]]) / 1000.0)
    else:
        s["jain"] = 0.0

    return s


def aggregate_runs(computed):
    by_group = defaultdict(list)
    for r in computed:
        by_group[(r["ratio"], r["mode"])].append(r)

    stats = {}
    for (ratio, mode), reps in by_group.items():
        row = {"ratio": ratio, "mode": mode, "nreps": len(reps)}
        for f in ("miss_rate", "jain", "alpha_steady", "ai_burn"):
            vals = [r.get(f, 0) for r in reps]
            m = float(np.mean(vals))
            row[f"{f}_mean"] = m
            row[f"{f}_hi"] = max(vals) - m
            row[f"{f}_lo"] = m - min(vals)
        for f in ("n_up", "n_down", "n_hold"):
            row[f"{f}_mean"] = float(np.mean([r.get(f, 0) for r in reps]))
        # 每组 run(CPU ticks) / share(份额%) 聚合 —— 供需要 ai_run/ai_share 的脚本用
        for name in ("ai", "ctrl", "log"):
            if all("run" in r and name in r.get("run", {}) for r in reps):
                row[f"run_{name}_mean"] = float(np.mean([r["run"][name] for r in reps]))
            if all("share" in r and name in r.get("share", {}) for r in reps):
                row[f"share_{name}_mean"] = float(np.mean([r["share"][name] for r in reps]))
        if all("sum_step" in r for r in reps):
            row["sum_step_mean"] = float(np.mean([r["sum_step"] for r in reps]))
            row["sum_decay_mean"] = float(np.mean([r["sum_decay"] for r in reps]))
        stats[(ratio, mode)] = row
    return stats


def fmt_err(mean, hi, lo, wm=7, we=5):
    return f"{mean:>{wm}.1f} +{hi:>{we}.1f}/-{lo:>{we}.1f}"


# ------------------------------------------------------------------ 通用图
def plot_miss_traj(computed, modes, outdir, filename, title,
                   ratios=None, ratio_labels=None):
    """通用 miss 率逐窗口轨迹图: 每 ratio 一个子图, 多 mode 对比 + 相位阴影。

    这是最直观的"控制器怎么响应负载变化"图——miss 率随相位 L/H 起落。"""
    rs = ratios if ratios is not None else RATIOS
    labels = ratio_labels or RATIO_LABELS
    n = len(rs)
    fig, axes = plt.subplots(1, n, figsize=(6 * n, 5))
    if n == 1:
        axes = [axes]
    for idx, ratio in enumerate(rs):
        ax = axes[idx]
        maxw = 0
        for mode in modes:
            reps = [r for r in computed if r["ratio"] == ratio and r["mode"] == mode
                    and len(r.get("win_miss", [])) > 0]
            if not reps:
                continue
            # NaN 填充到最长 rep: 短 rep 只造成断点, 不再截断整条均值曲线
            maxlen = max(len(r["win_miss"]) for r in reps)
            aligned = np.full((len(reps), maxlen), np.nan)
            for i, r in enumerate(reps):
                aligned[i, :len(r["win_miss"])] = r["win_miss"]
            ax.plot(np.arange(maxlen), np.nanmean(aligned, axis=0),
                    color=color_of(mode), lw=1.5, label=mode)
            maxw = max(maxw, maxlen)
        if maxw > 0:
            add_phase_shading(ax, phase_bounds_for_ratio(ratio, maxw), 105)
        ax.set_title(f"Ratio {labels.get(ratio, ratio)}")
        ax.set_xlabel("Window"); ax.set_ylabel("ctrl miss %")
        ax.set_ylim(-2, 105); ax.legend(fontsize=7)
    fig.suptitle(title, fontsize=13)
    fig.tight_layout(rect=[0, 0, 1, 0.95])
    out = os.path.join(outdir, filename)
    fig.savefig(out); print(f"[saved] {out}"); plt.close(fig)
