#!/usr/bin/env python3
"""
Alpha 机制实验作图：effective_tickets / 实际 CPU 份额如何随 alpha 与线程数变化。

读 alpha_arg_test 的 raw 日志，按 [alpha_sample] / [alpha_result] 行解析。
对每个 (alpha, case) 提取每个 role 的 effective_tickets、run_ticks、work。
支持连续 alpha（任意整数点）与多 repeat（同 alpha+case+role 求均值）。

产出（对每个 case = 一组 threads 组合）：
  1. effective_vs_alpha_<case>.png
       y = effective_tickets，x = alpha，每条线一个 role(线程数)。
       展示调度器算出的权重随 alpha 上升（n^(alpha/100)）。
  2. tickshare_vs_alpha_<case>.png
       y = 实际 CPU 份额（run_ticks 占比 或 work 占比），x = alpha。
       展示 effective 权重真的兑现成了 CPU 时间——机制有效的铁证。

用法：python3 plot_alpha_arg_log.py <raw.log> [out_dir] [--metric run_ticks|work]
"""
import os
import re
import sys
from collections import defaultdict

import matplotlib.pyplot as plt

RUN_RE = re.compile(r"\[alpha_arg\]\s+run\s+alpha=(?P<a>\d+)\s+procs=\d+\s+threads=(?P<th>[\d,]+)")
SAMPLE_RE = re.compile(
    r"\[alpha_sample\]\s+alpha=(?P<a>\d+)\s+role=(?P<role>\d+)\s+pid=\d+"
    r"\s+threads=(?P<th>\d+)\s+tickets=(?P<tk>\d+)\s+effective=(?P<eff>\d+)"
    r"\s+ready=(?P<ready>\d+)\s+run_ticks=(?P<rt>\d+)"
)
RESULT_RE = re.compile(
    r"\[alpha_result\]\s+alpha=(?P<a>\d+)\s+role=(?P<role>\d+)\s+pid=\d+"
    r"\s+threads=(?P<th>\d+)\s+tickets=(?P<tk>\d+)\s+work=(?P<work>\d+)"
)


def parse(path):
    """
    返回：
      data[case_str][alpha][role] = {
          'threads': int, 'eff': [..], 'run_ticks': [..], 'work': [..]
      }
    （列表是为了支持多 repeat 聚合）
    并返回每个 case 的 role->threads 映射。
    """
    data = defaultdict(lambda: defaultdict(lambda: defaultdict(
        lambda: {"threads": None, "eff": [], "run_ticks": [], "work": []})))
    cur_case = None  # threads 组合字符串，如 "1,5,7"

    with open(path, encoding="utf-8", errors="ignore") as f:
        for line in f:
            m = RUN_RE.search(line)
            if m:
                cur_case = m.group("th")
                continue
            m = SAMPLE_RE.search(line)
            if m and cur_case is not None:
                a = int(m.group("a"))
                role = int(m.group("role"))
                d = data[cur_case][a][role]
                d["threads"] = int(m.group("th"))
                d["eff"].append(int(m.group("eff")))
                d["run_ticks"].append(int(m.group("rt")))
                continue
            m = RESULT_RE.search(line)
            if m and cur_case is not None:
                a = int(m.group("a"))
                role = int(m.group("role"))
                d = data[cur_case][a][role]
                d["threads"] = int(m.group("th"))
                d["work"].append(int(m.group("work")))
                continue
    return data


def mean(xs):
    return sum(xs) / len(xs) if xs else 0.0


