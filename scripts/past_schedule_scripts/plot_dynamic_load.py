#!/usr/bin/env python3
"""
动态负载实验对比图：AIMD vs 固定 alpha，在「轻→重→轻」三段负载下。

支持多 repeat：同一策略（同 initial_alpha + 同 fixed/adaptive）出现多次时自动聚合。

读 dynamic_load_exp 的 raw 日志（每个 repeat 含 1 条 adaptive + 若干 fixed，同负载）。

产出：
  图1 (三子图) dynamic_load_alpha_miss_work.png:
    上：alpha 轨迹。多 repeat 时 AIMD 画各 repeat 半透明叠加 + 第一条加粗
        （不对 alpha 求平均——退避时机每次不同，平均会把锯齿抹成虚假平滑曲线）。
        fixed 是水平线，直接画。
    中：control 累计 miss 随窗口。多 repeat 求逐窗口均值 + min/max 误差带。
    下：累计 AI work 随窗口（*近似*，见下）。多 repeat 求均值 + 误差带。
  图2 (散点) dynamic_load_tradeoff.png:
    x=总 miss, y=总 AI work。多 repeat 求均值，画 min/max 误差棒。
    左上 = 低 miss + 高吞吐 = 占优区。

并打印成绩单（多 repeat 显示 mean±range）。

注意：AI work 在日志里只有最终总数，没有逐窗口值。第三子图的累计 work
是按各窗口 AI 活跃度加权的*近似*分配，曲线终点等于真实总 work。

用法：python3 plot_dynamic_load.py <raw.log> [out_dir]
"""
import os
import re
import sys
from collections import defaultdict

import matplotlib.pyplot as plt

RUN_RE = re.compile(r"\[adaptive_alpha\]\s+run\s+initial_alpha=(?P<ia>\d+)")
WIN_RE = re.compile(
    r"\[adaptive_window\]\s+window=(?P<w>\d+)\s+alpha_before=\d+"
    r"\s+alpha_after=(?P<aa>\d+).*?jobs=(?P<jobs>\d+)\s+miss=(?P<miss>\d+).*?action=(?P<act>\S+)"
)
PHASE_RE = re.compile(r"\[load_phase\]\s+window=(?P<w>\d+)\s+phase=(?P<p>\d+)")
CTRL_RE = re.compile(
    r"\[edge_deadline\]\s+alpha=\d+\s+role=control_result.*?jobs=(?P<jobs>\d+)\s+miss=(?P<miss>\d+)"
    r"(?:.*?lateness_max=(?P<lmax>\d+))?"
)
AI_RE = re.compile(r"\[edge_deadline\]\s+alpha=\d+\s+role=ai_result.*?work=(?P<work>\d+)")
FINAL_RE = re.compile(r"\[adaptive_alpha\]\s+final_alpha=(?P<fa>\d+)")


def parse(path):
    runs = []
    cur = None
    with open(path, encoding="utf-8", errors="ignore") as f:
        for line in f:
            m = RUN_RE.search(line)
            if m:
                if cur:
                    runs.append(cur)
                cur = {"initial_alpha": int(m.group("ia")), "win": [], "phase": {},
                       "is_fixed": False, "miss": None, "jobs": None,
                       "lmax": None, "ai_work": None, "final": None}
                continue
            if cur is None:
                continue
            m = WIN_RE.search(line)
            if m:
                if m.group("act") == "fixed_hold":
                    cur["is_fixed"] = True
                cur["win"].append({"w": int(m.group("w")), "alpha": int(m.group("aa")),
                                   "miss": int(m.group("miss"))})
                continue
            m = PHASE_RE.search(line)
            if m:
                cur["phase"][int(m.group("w"))] = int(m.group("p"))
                continue
            m = CTRL_RE.search(line)
            if m:
                cur["jobs"] = int(m.group("jobs"))
                cur["miss"] = int(m.group("miss"))
                cur["lmax"] = int(m.group("lmax")) if m.group("lmax") else 0
                continue
            m = AI_RE.search(line)
            if m:
                cur["ai_work"] = int(m.group("work"))
                continue
            m = FINAL_RE.search(line)
            if m:
                cur["final"] = int(m.group("fa"))
    if cur:
        runs.append(cur)
    # 只保留拿到 control 结果的完整 run
    return [r for r in runs if r["miss"] is not None and r["ai_work"] is not None]


def strat_key(run):
    """策略标识：fixed 按 alpha 分；adaptive 单独一类。"""
    if run["is_fixed"]:
        return ("fixed", run["initial_alpha"])
    return ("aimd", None)


def strat_label(key):
    kind, a = key
    if kind == "fixed":
        return f"fixed alpha={a}"
    return "AIMD (adaptive)"


def phase_spans(phase_map):
    if not phase_map:
        return []
    ws = sorted(phase_map)
    spans = []
    s = ws[0]
    cur_p = phase_map[s]
    prev = s
    for w in ws[1:]:
        if phase_map[w] != cur_p:
            spans.append((s, prev, cur_p))
            s = w
            cur_p = phase_map[w]
        prev = w
    spans.append((s, prev, cur_p))
    return spans


