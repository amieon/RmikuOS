#!/usr/bin/env python3
import re
import csv
import sys
import os
import math
from collections import defaultdict
from statistics import mean, pstdev

RUN_RE = re.compile(
    r"\[alpha_arg\]\s+run\s+alpha=(?P<alpha>\d+)\s+procs=(?P<procs>\d+)\s+threads=(?P<threads>[0-9, ]+)"
)

SAMPLE_RE = re.compile(
    r"\[alpha_sample\]\s+"
    r"alpha=(?P<alpha>\d+)\s+"
    r"role=(?P<role>\d+)\s+"
    r"pid=(?P<pid>\d+)\s+"
    r"threads=(?P<threads>\d+)\s+"
    r"tickets=(?P<tickets>\d+)\s+"
    r"effective=(?P<effective>\d+)\s+"
    r"ready=(?P<ready>\d+)\s+"
    r"run_ticks=(?P<run_ticks>\d+)\s+"
    r"stride=(?P<stride>\d+)\s+"
    r"pass=(?P<pass>\d+)"
)

RESULT_RE = re.compile(
    r"\[alpha_result\]\s+"
    r"alpha=(?P<alpha>\d+)\s+"
    r"role=(?P<role>\d+)\s+"
    r"pid=(?P<pid>\d+)\s+"
    r"threads=(?P<threads>\d+)\s+"
    r"tickets=(?P<tickets>\d+)\s+"
    r"work=(?P<work>\d+)"
)


def int_dict(d):
    return {k: int(v) for k, v in d.items()}


def safe_div(a, b):
    return a / b if b else 0.0


def parse_log(path):
    runs = []
    cur = None
    run_id = -1

    with open(path, "r", encoding="utf-8", errors="ignore") as f:
        for line in f:
            line = line.strip()

            m = RUN_RE.search(line)
            if m:
                run_id += 1
                alpha = int(m.group("alpha"))
                procs = int(m.group("procs"))
                threads = [int(x) for x in m.group("threads").replace(" ", "").split(",") if x]

                cur = {
                    "run_id": run_id,
                    "alpha": alpha,
                    "procs": procs,
                    "threads_case": "_".join(str(x) for x in threads),
                    "threads_list": threads,
                    "roles": defaultdict(dict),
                }
                runs.append(cur)
                continue

            if cur is None:
                continue

            m = SAMPLE_RE.search(line)
            if m:
                d = int_dict(m.groupdict())
                role = d["role"]
                cur["roles"][role].update({
                    "sample_pid": d["pid"],
                    "alpha": d["alpha"],
                    "role": role,
                    "threads": d["threads"],
                    "tickets": d["tickets"],
                    "effective": d["effective"],
                    "ready": d["ready"],
                    "run_ticks": d["run_ticks"],
                    "stride": d["stride"],
                    "pass": d["pass"],
                })
                continue

            m = RESULT_RE.search(line)
            if m:
                d = int_dict(m.groupdict())
                role = d["role"]
                cur["roles"][role].update({
                    "result_pid": d["pid"],
                    "alpha": d["alpha"],
                    "role": role,
                    "threads": d["threads"],
                    "tickets": d["tickets"],
                    "work": d["work"],
                })
                continue

    return runs


def flatten_runs(runs):
    rows = []

    for run in runs:
        roles = run["roles"]

        total_ticks = sum(v.get("run_ticks", 0) for v in roles.values())
        total_work = sum(v.get("work", 0) for v in roles.values())
        total_effective = sum(v.get("effective", 0) for v in roles.values())

        for role in sorted(roles.keys()):
            r = roles[role]

            row = {
                "run_id": run["run_id"],
                "alpha": run["alpha"],
                "threads_case": run["threads_case"],
                "role": role,

                "threads": r.get("threads", ""),
                "tickets": r.get("tickets", ""),

                "ready": r.get("ready", ""),
                "effective": r.get("effective", ""),
                "run_ticks": r.get("run_ticks", ""),
                "work": r.get("work", ""),

                "stride": r.get("stride", ""),
                "pass": r.get("pass", ""),

                "tick_share": safe_div(r.get("run_ticks", 0), total_ticks),
                "work_share": safe_div(r.get("work", 0), total_work),
                "effective_share": safe_div(r.get("effective", 0), total_effective),

                "sample_pid": r.get("sample_pid", ""),
                "result_pid": r.get("result_pid", ""),
            }

            rows.append(row)

    return rows


