#!/usr/bin/env python3
import csv
import math
import os
import re
import sys
from collections import defaultdict

import matplotlib.pyplot as plt


RUN_RE = re.compile(
    r"\[stride_ticket\]\s+run\s+procs=(?P<procs>\d+)\s+tickets=(?P<tickets>[0-9,]+)"
)

SAMPLE_RE = re.compile(
    r"\[stride_sample\]\s+role=(?P<role>\d+)"
    r"\s+pid=(?P<pid>\d+)"
    r"\s+tickets=(?P<tickets>\d+)"
    r"\s+expected_per_1000=(?P<expected_per_1000>\d+)"
    r"\s+effective=(?P<effective>\d+)"
    r"\s+ready=(?P<ready>\d+)"
    r"\s+run_ticks=(?P<run_ticks>\d+)"
    r"\s+stride=(?P<stride>\d+)"
    r"\s+pass=(?P<pass>\d+)"
)

RESULT_RE = re.compile(
    r"\[stride_result\]\s+role=(?P<role>\d+)"
    r"\s+pid=(?P<pid>\d+)"
    r"\s+tickets=(?P<tickets>\d+)"
    r"\s+work=(?P<work>\d+)"
)


def mean(xs):
    if not xs:
        return 0.0
    return sum(xs) / len(xs)


def std(xs):
    if len(xs) <= 1:
        return 0.0

    m = mean(xs)
    return math.sqrt(sum((x - m) ** 2 for x in xs) / (len(xs) - 1))


def parse_tickets(s):
    return [int(x) for x in s.split(",") if x]


