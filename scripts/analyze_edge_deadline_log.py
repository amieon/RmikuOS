#!/usr/bin/env python3
import re
import csv
import os
import sys
from collections import defaultdict
from statistics import mean, pstdev

RUN_RE = re.compile(
    r"\[edge_deadline\]\s+run\s+alpha=(?P<alpha>\d+)"
)

SAMPLE_RE = re.compile(
    r"\[edge_sample\]\s+"
    r"alpha=(?P<alpha>\d+)\s+"
    r"role=(?P<role>[A-Za-z0-9_]+)\s+"
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
    r"\[edge_deadline\]\s+"
    r"alpha=(?P<alpha>\d+)\s+"
    r"role=(?P<role>[A-Za-z0-9_]+)\s+"
    r"pid=(?P<pid>\d+)\s+"
    r"threads=(?P<threads>\d+)\s+"
    r"tickets=(?P<tickets>\d+)\s+"
    r"effective=(?P<effective>\d+)\s+"
    r"ready=(?P<ready>\d+)\s+"
    r"run_ticks=(?P<run_ticks>\d+)\s+"
    r"work=(?P<work>\d+)\s+"
    r"jobs=(?P<jobs>\d+)\s+"
    r"miss=(?P<miss>\d+)"
)


def to_int_dict(d):
    out = {}
    for k, v in d.items():
        if k == "role":
            out[k] = v
        else:
            out[k] = int(v)
    return out


def norm_role(role):
    if role.endswith("_result"):
        return role[:-len("_result")]
    return role


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
                cur = {
                    "run_id": run_id,
                    "alpha": int(m.group("alpha")),
                    "roles": defaultdict(dict),
                }
                runs.append(cur)
                continue

            if cur is None:
                continue

            m = SAMPLE_RE.search(line)
            if m:
                d = to_int_dict(m.groupdict())
                role = norm_role(d["role"])

                cur["roles"][role].update({
                    "alpha": d["alpha"],
                    "role": role,
                    "sample_pid": d["pid"],
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
                d = to_int_dict(m.groupdict())
                role = norm_role(d["role"])

                cur["roles"][role].update({
                    "alpha": d["alpha"],
                    "role": role,
                    "result_pid": d["pid"],
                    "threads": d["threads"],
                    "tickets": d["tickets"],
                    "work": d["work"],
                    "jobs": d["jobs"],
                    "miss": d["miss"],
                })
                continue

    return runs


def flatten_runs(runs):
    rows = []

    for run in runs:
        roles = run["roles"]

        total_ticks = sum(r.get("run_ticks", 0) for r in roles.values())
        total_work = sum(r.get("work", 0) for r in roles.values())
        total_effective = sum(r.get("effective", 0) for r in roles.values())

        for role in sorted(roles.keys()):
            r = roles[role]

            jobs = r.get("jobs", 0)
            miss = r.get("miss", 0)

            row = {
                "run_id": run["run_id"],
                "alpha": run["alpha"],
                "role": role,

                "threads": r.get("threads", ""),
                "tickets": r.get("tickets", ""),

                "ready": r.get("ready", ""),
                "effective": r.get("effective", ""),
                "run_ticks": r.get("run_ticks", ""),
                "work": r.get("work", ""),

                "jobs": jobs,
                "miss": miss,
                "miss_rate": safe_div(miss, jobs),

                "tick_share": safe_div(r.get("run_ticks", 0), total_ticks),
                "work_share": safe_div(r.get("work", 0), total_work),
                "effective_share": safe_div(r.get("effective", 0), total_effective),

                "stride": r.get("stride", ""),
                "pass": r.get("pass", ""),

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
            r["role"],
            r["threads"],
            r["tickets"],
        )
        groups[key].append(r)

    fields = [
        "ready",
        "effective",
        "run_ticks",
        "work",
        "jobs",
        "miss",
        "miss_rate",
        "tick_share",
        "work_share",
        "effective_share",
    ]

    summary = []

    for key, items in sorted(groups.items()):
        alpha, role, threads, tickets = key

        out = {
            "alpha": alpha,
            "role": role,
            "threads": threads,
            "tickets": tickets,
            "n": len(items),
        }

        for field in fields:
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
        print(f"[edge_analyze] no rows for {path}")
        return

    os.makedirs(os.path.dirname(path) or ".", exist_ok=True)

    fields = list(rows[0].keys())

    with open(path, "w", newline="", encoding="utf-8") as f:
        w = csv.DictWriter(f, fieldnames=fields)
        w.writeheader()
        w.writerows(rows)

    print(f"[edge_analyze] wrote {path}, rows={len(rows)}")


def print_quick_view(summary):
    print()
    print("=== edge deadline quick view ===")
    print("alpha role      n  threads  miss_rate  miss/jobs       tick_share  work_mean")

    for r in summary:
        alpha = int(r["alpha"])
        role = r["role"]
        n = int(r["n"])
        threads = r["threads"]

        miss_rate = float(r["miss_rate_mean"]) if r["miss_rate_mean"] != "" else 0.0
        miss = float(r["miss_mean"]) if r["miss_mean"] != "" else 0.0
        jobs = float(r["jobs_mean"]) if r["jobs_mean"] != "" else 0.0
        tick_share = float(r["tick_share_mean"]) if r["tick_share_mean"] != "" else 0.0
        work = float(r["work_mean"]) if r["work_mean"] != "" else 0.0

        print(
            f"{alpha:<5} {role:<9} {n:<2} {threads:<7} "
            f"{miss_rate:<10.4f} {miss:.2f}/{jobs:.2f}      "
            f"{tick_share:<10.4f} {work:.2f}"
        )


def main():
    if len(sys.argv) < 2:
        print("usage: analyze_edge_deadline_log.py <raw_log> [out_dir]")
        print("example: python3 scripts/analyze_edge_deadline_log.py logs/edge_deadline_raw.log logs")
        return 1

    raw_log = sys.argv[1]
    out_dir = sys.argv[2] if len(sys.argv) >= 3 else "logs"

    runs = parse_log(raw_log)
    rows = flatten_runs(runs)
    summary = summarize(rows)

    print(f"[edge_analyze] parsed runs={len(runs)}, records={len(rows)}")

    write_csv(os.path.join(out_dir, "edge_deadline_records.csv"), rows)
    write_csv(os.path.join(out_dir, "edge_deadline_summary.csv"), summary)

    print_quick_view(summary)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())