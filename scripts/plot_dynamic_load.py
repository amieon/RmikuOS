#!/usr/bin/env python3
"""
动态负载实验对比图：AIMD vs 固定 alpha，在「轻→重→轻」三段负载下。

读 dynamic_load_exp 的 raw 日志（1 条 adaptive + 若干 fixed，同负载）。
产出双子图：
  上：alpha 随窗口的轨迹（AIMD 呼吸曲线 vs 各 fixed 水平线），phase 用背景阴影区分。
  下：control 累计 miss 随窗口增长（看谁在重阶段爆掉）。
并打印一张成绩单：各策略的总 miss / 最坏 tardiness / AI work。

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
    return runs


def phase_spans(phase_map):
    """把 window->phase 映射压成 [(start_w, end_w, phase), ...] 连续段。"""
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
        return f"fixed α={run['initial_alpha']}"
    return "AIMD (adaptive)"


def main():
    if len(sys.argv) < 2:
        print("usage: plot_dynamic_load.py <raw.log> [out_dir]")
        return 1
    raw = sys.argv[1]
    out_dir = sys.argv[2] if len(sys.argv) >= 3 else "logs/figs_dynamic"
    os.makedirs(out_dir, exist_ok=True)

    runs = parse(raw)
    # adaptive 排第一，fixed 按 alpha 排序
    runs.sort(key=lambda r: (r["is_fixed"], r["initial_alpha"]))

    # 取一个有 phase 的 run 来画阴影（都一样）
    phase_ref = next((r for r in runs if r["phase"]), None)
    spans = phase_spans(phase_ref["phase"]) if phase_ref else []

    fig, (ax1, ax2) = plt.subplots(2, 1, figsize=(10, 8), sharex=True)

    # phase 背景阴影：重(phase 1)染红，轻(0/2)留白
    for s, e, p in spans:
        if p == 1:
            for ax in (ax1, ax2):
                ax.axvspan(s - 0.5, e + 0.5, color="tab:red", alpha=0.07, zorder=0)

    colors = {"AIMD (adaptive)": "tab:red"}
    fixed_colors = ["tab:green", "tab:orange", "tab:purple", "tab:brown"]
    ci = 0

    for run in runs:
        lab = label_of(run)
        ws = [d["w"] for d in run["win"]]
        alphas = [d["alpha"] for d in run["win"]]
        # 累计 miss
        cum = []
        acc = 0
        for d in run["win"]:
            acc += d["miss"]
            cum.append(acc)

        if run["is_fixed"]:
            c = fixed_colors[ci % len(fixed_colors)]
            ci += 1
            ax1.plot(ws, alphas, "--", color=c, label=lab, linewidth=1.8, alpha=0.9)
            ax2.plot(ws, cum, "--", color=c, label=lab, linewidth=1.8, alpha=0.9)
        else:
            ax1.plot(ws, alphas, "-", color="tab:red", label=lab, linewidth=2.6, zorder=5)
            ax2.plot(ws, cum, "-", color="tab:red", label=lab, linewidth=2.6, zorder=5)

    ax1.set_ylabel("scheduler alpha")
    ax1.set_title("Dynamic load: AIMD vs fixed-alpha under dynamic load")
    ax1.set_ylim(-5, 110)
    ax1.set_yticks([0, 25, 50, 75, 100])
    ax1.grid(True, alpha=0.3)
    ax1.legend(loc="center left", fontsize=8)

    ax2.set_ylabel("cumulative control misses")
    ax2.set_xlabel("window")
    ax2.grid(True, alpha=0.3)
    ax2.legend(loc="upper left", fontsize=8)

    # 在重负载段顶部标注
    if spans:
        for s, e, p in spans:
            mid = (s + e) / 2
            txt = "HEAVY load" if p == 1 else "light load"
            ax1.annotate(txt, xy=(mid, 104), ha="center", fontsize=8,
                         color="gray")

    fig.tight_layout()
    p = os.path.join(out_dir, "dynamic_load_alpha_and_miss.png")
    fig.savefig(p, dpi=200)
    plt.close(fig)
    print(f"wrote {p}")

    # 成绩单
    print()
    print(f"{'strategy':<22} {'miss/jobs':<12} {'max_tard':<9} {'ai_work':<8}")
    print("-" * 55)
    for run in runs:
        lab = label_of(run)
        print(f"{lab:<22} {run['miss']}/{run['jobs']:<10} "
              f"{run['lmax']:<9} {run['ai_work']:<8}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())