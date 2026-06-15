#!/usr/bin/env python3
"""
adaptive_alpha 日志统计 + 画图。

输入：smoother_adaptive_alpha_test 重定向出来的原始日志（可含多个 run，
      也可把多次重复实验 cat 到一个文件里）。
输出：CSV + PNG，放到 out_dir。

向后兼容：control 的 [edge_deadline] 行若没有 tardiness/resp 字段，
自动降级，只是不画相关图，不会报错。

字段契约（control_result 行结尾六个字段，顺序必须一致）：
  lateness_sum lateness_max resp_sum resp_sumsq resp_min resp_max
所有除法 / 开方 / 标准差都在这里算，C 端只吐原始整数。
"""
import csv
import math
import os
import re
import sys
from collections import defaultdict

import matplotlib.pyplot as plt


RUN_RE = re.compile(
    r"\[adaptive_alpha\]\s+run\s+initial_alpha=(?P<initial_alpha>\d+)"
    r"\s+control_threads=(?P<control_threads>\d+)"
    r"\s+ai_threads=(?P<ai_threads>\d+)"
    r"\s+logger_threads=(?P<logger_threads>\d+)"
)

WINDOW_RE = re.compile(
    r"\[adaptive_window\]\s+window=(?P<window>\d+)"
    r"\s+alpha_before=(?P<alpha_before>\d+)"
    r"\s+alpha_after=(?P<alpha_after>\d+)"
    r"\s+max_allowed=(?P<max_allowed>\d+)"
    r"\s+safe_windows=(?P<safe_windows>\d+)"
    r"\s+jobs=(?P<jobs>\d+)"
    r"\s+miss=(?P<miss>\d+)"
    r"\s+miss_per_1000=(?P<miss_per_1000>\d+)"
    r"\s+action=(?P<action>\S+)"
)

SAMPLE_RE = re.compile(
    r"\[edge_sample\]\s+alpha=(?P<alpha>\d+)"
    r"\s+role=(?P<role>\S+)"
    r"\s+pid=(?P<pid>\d+)"
    r"\s+threads=(?P<threads>\d+)"
    r"\s+tickets=(?P<tickets>\d+)"
    r"\s+effective=(?P<effective>\d+)"
    r"\s+ready=(?P<ready>\d+)"
    r"\s+run_ticks=(?P<run_ticks>\d+)"
    r"\s+stride=(?P<stride>\d+)"
    r"\s+pass=(?P<pass>\d+)"
)

RESULT_RE = re.compile(
    r"\[edge_deadline\]\s+alpha=(?P<alpha>\d+)"
    r"\s+role=(?P<role>\S+)"
    r"\s+pid=(?P<pid>\d+)"
    r"\s+threads=(?P<threads>\d+)"
    r"\s+tickets=(?P<tickets>\d+)"
    r"\s+effective=(?P<effective>\d+)"
    r"\s+ready=(?P<ready>\d+)"
    r"\s+run_ticks=(?P<run_ticks>\d+)"
    r"\s+work=(?P<work>\d+)"
    r"\s+jobs=(?P<jobs>\d+)"
    r"\s+miss=(?P<miss>\d+)"
)

# control 结尾的可选 tardiness / resp 字段，单独匹配，老日志缺它则降级。
EDGE_EXTRA_RE = re.compile(
    r"lateness_sum=(?P<lateness_sum>\d+)"
    r"\s+lateness_max=(?P<lateness_max>\d+)"
    r"\s+resp_sum=(?P<resp_sum>\d+)"
    r"\s+resp_sumsq=(?P<resp_sumsq>\d+)"
    r"\s+resp_min=(?P<resp_min>\d+)"
    r"\s+resp_max=(?P<resp_max>\d+)"
)

FINAL_RE = re.compile(r"\[adaptive_alpha\]\s+final_alpha=(?P<final_alpha>\d+)")


def to_int(x):
    return int(x)


