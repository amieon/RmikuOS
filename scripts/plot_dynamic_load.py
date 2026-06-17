#!/usr/bin/env python3
"""
动态负载实验对比图：AIMD vs 固定 alpha，在「轻→重→轻」三段负载下。

读 dynamic_load_exp 的 raw 日志（1 条 adaptive + 若干 fixed，同负载）。

产出：
  图1 (三子图) dynamic_load_alpha_miss_work.png:
    上  : alpha 随窗口的轨迹（AIMD 呼吸曲线 vs 各 fixed 水平线），phase 阴影。
    中  : control 累计 miss 随窗口增长（看谁在重阶段爆掉）。
    下  : 累计 AI work 随窗口增长（看吞吐差距怎么攒出来的，*近似*，见下）。
  图2 (散点) dynamic_load_tradeoff.png:
    x=总 miss, y=总 AI work，四策略四点。左上 = 低 miss + 高吞吐 = 占优区。

并打印成绩单。

注意：AI work 在日志里只有最终总数，没有逐窗口值。第三子图的累计 work
是用「各窗口 AI 活跃度」加权后的*近似*分配（重负载段权重高、轻负载段权重低），
仅用于展示吞吐差距的形成趋势，曲线终点等于真实总 work。若需精确逐窗口 work，
需在 C 端每窗口打印当前累计 work。

用法：python3 plot_dynamic_load.py <raw.log> [out_dir]
"""
import os
import re
import sys

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
    return runs


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


def label_of(run):
    if run["is_fixed"]:
        return f"fixed alpha={run['initial_alpha']}"
    return "AIMD (adaptive)"


def approx_cumwork(run, phase_map):
    """
    用 AI 活跃度近似分配总 work 到各窗口：
    重负载窗口(phase 1)全部 AI 活跃 -> 权重高；轻负载窗口 -> 权重低。
    权重 = 该窗口活跃 AI 比例的代理（重=1.0, 轻=0.2）。曲线终点 = 真实总 work。
    """
    total = run["ai_work"] or 0
    ws = [d["w"] for d in run["win"]]
    weights = []
    for w in ws:
        p = phase_map.get(w, 0)
        weights.append(1.0 if p == 1 else 0.2)
    s = sum(weights) or 1.0
    cum = []
    acc = 0.0
    for wt in weights:
        acc += total * wt / s
        cum.append(acc)
    return ws, cum


def plot_three_panel(runs, spans, phase_ref, out_dir):
    fig, (ax1, ax2, ax3) = plt.subplots(3, 1, figsize=(10, 11), sharex=True)

    for s, e, p in spans:
        if p == 1:
            for ax in (ax1, ax2, ax3):
                ax.axvspan(s - 0.5, e + 0.5, color="tab:red", alpha=0.07, zorder=0)

    fixed_colors = ["tab:green", "tab:orange", "tab:purple", "tab:brown"]
    ci = 0
    for run in runs:
        lab = label_of(run)
        ws = [d["w"] for d in run["win"]]
        alphas = [d["alpha"] for d in run["win"]]
        cum_miss, acc = [], 0
        for d in run["win"]:
            acc += d["miss"]
            cum_miss.append(acc)
        wws, cum_work = approx_cumwork(run, phase_ref["phase"])

        if run["is_fixed"]:
            c = fixed_colors[ci % len(fixed_colors)]
            ci += 1
            ax1.plot(ws, alphas, "--", color=c, label=lab, lw=1.8, alpha=0.9)
            ax2.plot(ws, cum_miss, "--", color=c, label=lab, lw=1.8, alpha=0.9)
            ax3.plot(wws, cum_work, "--", color=c, label=lab, lw=1.8, alpha=0.9)
        else:
            ax1.plot(ws, alphas, "-", color="tab:red", label=lab, lw=2.6, zorder=5)
            ax2.plot(ws, cum_miss, "-", color="tab:red", label=lab, lw=2.6, zorder=5)
            ax3.plot(wws, cum_work, "-", color="tab:red", label=lab, lw=2.6, zorder=5)

    ax1.set_ylabel("scheduler alpha")
    ax1.set_title("Dynamic load: AIMD vs fixed-alpha")
    ax1.set_ylim(-5, 110)
    ax1.set_yticks([0, 25, 50, 75, 100])
    ax1.grid(True, alpha=0.3)
    ax1.legend(loc="center left", fontsize=8)

    if spans:
        for s, e, p in spans:
            ax1.annotate("HEAVY load" if p == 1 else "light load",
                         xy=((s + e) / 2, 104), ha="center", fontsize=8, color="gray")

    ax2.set_ylabel("cumulative control misses\n(lower better)")
    ax2.grid(True, alpha=0.3)
    ax2.legend(loc="upper left", fontsize=8)

    ax3.set_ylabel("cumulative AI work\n(higher better, *approx*)")
    ax3.set_xlabel("window")
    ax3.grid(True, alpha=0.3)
    ax3.legend(loc="upper left", fontsize=8)

    fig.tight_layout()
    p = os.path.join(out_dir, "dynamic_load_alpha_miss_work.png")
    fig.savefig(p, dpi=200)
    plt.close(fig)
    print(f"wrote {p}")


def plot_tradeoff(runs, out_dir):
    fig, ax = plt.subplots(figsize=(8, 6))
    for run in runs:
        x = run["miss"]
        y = run["ai_work"]
        if run["is_fixed"]:
            ax.scatter(x, y, color="tab:blue", s=90, zorder=3)
            ax.annotate(f" fixed α={run['initial_alpha']}", (x, y),
                        textcoords="offset points", xytext=(6, 2), fontsize=9,
                        color="tab:blue")
        else:
            ax.scatter(x, y, color="tab:red", marker="*", s=320, zorder=5,
                       edgecolor="black", linewidth=0.5)
            ax.annotate("  AIMD", (x, y), textcoords="offset points",
                        xytext=(6, 2), fontsize=10, color="tab:red", weight="bold")

    ax.set_xlabel("total control misses  (lower is better, ← better)")
    ax.set_ylabel("total AI work  (higher is better, ↑ better)")
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


def main():
    if len(sys.argv) < 2:
        print("usage: plot_dynamic_load.py <raw.log> [out_dir]")
        return 1
    raw = sys.argv[1]
    out_dir = sys.argv[2] if len(sys.argv) >= 3 else "logs/figs_dynamic"
    os.makedirs(out_dir, exist_ok=True)

    runs = parse(raw)
    runs.sort(key=lambda r: (r["is_fixed"], r["initial_alpha"]))
    phase_ref = next((r for r in runs if r["phase"]), None)
    spans = phase_spans(phase_ref["phase"]) if phase_ref else []

    plot_three_panel(runs, spans, phase_ref, out_dir)
    plot_tradeoff(runs, out_dir)

    print()
    print(f"{'strategy':<22} {'miss/jobs':<12} {'max_tard':<9} {'ai_work':<8}")
    print("-" * 55)
    for run in runs:
        print(f"{label_of(run):<22} {run['miss']}/{run['jobs']:<10} "
              f"{run['lmax']:<9} {run['ai_work']:<8}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())