def new_run(run_id, m):
    tickets = parse_tickets(m.group("tickets"))

    return {
        "run_id": run_id,
        "procs": int(m.group("procs")),
        "tickets": tickets,
        "case": "_".join(str(x) for x in tickets),
        "samples": {},
        "results": {},
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

            m = SAMPLE_RE.search(line)
            if m:
                role = int(m.group("role"))

                cur["samples"][role] = {
                    "role": role,
                    "pid": int(m.group("pid")),
                    "tickets": int(m.group("tickets")),
                    "expected_per_1000": int(m.group("expected_per_1000")),
                    "effective": int(m.group("effective")),
                    "ready": int(m.group("ready")),
                    "run_ticks": int(m.group("run_ticks")),
                    "stride": int(m.group("stride")),
                    "pass": int(m.group("pass")),
                }

                continue

            m = RESULT_RE.search(line)
            if m:
                role = int(m.group("role"))

                cur["results"][role] = {
                    "role": role,
                    "pid": int(m.group("pid")),
                    "tickets": int(m.group("tickets")),
                    "work": int(m.group("work")),
                }

                continue

    if cur is not None:
        runs.append(cur)

    return runs


def flatten_runs(runs):
    rows = []

    for run in runs:
        roles = sorted(set(run["samples"].keys()) | set(run["results"].keys()))

        total_tickets = sum(run["tickets"])

        total_ticks = 0
        total_work = 0

        for role in roles:
            total_ticks += run["samples"].get(role, {}).get("run_ticks", 0)
            total_work += run["results"].get(role, {}).get("work", 0)

        for role in roles:
            sample = run["samples"].get(role, {})
            result = run["results"].get(role, {})

            tickets = sample.get("tickets", result.get("tickets", 0))
            run_ticks = sample.get("run_ticks", 0)
            work = result.get("work", 0)

            expected_share = tickets / total_tickets if total_tickets > 0 else 0.0
            tick_share = run_ticks / total_ticks if total_ticks > 0 else 0.0
            work_share = work / total_work if total_work > 0 else 0.0

            rows.append({
                "run_id": run["run_id"],
                "case": run["case"],
                "role": role,
                "tickets": tickets,
                "expected_share": expected_share,
                "expected_per_1000": sample.get("expected_per_1000", int(expected_share * 1000)),
                "effective": sample.get("effective", 0),
                "ready": sample.get("ready", 0),
                "run_ticks": run_ticks,
                "work": work,
                "tick_share": tick_share,
                "work_share": work_share,
                "tick_error": tick_share - expected_share,
                "abs_tick_error": abs(tick_share - expected_share),
            })

    return rows


def summarize(rows):
    groups = defaultdict(list)

    for row in rows:
        key = (
            row["case"],
            row["role"],
            row["tickets"],
        )

        groups[key].append(row)

    summary = []

    fields = [
        "expected_share",
        "effective",
        "ready",
        "run_ticks",
        "work",
        "tick_share",
        "work_share",
        "tick_error",
        "abs_tick_error",
    ]

    for key, rs in sorted(groups.items()):
        case, role, tickets = key

        out = {
            "case": case,
            "role": role,
            "tickets": tickets,
            "n": len(rs),
        }

        for f in fields:
            vals = [float(r[f]) for r in rs]

            out[f"{f}_mean"] = mean(vals)
            out[f"{f}_std"] = std(vals)
            out[f"{f}_min"] = min(vals) if vals else 0.0
            out[f"{f}_max"] = max(vals) if vals else 0.0

        summary.append(out)

    return summary


def write_csv(path, rows, fieldnames=None):
    if not rows:
        return

    if fieldnames is None:
        fieldnames = list(rows[0].keys())

    with open(path, "w", newline="", encoding="utf-8") as f:
        writer = csv.DictWriter(f, fieldnames=fieldnames)
        writer.writeheader()

        for row in rows:
            writer.writerow(row)


def group_summary_by_case(summary):
    groups = defaultdict(list)

    for row in summary:
        groups[row["case"]].append(row)

    return groups


def plot_case(case, rows, out_dir):
    rows = sorted(rows, key=lambda r: r["role"])

    labels = [f"role{r['role']}\nT={r['tickets']}" for r in rows]
    xs = list(range(len(rows)))

    expected = [r["expected_share_mean"] for r in rows]
    tick = [r["tick_share_mean"] for r in rows]
    work = [r["work_share_mean"] for r in rows]

    plt.figure(figsize=(8, 4.8))

    plt.plot(xs, expected, marker="o", linestyle="--", label="expected ticket share")
    plt.plot(xs, tick, marker="x", linestyle="-", label="actual tick share")
    plt.plot(xs, work, marker="s", linestyle="-", label="work share")

    plt.xticks(xs, labels)
    plt.ylabel("share")
    plt.ylim(0, 1)
    plt.title(f"Stride baseline: expected vs actual share, case={case}")
    plt.grid(True)
    plt.legend()
    plt.tight_layout()

    path = os.path.join(out_dir, f"stride_share_{case}.png")
    plt.savefig(path, dpi=200)
    plt.close()
    print(f"wrote {path}")

    errors = [r["abs_tick_error_mean"] for r in rows]

    plt.figure(figsize=(8, 4.8))
    plt.bar(xs, errors)
    plt.xticks(xs, labels)
    plt.ylabel("absolute tick share error")
    plt.title(f"Stride scheduling error, case={case}")
    plt.grid(True)
    plt.tight_layout()

    path = os.path.join(out_dir, f"stride_error_{case}.png")
    plt.savefig(path, dpi=200)
    plt.close()
    print(f"wrote {path}")


def plot_all(summary, out_dir):
    os.makedirs(out_dir, exist_ok=True)

    groups = group_summary_by_case(summary)

    for case, rows in sorted(groups.items()):
        plot_case(case, rows, out_dir)


def print_quick_view(summary):
    print()
    print("case        role tickets n  expected  tick_share  work_share  abs_error")
    print("---------- ---- ------- -- --------- ---------- ---------- ---------")

    for r in summary:
        print(
            f"{r['case']:<10} "
            f"{r['role']:<4} "
            f"{r['tickets']:<7} "
            f"{r['n']:<2} "
            f"{r['expected_share_mean']:<9.4f} "
            f"{r['tick_share_mean']:<10.4f} "
            f"{r['work_share_mean']:<10.4f} "
            f"{r['abs_tick_error_mean']:<9.4f}"
        )


def main():
    if len(sys.argv) < 2:
        print("usage: analyze_stride_ticket_log.py <stride_ticket_raw.log> [out_dir]")
        print("example: python3 scripts/analyze_stride_ticket_log.py logs/stride_ticket_raw.log logs")
        return 1

    log_path = sys.argv[1]
    out_dir = sys.argv[2] if len(sys.argv) >= 3 else "logs"

    os.makedirs(out_dir, exist_ok=True)

    runs = parse_log(log_path)
    records = flatten_runs(runs)
    summary = summarize(records)

    records_path = os.path.join(out_dir, "stride_records.csv")
    summary_path = os.path.join(out_dir, "stride_summary.csv")
    figs_dir = os.path.join(out_dir, "figs_stride")

    write_csv(records_path, records)
    write_csv(summary_path, summary)

    print(f"parsed runs={len(runs)}, records={len(records)}, summary rows={len(summary)}")
    print(f"wrote {records_path}")
    print(f"wrote {summary_path}")

    print_quick_view(summary)
    plot_all(summary, figs_dir)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())