def new_run(run_id, m):
    control_threads = to_int(m.group("control_threads"))
    ai_threads = to_int(m.group("ai_threads"))
    logger_threads = to_int(m.group("logger_threads"))

    return {
        "run_id": run_id,
        "initial_alpha": to_int(m.group("initial_alpha")),
        "control_threads": control_threads,
        "ai_threads": ai_threads,
        "logger_threads": logger_threads,
        "case": f"{control_threads}_{ai_threads}_{logger_threads}",
        "windows": [],
        "samples": {},
        "results": {},
        "final_alpha": None,
    }


def parse_log(path):
    runs = []
    cur = None
    run_id = 0

    with open(path, "r", encoding="utf-8", errors="ignore") as f:
        for line in f:
            line = line.strip()

            m = RUN_RE.search(line)
            if m:
                if cur is not None:
                    runs.append(cur)
                cur = new_run(run_id, m)
                run_id += 1
                continue

            if cur is None:
                continue

            m = WINDOW_RE.search(line)
            if m:
                row = {
                    "run_id": cur["run_id"],
                    "case": cur["case"],
                    "initial_alpha": cur["initial_alpha"],
                    "window": to_int(m.group("window")),
                    "alpha_before": to_int(m.group("alpha_before")),
                    "alpha_after": to_int(m.group("alpha_after")),
                    "max_allowed": to_int(m.group("max_allowed")),
                    "safe_windows": to_int(m.group("safe_windows")),
                    "jobs": to_int(m.group("jobs")),
                    "miss": to_int(m.group("miss")),
                    "miss_per_1000": to_int(m.group("miss_per_1000")),
                    "action": m.group("action"),
                }
                if not cur["windows"] or cur["windows"][-1] != row:
                    cur["windows"].append(row)
                continue

            m = SAMPLE_RE.search(line)
            if m:
                role = m.group("role")
                cur["samples"][role] = {
                    "alpha": to_int(m.group("alpha")),
                    "threads": to_int(m.group("threads")),
                    "tickets": to_int(m.group("tickets")),
                    "effective": to_int(m.group("effective")),
                    "ready": to_int(m.group("ready")),
                    "run_ticks": to_int(m.group("run_ticks")),
                    "stride": to_int(m.group("stride")),
                    "pass": to_int(m.group("pass")),
                }
                continue

            m = RESULT_RE.search(line)
            if m:
                role = m.group("role").replace("_result", "")
                entry = {
                    "alpha": to_int(m.group("alpha")),
                    "threads": to_int(m.group("threads")),
                    "tickets": to_int(m.group("tickets")),
                    "work": to_int(m.group("work")),
                    "jobs": to_int(m.group("jobs")),
                    "miss": to_int(m.group("miss")),
                }
                me = EDGE_EXTRA_RE.search(line)
                if me:
                    entry["lateness_sum"] = to_int(me.group("lateness_sum"))
                    entry["lateness_max"] = to_int(me.group("lateness_max"))
                    entry["resp_sum"] = to_int(me.group("resp_sum"))
                    entry["resp_sumsq"] = to_int(me.group("resp_sumsq"))
                    entry["resp_min"] = to_int(me.group("resp_min"))
                    entry["resp_max"] = to_int(me.group("resp_max"))
                    entry["has_tardiness"] = True
                else:
                    entry["has_tardiness"] = False
                cur["results"][role] = entry
                continue

            m = FINAL_RE.search(line)
            if m:
                cur["final_alpha"] = to_int(m.group("final_alpha"))
                continue

    if cur is not None:
        runs.append(cur)

    return runs


def build_window_rows(runs):
    rows = []
    for run in runs:
        rows.extend(run["windows"])
    return rows


