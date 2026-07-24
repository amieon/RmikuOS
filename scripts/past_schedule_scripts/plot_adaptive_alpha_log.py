#!/usr/bin/env python3
import csv
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

                # 防止旧日志里同一 window 被打印两遍。
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
                cur["results"][role] = {
                    "alpha": to_int(m.group("alpha")),
                    "threads": to_int(m.group("threads")),
                    "tickets": to_int(m.group("tickets")),
                    "work": to_int(m.group("work")),
                    "jobs": to_int(m.group("jobs")),
                    "miss": to_int(m.group("miss")),
                }
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
        })

    return rows


def write_csv(path, rows, fieldnames):
    with open(path, "w", newline="", encoding="utf-8") as f:
        writer = csv.DictWriter(f, fieldnames=fieldnames)
        writer.writeheader()

        for row in rows:
            writer.writerow(row)


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

    # 图 1：alpha 搜索轨迹
    plt.figure(figsize=(7, 4.5))
    plt.plot(xs, alpha_after, marker="o", label="alpha")
    plt.plot(xs, max_allowed, marker="x", linestyle="--", label="max allowed")

    for x, y, action in zip(xs, alpha_after, actions):
        plt.text(x, y + 3, action, fontsize=8, rotation=25)

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

    # 图 2：每个 window 的 miss pressure
    plt.figure(figsize=(7, 4.5))
    plt.plot(xs, miss_per_1000, marker="o")
    plt.axhline(100, linestyle="--", label="unsafe threshold")
    plt.axhline(500, linestyle="--", label="severe threshold")

    for x, y, action in zip(xs, miss_per_1000, actions):
        if y > 0:
            plt.text(x, y + 20, action, fontsize=8, rotation=25)

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


def group_summary_by_case(rows):
    groups = defaultdict(list)

    for row in rows:
        groups[row["case"]].append(row)

    return groups


def plot_summary(summary_rows, out_dir):
    groups = group_summary_by_case(summary_rows)

    for case, rows in sorted(groups.items()):
        rows = sorted(rows, key=lambda r: r["initial_alpha"])

        xs = [r["initial_alpha"] for r in rows]

        # final alpha
        plt.figure(figsize=(7, 4.5))
        plt.plot(xs, [r["final_alpha"] for r in rows], marker="o")
        plt.xlabel("initial alpha")
        plt.ylabel("final alpha")
        plt.yticks([0, 25, 50, 75, 100])
        plt.ylim(-5, 110)
        plt.title(f"Final alpha by initial alpha, case={case}")
        plt.grid(True)
        plt.tight_layout()

        path = os.path.join(out_dir, f"adaptive_final_alpha_{case}.png")
        plt.savefig(path, dpi=200)
        plt.close()
        print(f"wrote {path}")

        # miss rate
        plt.figure(figsize=(7, 4.5))
        plt.plot(xs, [r["control_miss_rate"] for r in rows], marker="o")
        plt.xlabel("initial alpha")
        plt.ylabel("control miss rate")
        plt.title(f"Adaptive control miss rate, case={case}")
        plt.grid(True)
        plt.tight_layout()

        path = os.path.join(out_dir, f"adaptive_miss_rate_{case}.png")
        plt.savefig(path, dpi=200)
        plt.close()
        print(f"wrote {path}")

        # AI work
        plt.figure(figsize=(7, 4.5))
        plt.plot(xs, [r["ai_work"] for r in rows], marker="o")
        plt.xlabel("initial alpha")
        plt.ylabel("AI work")
        plt.title(f"Adaptive AI throughput, case={case}")
        plt.grid(True)
        plt.tight_layout()

        path = os.path.join(out_dir, f"adaptive_ai_work_{case}.png")
        plt.savefig(path, dpi=200)
        plt.close()
        print(f"wrote {path}")

        # tradeoff: miss rate vs AI work
        plt.figure(figsize=(7, 4.5))
        mx = [r["control_miss_rate"] for r in rows]
        my = [r["ai_work"] for r in rows]

        plt.plot(mx, my, marker="o")

        for x, y, init in zip(mx, my, xs):
            plt.text(x, y, f" init={init}", fontsize=8)

        plt.xlabel("control miss rate")
        plt.ylabel("AI work")
        plt.title(f"Adaptive deadline-throughput trade-off, case={case}")
        plt.grid(True)
        plt.tight_layout()

        path = os.path.join(out_dir, f"adaptive_tradeoff_{case}.png")
        plt.savefig(path, dpi=200)
        plt.close()
        print(f"wrote {path}")


def print_summary(summary_rows):
    print()
    print("run case      init final miss/jobs miss_rate  ai_work logger_work ai_tick")
    print("--- --------- ---- ----- --------- ---------- ------- ----------- -------")

    for r in summary_rows:
        print(
            f"{r['run_id']:<3} "
            f"{r['case']:<9} "
            f"{r['initial_alpha']:<4} "
            f"{r['final_alpha']:<5} "
            f"{r['control_miss']}/{r['control_jobs']:<7} "
            f"{r['control_miss_rate']:<10.4f} "
            f"{r['ai_work']:<7} "
            f"{r['logger_work']:<11} "
            f"{r['ai_tick_share']:<7.4f}"
        )


def main():
    if len(sys.argv) < 2:
        print("usage: plot_adaptive_alpha_log.py <adaptive_alpha_raw.log> [out_dir]")
        print("example: python3 scripts/plot_adaptive_alpha_log.py logs/adaptive_alpha_raw.log logs/figs_adaptive")
        return 1

    log_path = sys.argv[1]
    out_dir = sys.argv[2] if len(sys.argv) >= 3 else "logs/figs_adaptive"

    os.makedirs(out_dir, exist_ok=True)

    runs = parse_log(log_path)
    window_rows = build_window_rows(runs)
    summary_rows = build_summary_rows(runs)

    print(f"parsed runs={len(runs)}, windows={len(window_rows)}")

    write_csv(
        os.path.join(out_dir, "adaptive_windows.csv"),
        window_rows,
        [
            "run_id",
            "case",
            "initial_alpha",
            "window",
            "alpha_before",
            "alpha_after",
            "max_allowed",
            "safe_windows",
            "jobs",
            "miss",
            "miss_per_1000",
            "action",
        ],
    )

    write_csv(
        os.path.join(out_dir, "adaptive_summary.csv"),
        summary_rows,
        [
            "run_id",
            "case",
            "initial_alpha",
            "final_alpha",
            "control_jobs",
            "control_miss",
            "control_miss_rate",
            "ai_work",
            "logger_work",
            "control_tick_share",
            "ai_tick_share",
            "logger_tick_share",
        ],
    )

    print_summary(summary_rows)

    for run in runs:
        plot_trace(run, out_dir)

    plot_summary(summary_rows, out_dir)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())