def summarize(rows):
    groups = defaultdict(list)

    for r in rows:
        key = (
            r["alpha"],
            r["threads_case"],
            r["role"],
            r["threads"],
            r["tickets"],
        )
        groups[key].append(r)

    summary = []

    numeric_fields = [
        "ready",
        "effective",
        "run_ticks",
        "work",
        "tick_share",
        "work_share",
        "effective_share",
    ]

    for key, items in sorted(groups.items()):
        alpha, threads_case, role, threads, tickets = key

        out = {
            "alpha": alpha,
            "threads_case": threads_case,
            "role": role,
            "threads": threads,
            "tickets": tickets,
            "n": len(items),
        }

        for field in numeric_fields:
            vals = []
            for x in items:
                v = x.get(field, "")
                if v == "":
                    continue
                vals.append(float(v))

            if vals:
                out[field + "_mean"] = mean(vals)
                out[field + "_std"] = pstdev(vals) if len(vals) > 1 else 0.0
                out[field + "_min"] = min(vals)
                out[field + "_max"] = max(vals)
            else:
                out[field + "_mean"] = ""
                out[field + "_std"] = ""
                out[field + "_min"] = ""
                out[field + "_max"] = ""

        summary.append(out)

    return summary


def write_csv(path, rows):
    if not rows:
        print(f"[analyze] no rows for {path}")
        return

    os.makedirs(os.path.dirname(path) or ".", exist_ok=True)

    fields = list(rows[0].keys())

    with open(path, "w", newline="", encoding="utf-8") as f:
        w = csv.DictWriter(f, fieldnames=fields)
        w.writeheader()
        w.writerows(rows)

    print(f"[analyze] wrote {path}, rows={len(rows)}")


def print_quick_view(summary):
    print()
    print("=== quick view: tick_share_mean / work_share_mean ===")

    for r in summary:
        print(
            "alpha={:<3} case={:<8} role={} threads={:<2} n={:<2} "
            "eff={:<8.2f} tick_share={:<7.3f} work_share={:<7.3f} "
            "ticks={:<8.2f} work={:<10.2f}".format(
                int(r["alpha"]),
                r["threads_case"],
                r["role"],
                int(r["threads"]) if r["threads"] != "" else -1,
                int(r["n"]),
                float(r["effective_mean"]) if r["effective_mean"] != "" else 0.0,
                float(r["tick_share_mean"]) if r["tick_share_mean"] != "" else 0.0,
                float(r["work_share_mean"]) if r["work_share_mean"] != "" else 0.0,
                float(r["run_ticks_mean"]) if r["run_ticks_mean"] != "" else 0.0,
                float(r["work_mean"]) if r["work_mean"] != "" else 0.0,
            )
        )


def main():
    if len(sys.argv) < 2:
        print("usage: analyze_alpha_log.py <raw_log> [out_dir]")
        print("example: python3 scripts/analyze_alpha_log.py logs/alpha_matrix_raw.log logs")
        return 1

    raw_log = sys.argv[1]
    out_dir = sys.argv[2] if len(sys.argv) >= 3 else "logs"

    runs = parse_log(raw_log)
    rows = flatten_runs(runs)
    summary = summarize(rows)

    print(f"[analyze] parsed runs={len(runs)}, records={len(rows)}")

    write_csv(os.path.join(out_dir, "alpha_records.csv"), rows)
    write_csv(os.path.join(out_dir, "alpha_summary.csv"), summary)

    print_quick_view(summary)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())