def derive_tardiness(control):
    """从原始整数聚合量推出可读指标。除法/开方都在这里。"""
    jobs = control.get("jobs", 0)
    miss = control.get("miss", 0)

    if not control.get("has_tardiness", False) or jobs <= 0:
        return {
            "has_tardiness": False,
            "mean_tardiness": -1.0,
            "tardiness_per_miss": -1.0,
            "max_tardiness": -1,
            "mean_resp": -1.0,
            "resp_jitter_std": -1.0,
            "resp_range": -1,
            "resp_min": -1,
            "resp_max": -1,
        }

    lateness_sum = control.get("lateness_sum", 0)
    lateness_max = control.get("lateness_max", 0)
    resp_sum = control.get("resp_sum", 0)
    resp_sumsq = control.get("resp_sumsq", 0)
    resp_min = control.get("resp_min", 0)
    resp_max = control.get("resp_max", 0)

    mean_resp = resp_sum / jobs
    var = resp_sumsq / jobs - mean_resp * mean_resp
    resp_std = math.sqrt(var) if var > 0 else 0.0

    return {
        "has_tardiness": True,
        "mean_tardiness": lateness_sum / jobs,
        "tardiness_per_miss": (lateness_sum / miss) if miss > 0 else 0.0,
        "max_tardiness": lateness_max,
        "mean_resp": mean_resp,
        "resp_jitter_std": resp_std,
        "resp_range": resp_max - resp_min,
        "resp_min": resp_min,
        "resp_max": resp_max,
    }


def build_summary_rows(runs):
    rows = []
    for run in runs:
        control = run["results"].get("control", {})
        ai = run["results"].get("ai", {})
        logger = run["results"].get("logger", {})

        jobs = control.get("jobs", 0)
        miss = control.get("miss", 0)
        miss_rate = miss / jobs if jobs > 0 else 0.0

        control_tick = run["samples"].get("control", {}).get("run_ticks", 0)
        ai_tick = run["samples"].get("ai", {}).get("run_ticks", 0)
        logger_tick = run["samples"].get("logger", {}).get("run_ticks", 0)
        total_tick = control_tick + ai_tick + logger_tick

        td = derive_tardiness(control)

        rows.append({
            "run_id": run["run_id"],
            "case": run["case"],
            "initial_alpha": run["initial_alpha"],
            "final_alpha": run["final_alpha"] if run["final_alpha"] is not None else -1,
            "control_jobs": jobs,
            "control_miss": miss,
            "control_miss_rate": miss_rate,
            "ai_work": ai.get("work", 0),
            "logger_work": logger.get("work", 0),
            "control_tick_share": control_tick / total_tick if total_tick > 0 else 0.0,
            "ai_tick_share": ai_tick / total_tick if total_tick > 0 else 0.0,
            "logger_tick_share": logger_tick / total_tick if total_tick > 0 else 0.0,
            "has_tardiness": 1 if td["has_tardiness"] else 0,
            "control_mean_tardiness": td["mean_tardiness"],
            "control_tardiness_per_miss": td["tardiness_per_miss"],
            "control_max_tardiness": td["max_tardiness"],
            "control_mean_resp": td["mean_resp"],
            "control_resp_jitter_std": td["resp_jitter_std"],
            "control_resp_range": td["resp_range"],
            "control_resp_min": td["resp_min"],
            "control_resp_max": td["resp_max"],
        })
    return rows


def write_csv(path, rows, fieldnames):
    with open(path, "w", newline="", encoding="utf-8") as f:
        writer = csv.DictWriter(f, fieldnames=fieldnames)
        writer.writeheader()
        for row in rows:
            writer.writerow(row)