def plot_effective(case, per_alpha, out_dir):
    """effective_tickets vs alpha，每条线一个 role(线程数)。"""
    alphas = sorted(per_alpha.keys())
    # 收集所有 role
    roles = sorted({r for a in alphas for r in per_alpha[a].keys()})

    fig, ax = plt.subplots(figsize=(8, 5.5))
    for role in roles:
        xs, ys, lo, hi = [], [], [], []
        threads = None
        for a in alphas:
            if role in per_alpha[a]:
                d = per_alpha[a][role]
                if not d["eff"]:
                    continue
                threads = d["threads"]
                xs.append(a)
                ys.append(mean(d["eff"]))
                lo.append(min(d["eff"]))
                hi.append(max(d["eff"]))
        if not xs:
            continue
        line, = ax.plot(xs, ys, "-o", markersize=3, label=f"{threads} threads")
        if any(h > l for l, h in zip(lo, hi)):
            ax.fill_between(xs, lo, hi, color=line.get_color(), alpha=0.15)

    ax.set_xlabel("alpha")
    ax.set_ylabel("effective_tickets")
    ax.set_title(f"Effective tickets vs alpha  (case threads={case})")
    ax.grid(True, alpha=0.3)
    ax.legend()
    fig.tight_layout()
    p = os.path.join(out_dir, f"effective_vs_alpha_{case.replace(',', '_')}.png")
    fig.savefig(p, dpi=200)
    plt.close(fig)
    print(f"wrote {p}")


def plot_tickshare(case, per_alpha, out_dir, metric):
    """实际 CPU 份额 vs alpha：每个 role 的 metric 占该 alpha 下总量的比例。"""
    alphas = sorted(per_alpha.keys())
    roles = sorted({r for a in alphas for r in per_alpha[a].keys()})

    fig, ax = plt.subplots(figsize=(8, 5.5))
    # 先算每个 alpha 的总量，再算各 role 占比
    role_threads = {}
    series = {role: ([], []) for role in roles}  # role -> (xs, share%)
    for a in alphas:
        total = 0.0
        vals = {}
        for role in roles:
            if role in per_alpha[a]:
                d = per_alpha[a][role]
                v = mean(d[metric])
                vals[role] = v
                total += v
                role_threads[role] = d["threads"]
        if total <= 0:
            continue
        for role in roles:
            if role in vals:
                series[role][0].append(a)
                series[role][1].append(100.0 * vals[role] / total)

    for role in roles:
        xs, sh = series[role]
        if xs:
            ax.plot(xs, sh, "-o", markersize=3,
                    label=f"{role_threads.get(role, '?')} threads")

    ax.set_xlabel("alpha")
    ax.set_ylabel(f"actual CPU share (% of {metric})")
    ax.set_title(f"Actual CPU share vs alpha  (case threads={case})")
    ax.grid(True, alpha=0.3)
    ax.legend()
    fig.tight_layout()
    p = os.path.join(out_dir, f"tickshare_vs_alpha_{case.replace(',', '_')}.png")
    fig.savefig(p, dpi=200)
    plt.close(fig)
    print(f"wrote {p}")


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    metric = "run_ticks"
    if "--metric" in sys.argv:
        i = sys.argv.index("--metric")
        if i + 1 < len(sys.argv):
            metric = sys.argv[i + 1]
    if metric not in ("run_ticks", "work"):
        print("metric must be run_ticks or work")
        return 1
    if not args:
        print("usage: plot_alpha_arg_log.py <raw.log> [out_dir] [--metric run_ticks|work]")
        return 1

    raw = args[0]
    out_dir = args[1] if len(args) >= 2 else "logs/figs_alpha"
    os.makedirs(out_dir, exist_ok=True)

    data = parse(raw)
    if not data:
        print("no [alpha_sample]/[alpha_arg] data parsed — check log format")
        return 1

    cases = sorted(data.keys())
    print(f"parsed cases: {cases}")
    for case in cases:
        per_alpha = data[case]
        alphas = sorted(per_alpha.keys())
        print(f"  case threads={case}: {len(alphas)} alpha points "
              f"({alphas[0]}..{alphas[-1]})")
        plot_effective(case, per_alpha, out_dir)
        plot_tickshare(case, per_alpha, out_dir, metric)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())