def cum_miss_series(run):
    ws, cum, acc = [], [], 0
    for d in run["win"]:
        acc += d["miss"]
        ws.append(d["w"])
        cum.append(acc)
    return ws, cum


def approx_cumwork(run, phase_map):
    total = run["ai_work"] or 0
    ws = [d["w"] for d in run["win"]]
    weights = [1.0 if phase_map.get(w, 0) == 1 else 0.2 for w in ws]
    s = sum(weights) or 1.0
    cum, acc = [], 0.0
    for wt in weights:
        acc += total * wt / s
        cum.append(acc)
    return ws, cum


def mean_band(list_of_series):
    """多条等长(按最短对齐)序列 -> (mean, lo, hi) 逐点。"""
    if not list_of_series:
        return [], [], []
    n = min(len(s) for s in list_of_series)
    mean, lo, hi = [], [], []
    for i in range(n):
        vals = [s[i] for s in list_of_series]
        mean.append(sum(vals) / len(vals))
        lo.append(min(vals))
        hi.append(max(vals))
    return mean, lo, hi


def _dedup_legend(ax, loc="best"):
    """同名 label 只在图例出现一次。"""
    handles, labels = ax.get_legend_handles_labels()
    seen = {}
    for h, l in zip(handles, labels):
        if l not in seen:
            seen[l] = h
    ax.legend(seen.values(), seen.keys(), loc=loc, fontsize=8)


# 固定一组备选色，按 alpha 值稳定映射（同一 alpha 永远同色）。
FIXED_PALETTE = ["tab:green", "tab:orange", "tab:purple", "tab:brown",
                 "tab:olive", "tab:cyan"]


def fixed_color_for(alpha, all_fixed_alphas):
    """按 alpha 在所有 fixed alpha 排序中的位置取色，保证同 alpha 同色、可复现。"""
    order = sorted(all_fixed_alphas)
    idx = order.index(alpha) if alpha in order else 0
    return FIXED_PALETTE[idx % len(FIXED_PALETTE)]


def plot_three_panel(grouped, spans, phase_ref, out_dir):
    fig, (ax1, ax2, ax3) = plt.subplots(3, 1, figsize=(10, 11), sharex=True)
    for s, e, p in spans:
        if p == 1:
            for ax in (ax1, ax2, ax3):
                ax.axvspan(s - 0.5, e + 0.5, color="tab:red", alpha=0.07, zorder=0)

    # 收集所有 fixed 的 alpha，用于稳定配色
    all_fixed_alphas = [k[1] for k in grouped if k[0] == "fixed"]

    # 固定策略按 alpha 排序，AIMD 最后画（在最上层）
    keys = sorted(grouped, key=lambda k: (k[0] == "aimd", k[1] if k[1] else 0))
    for key in keys:
        runs = grouped[key]
        lab = strat_label(key)
        is_aimd = key[0] == "aimd"
        color = "tab:red" if is_aimd else fixed_color_for(key[1], all_fixed_alphas)

        # --- 上：alpha 轨迹 ---
        if is_aimd:
            # 多 repeat：半透明叠加，第一条加粗代表
            for i, run in enumerate(runs):
                ws = [d["w"] for d in run["win"]]
                al = [d["alpha"] for d in run["win"]]
                if i == 0:
                    ax1.plot(ws, al, "-", color="tab:red", lw=2.6, zorder=5,
                             label=lab + (f" (n={len(runs)})" if len(runs) > 1 else ""))
                else:
                    ax1.plot(ws, al, "-", color="tab:red", lw=1.0, alpha=0.35, zorder=4)
        else:
            run = runs[0]  # fixed 各 repeat alpha 都一样，画一条即可
            ws = [d["w"] for d in run["win"]]
            al = [d["alpha"] for d in run["win"]]
            ax1.plot(ws, al, "--", color=color, lw=1.8, alpha=0.9, label=lab)

        # --- 中：累计 miss（多 repeat 求均值+带）---
        series = [cum_miss_series(r)[1] for r in runs]
        ref_ws = cum_miss_series(runs[0])[0]
        mean, lo, hi = mean_band(series)
        n = len(mean)
        if is_aimd:
            ax2.plot(ref_ws[:n], mean, "-", color="tab:red", lw=2.6, zorder=5, label=lab)
            if len(runs) > 1:
                ax2.fill_between(ref_ws[:n], lo, hi, color="tab:red", alpha=0.15, zorder=4)
        else:
            ax2.plot(ref_ws[:n], mean, "--", color=color, lw=1.8, alpha=0.9, label=lab)
            if len(runs) > 1:
                ax2.fill_between(ref_ws[:n], lo, hi, color=color, alpha=0.12)

        # --- 下：累计 work（近似，多 repeat 求均值+带）---
        wseries = [approx_cumwork(r, phase_ref["phase"])[1] for r in runs]
        ref_ws2 = approx_cumwork(runs[0], phase_ref["phase"])[0]
        wmean, wlo, whi = mean_band(wseries)
        n2 = len(wmean)
        if is_aimd:
            ax3.plot(ref_ws2[:n2], wmean, "-", color="tab:red", lw=2.6, zorder=5, label=lab)
            if len(runs) > 1:
                ax3.fill_between(ref_ws2[:n2], wlo, whi, color="tab:red", alpha=0.15, zorder=4)
        else:
            ax3.plot(ref_ws2[:n2], wmean, "--", color=color, lw=1.8, alpha=0.9, label=lab)
            if len(runs) > 1:
                ax3.fill_between(ref_ws2[:n2], wlo, whi, color=color, alpha=0.12)

    ax1.set_ylabel("scheduler alpha")
    ax1.set_title("Dynamic load: AIMD vs fixed-alpha")
    ax1.set_ylim(-5, 110)
    ax1.set_yticks([0, 25, 50, 75, 100])
    ax1.grid(True, alpha=0.3)
    _dedup_legend(ax1, loc="center left")
    if spans:
        for s, e, p in spans:
            ax1.annotate("HEAVY load" if p == 1 else "light load",
                         xy=((s + e) / 2, 104), ha="center", fontsize=8, color="gray")

    ax2.set_ylabel("cumulative control misses\n(lower better)")
    ax2.grid(True, alpha=0.3)
    _dedup_legend(ax2, loc="upper left")

    ax3.set_ylabel("cumulative AI work\n(higher better, *approx*)")
    ax3.set_xlabel("window")
    ax3.grid(True, alpha=0.3)
    _dedup_legend(ax3, loc="upper left")

    fig.tight_layout()
    p = os.path.join(out_dir, "dynamic_load_alpha_miss_work.png")
    fig.savefig(p, dpi=200)
    plt.close(fig)
    print(f"wrote {p}")