# ---------------------------------------------------------------------------
# 多次重复实验的聚合：把同一 (case, initial_alpha) 的多个 run 收拢，
# 取均值并保留 min/max 做误差带。单次实验时退化为点本身。
# ---------------------------------------------------------------------------
def aggregate_repeats(summary_rows):
    groups = defaultdict(list)
    for r in summary_rows:
        groups[(r["case"], r["initial_alpha"])].append(r)

    agg = []
    for (case, init), rs in groups.items():
        n = len(rs)

        def col(key):
            return [r[key] for r in rs]

        def mean(key):
            vals = col(key)
            return sum(vals) / len(vals) if vals else 0.0

        td_rs = [r for r in rs if r["has_tardiness"]]
        has_td = len(td_rs) > 0

        def td_mean(key):
            vals = [r[key] for r in td_rs]
            return sum(vals) / len(vals) if vals else -1.0

        def td_minmax(key):
            vals = [r[key] for r in td_rs]
            return (min(vals), max(vals)) if vals else (-1.0, -1.0)

        mt_lo, mt_hi = td_minmax("control_mean_tardiness")
        mx_lo, mx_hi = td_minmax("control_max_tardiness")
        jt_lo, jt_hi = td_minmax("control_resp_jitter_std")

        agg.append({
            "case": case,
            "initial_alpha": init,
            "repeats": n,
            "final_alpha_mean": mean("final_alpha"),
            "miss_rate_mean": mean("control_miss_rate"),
            "ai_work_mean": mean("ai_work"),
            "has_tardiness": 1 if has_td else 0,
            "mean_tardiness_mean": td_mean("control_mean_tardiness"),
            "mean_tardiness_lo": mt_lo,
            "mean_tardiness_hi": mt_hi,
            "max_tardiness_mean": td_mean("control_max_tardiness"),
            "max_tardiness_lo": mx_lo,
            "max_tardiness_hi": mx_hi,
            "jitter_std_mean": td_mean("control_resp_jitter_std"),
            "jitter_std_lo": jt_lo,
            "jitter_std_hi": jt_hi,
            "resp_range_mean": td_mean("control_resp_range"),
        })

    agg.sort(key=lambda r: (r["case"], r["initial_alpha"]))
    return agg


def _line(xs, ys, xlabel, ylabel, title, path):
    plt.figure(figsize=(7, 4.5))
    plt.plot(xs, ys, marker="o")
    plt.xlabel(xlabel)
    plt.ylabel(ylabel)
    plt.title(title)
    plt.grid(True)
    plt.tight_layout()
    plt.savefig(path, dpi=200)
    plt.close()
    print(f"wrote {path}")


def plot_trace(run, out_dir):
    if not run["windows"]:
        return
    case = run["case"]
    init = run["initial_alpha"]
    run_id = run["run_id"]
    ws = sorted(run["windows"], key=lambda r: r["window"])
    xs = [r["window"] for r in ws]
    alpha_after = [r["alpha_after"] for r in ws]
    max_allowed = [r["max_allowed"] for r in ws]
    miss_per_1000 = [r["miss_per_1000"] for r in ws]
    actions = [r["action"] for r in ws]

    plt.figure(figsize=(7, 4.5))
    plt.plot(xs, alpha_after, marker="o", label="alpha")
    plt.plot(xs, max_allowed, marker="x", linestyle="--", label="max allowed")
    for x, y, action in zip(xs, alpha_after, actions):
        plt.text(x, y + 3, action, fontsize=7, rotation=25)
    plt.xlabel("window")
    plt.ylabel("alpha")
    plt.yticks([0, 25, 50, 75, 100])
    plt.ylim(-5, 110)
    plt.title(f"Adaptive alpha trace, case={case}, initial={init}")
    plt.grid(True)
    plt.legend()
    plt.tight_layout()
    path = os.path.join(out_dir, f"adaptive_alpha_trace_{case}_init{init}_run{run_id}.png")
    plt.savefig(path, dpi=200)
    plt.close()
    print(f"wrote {path}")

    plt.figure(figsize=(7, 4.5))
    plt.plot(xs, miss_per_1000, marker="o")
    plt.axhline(100, linestyle="--", label="unsafe threshold")
    plt.axhline(500, linestyle="--", label="severe threshold")
    for x, y, action in zip(xs, miss_per_1000, actions):
        if y > 0:
            plt.text(x, y + 20, action, fontsize=7, rotation=25)
    plt.xlabel("window")
    plt.ylabel("miss per 1000 jobs")
    plt.title(f"Control miss pressure, case={case}, initial={init}")
    plt.grid(True)
    plt.legend()
    plt.tight_layout()
    path = os.path.join(out_dir, f"adaptive_miss_trace_{case}_init{init}_run{run_id}.png")
    plt.savefig(path, dpi=200)
    plt.close()
    print(f"wrote {path}")


def _errband(ax, xs, mean, lo, hi, label):
    ax.plot(xs, mean, marker="o", label=label)
    # min/max 误差带；单次实验时 lo==hi==mean，带宽为 0，不影响观感
    ax.fill_between(xs, lo, hi, alpha=0.15)


def plot_aggregate(agg, out_dir):
    groups = defaultdict(list)
    for r in agg:
        groups[r["case"]].append(r)

    for case, rs in sorted(groups.items()):
        rs = sorted(rs, key=lambda r: r["initial_alpha"])
        xs = [r["initial_alpha"] for r in rs]

        # final alpha 收敛点
        _line(xs, [r["final_alpha_mean"] for r in rs],
              "initial alpha", "final alpha (mean)",
              f"Final alpha vs initial, case={case}",
              os.path.join(out_dir, f"agg_final_alpha_{case}.png"))

        # miss rate
        _line(xs, [r["miss_rate_mean"] for r in rs],
              "initial alpha", "control miss rate (mean)",
              f"Control miss rate vs initial, case={case}",
              os.path.join(out_dir, f"agg_miss_rate_{case}.png"))

        td_rs = [r for r in rs if r["has_tardiness"]]
        if not td_rs:
            continue
        tx = [r["initial_alpha"] for r in td_rs]

        # 平均 tardiness（带 min/max 误差带）
        fig, ax = plt.subplots(figsize=(7, 4.5))
        _errband(ax, tx,
                 [r["mean_tardiness_mean"] for r in td_rs],
                 [r["mean_tardiness_lo"] for r in td_rs],
                 [r["mean_tardiness_hi"] for r in td_rs],
                 "mean tardiness")
        ax.set_xlabel("initial alpha")
        ax.set_ylabel("mean tardiness (ticks)")
        ax.set_title(f"Mean tardiness vs initial, case={case}")
        ax.grid(True)
        fig.tight_layout()
        p = os.path.join(out_dir, f"agg_mean_tardiness_{case}.png")
        fig.savefig(p, dpi=200)
        plt.close(fig)
        print(f"wrote {p}")

        # 最坏 tardiness（带误差带）
        fig, ax = plt.subplots(figsize=(7, 4.5))
        _errband(ax, tx,
                 [r["max_tardiness_mean"] for r in td_rs],
                 [r["max_tardiness_lo"] for r in td_rs],
                 [r["max_tardiness_hi"] for r in td_rs],
                 "max tardiness")
        ax.set_xlabel("initial alpha")
        ax.set_ylabel("max tardiness (ticks)")
        ax.set_title(f"Worst-case tardiness vs initial, case={case}")
        ax.grid(True)
        fig.tight_layout()
        p = os.path.join(out_dir, f"agg_max_tardiness_{case}.png")
        fig.savefig(p, dpi=200)
        plt.close(fig)
        print(f"wrote {p}")

        # jitter：std 和 range 并排画做对照（小尺度下 std 分辨率有限）
        fig, ax = plt.subplots(figsize=(7, 4.5))
        ax.plot(tx, [r["jitter_std_mean"] for r in td_rs],
                marker="o", label="response jitter std")
        ax.plot(tx, [r["resp_range_mean"] for r in td_rs],
                marker="s", linestyle="--", label="response range (max-min)")
        ax.set_xlabel("initial alpha")
        ax.set_ylabel("ticks")
        ax.set_title(f"Response-time jitter vs initial, case={case}")
        ax.grid(True)
        ax.legend()
        fig.tight_layout()
        p = os.path.join(out_dir, f"agg_jitter_{case}.png")
        fig.savefig(p, dpi=200)
        plt.close(fig)
        print(f"wrote {p}")

        # 软 trade-off：平均 tardiness vs AI 吞吐
        plt.figure(figsize=(7, 4.5))
        sx = [r["mean_tardiness_mean"] for r in td_rs]
        sy = [r["ai_work_mean"] for r in td_rs]
        plt.plot(sx, sy, marker="o")
        for x, y, init in zip(sx, sy, tx):
            plt.text(x, y, f" init={init}", fontsize=8)
        plt.xlabel("control mean tardiness (ticks)")
        plt.ylabel("AI work (mean)")
        plt.title(f"Tardiness-throughput trade-off, case={case}")
        plt.grid(True)
        plt.tight_layout()
        p = os.path.join(out_dir, f"agg_tradeoff_{case}.png")
        plt.savefig(p, dpi=200)
        plt.close()
        print(f"wrote {p}")