def plot_tradeoff(grouped, out_dir):
    fig, ax = plt.subplots(figsize=(8, 6))
    for key, runs in grouped.items():
        misses = [r["miss"] for r in runs]
        works = [r["ai_work"] for r in runs]
        mx = sum(misses) / len(misses)
        my = sum(works) / len(works)
        is_aimd = key[0] == "aimd"
        xerr = [[mx - min(misses)], [max(misses) - mx]] if len(runs) > 1 else None
        yerr = [[my - min(works)], [max(works) - my]] if len(runs) > 1 else None
        if is_aimd:
            ax.errorbar(mx, my, xerr=xerr, yerr=yerr, fmt="*", color="tab:red",
                        markersize=22, capsize=4, zorder=5,
                        markeredgecolor="black", markeredgewidth=0.5)
            ax.annotate("  AIMD", (mx, my), textcoords="offset points",
                        xytext=(8, 2), fontsize=10, color="tab:red", weight="bold")
        else:
            ax.errorbar(mx, my, xerr=xerr, yerr=yerr, fmt="o", color="tab:blue",
                        markersize=9, capsize=4, zorder=3)
            ax.annotate(f"  fixed α={key[1]}", (mx, my), textcoords="offset points",
                        xytext=(6, 2), fontsize=9, color="tab:blue")

    ax.set_xlabel("total control misses  (lower is better, <- better)")
    ax.set_ylabel("total AI work  (higher is better, ^ better)")
    ax.set_title("Dynamic load trade-off: miss vs throughput")
    ax.grid(True, alpha=0.3)
    ax.annotate("better\n(low miss, high throughput)",
                xy=(0.02, 0.97), xycoords="axes fraction",
                fontsize=9, color="gray", va="top")
    fig.tight_layout()
    p = os.path.join(out_dir, "dynamic_load_tradeoff.png")
    fig.savefig(p, dpi=200)
    plt.close(fig)
    print(f"wrote {p}")


def fmt_range(vals):
    m = sum(vals) / len(vals)
    if len(vals) == 1:
        return f"{m:.0f}"
    return f"{m:.0f} [{min(vals)}-{max(vals)}]"


def main():
    if len(sys.argv) < 2:
        print("usage: plot_dynamic_load.py <raw.log> [out_dir]")
        return 1
    raw = sys.argv[1]
    out_dir = sys.argv[2] if len(sys.argv) >= 3 else "logs/figs_dynamic"
    os.makedirs(out_dir, exist_ok=True)

    runs = parse(raw)
    grouped = defaultdict(list)
    for r in runs:
        grouped[strat_key(r)].append(r)

    phase_ref = next((r for r in runs if r["phase"]), None)
    spans = phase_spans(phase_ref["phase"]) if phase_ref else []

    plot_three_panel(grouped, spans, phase_ref, out_dir)
    plot_tradeoff(grouped, out_dir)

    # 成绩单
    reps = max(len(v) for v in grouped.values())
    print(f"\nrepeats detected (max per strategy): {reps}")
    print(f"{'strategy':<22} {'miss':<16} {'max_tard':<14} {'ai_work':<18}")
    print("-" * 72)
    order = sorted(grouped, key=lambda k: (k[0] == "aimd", k[1] if k[1] else 0))
    for key in order:
        rs = grouped[key]
        print(f"{strat_label(key):<22} "
              f"{fmt_range([r['miss'] for r in rs]):<16} "
              f"{fmt_range([r['lmax'] for r in rs]):<14} "
              f"{fmt_range([r['ai_work'] for r in rs]):<18}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())