def print_summary(summary_rows):
    any_td = any(r["has_tardiness"] for r in summary_rows)
    print()
    if any_td:
        print("run case      init final miss/jobs  miss_rate mean_tard max_tard jitter resp[min-max] ai_work")
        print("--- --------- ---- ----- ---------- --------- --------- -------- ------ ------------- -------")
        for r in summary_rows:
            if r["has_tardiness"]:
                mt = f"{r['control_mean_tardiness']:.3f}"
                mx = f"{r['control_max_tardiness']}"
                jt = f"{r['control_resp_jitter_std']:.2f}"
                rng = f"{r['control_resp_min']}-{r['control_resp_max']}"
            else:
                mt = mx = jt = rng = "-"
            print(
                f"{r['run_id']:<3} {r['case']:<9} {r['initial_alpha']:<4} "
                f"{r['final_alpha']:<5} {r['control_miss']}/{r['control_jobs']:<8} "
                f"{r['control_miss_rate']:<9.4f} {mt:<9} {mx:<8} {jt:<6} "
                f"{rng:<13} {r['ai_work']}"
            )
    else:
        print("run case      init final miss/jobs  miss_rate ai_work logger_work")
        print("--- --------- ---- ----- ---------- --------- ------- -----------")
        for r in summary_rows:
            print(
                f"{r['run_id']:<3} {r['case']:<9} {r['initial_alpha']:<4} "
                f"{r['final_alpha']:<5} {r['control_miss']}/{r['control_jobs']:<8} "
                f"{r['control_miss_rate']:<9.4f} {r['ai_work']:<7} {r['logger_work']}"
            )


def main():
    if len(sys.argv) < 2:
        print("usage: plot_adaptive_alpha_log.py <raw.log> [out_dir]")
        print("example: python3 scripts/plot_adaptive_alpha_log.py logs/adaptive_alpha_raw.log logs/figs_adaptive")
        return 1

    log_path = sys.argv[1]
    out_dir = sys.argv[2] if len(sys.argv) >= 3 else "logs/figs_adaptive"
    os.makedirs(out_dir, exist_ok=True)

    runs = parse_log(log_path)
    window_rows = build_window_rows(runs)
    summary_rows = build_summary_rows(runs)
    agg_rows = aggregate_repeats(summary_rows)

    n_td = sum(1 for r in summary_rows if r["has_tardiness"])
    print(f"parsed runs={len(runs)}, windows={len(window_rows)}, "
          f"runs_with_tardiness={n_td}, agg_points={len(agg_rows)}")

    write_csv(os.path.join(out_dir, "adaptive_windows.csv"), window_rows,
              ["run_id", "case", "initial_alpha", "window", "alpha_before",
               "alpha_after", "max_allowed", "safe_windows", "jobs", "miss",
               "miss_per_1000", "action"])

    write_csv(os.path.join(out_dir, "adaptive_summary.csv"), summary_rows,
              ["run_id", "case", "initial_alpha", "final_alpha", "control_jobs",
               "control_miss", "control_miss_rate", "ai_work", "logger_work",
               "control_tick_share", "ai_tick_share", "logger_tick_share",
               "has_tardiness", "control_mean_tardiness", "control_tardiness_per_miss",
               "control_max_tardiness", "control_mean_resp", "control_resp_jitter_std",
               "control_resp_range", "control_resp_min", "control_resp_max"])

    write_csv(os.path.join(out_dir, "adaptive_aggregate.csv"), agg_rows,
              ["case", "initial_alpha", "repeats", "final_alpha_mean",
               "miss_rate_mean", "ai_work_mean", "has_tardiness",
               "mean_tardiness_mean", "mean_tardiness_lo", "mean_tardiness_hi",
               "max_tardiness_mean", "max_tardiness_lo", "max_tardiness_hi",
               "jitter_std_mean", "jitter_std_lo", "jitter_std_hi",
               "resp_range_mean"])

    print_summary(summary_rows)

    for run in runs:
        plot_trace(run, out_dir)

    plot_aggregate(agg_rows, out_dir)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())