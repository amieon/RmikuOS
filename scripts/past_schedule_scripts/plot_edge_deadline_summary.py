#!/usr/bin/env python3
import csv
import os
import sys
from collections import defaultdict

import matplotlib.pyplot as plt


def to_float(x):
    if x is None or x == "":
        return 0.0
    return float(x)


def to_int(x):
    if x is None or x == "":
        return 0
    return int(float(x))


def read_rows(path):
    rows = []

    with open(path, "r", encoding="utf-8") as f:
        reader = csv.DictReader(f)

        for row in reader:
            row["alpha"] = to_int(row["alpha"])
            row["case"] = row["case"]
            row["role"] = row["role"]
            row["threads"] = to_int(row["threads"])
            row["tickets"] = to_int(row["tickets"])
            row["n"] = to_int(row["n"])

            for k in [
                "ready_mean",
                "effective_mean",
                "run_ticks_mean",
                "work_mean",
                "jobs_mean",
                "miss_mean",
                "miss_rate_mean",
                "tick_share_mean",
                "work_share_mean",
                "effective_share_mean",
            ]:
                row[k] = to_float(row.get(k, ""))

            rows.append(row)

    return rows


def group_by_case(rows):
    groups = defaultdict(list)

    for row in rows:
        groups[row["case"]].append(row)

    return groups


def find_role(rows, alpha, role):
    for row in rows:
        if row["alpha"] == alpha and row["role"] == role:
            return row
    return None


def plot_miss_rate(case, rows, out_dir):
    control_rows = [r for r in rows if r["role"] == "control"]
    control_rows.sort(key=lambda x: x["alpha"])

    if not control_rows:
        return

    xs = [r["alpha"] for r in control_rows]
    ys = [r["miss_rate_mean"] for r in control_rows]

    plt.figure(figsize=(7, 4.5))
    plt.plot(xs, ys, marker="o")
    plt.xlabel("alpha")
    plt.ylabel("control miss rate")
    plt.title(f"Control deadline miss rate, case={case}")
    plt.grid(True)
    plt.tight_layout()

    path = os.path.join(out_dir, f"edge_miss_rate_{case}.png")
    plt.savefig(path, dpi=200)
    plt.close()
    print(f"wrote {path}")


def plot_tick_share(case, rows, out_dir):
    roles = ["control", "ai", "logger"]

    plt.figure(figsize=(7, 4.5))

    for role in roles:
        rs = [r for r in rows if r["role"] == role]
        rs.sort(key=lambda x: x["alpha"])

        if not rs:
            continue

        xs = [r["alpha"] for r in rs]
        ys = [r["tick_share_mean"] for r in rs]
        threads = rs[0]["threads"]

        plt.plot(xs, ys, marker="o", label=f"{role}, threads={threads}")

    plt.xlabel("alpha")
    plt.ylabel("tick share")
    plt.title(f"CPU tick share by role, case={case}")
    plt.ylim(0, 1)
    plt.grid(True)
    plt.legend()
    plt.tight_layout()

    path = os.path.join(out_dir, f"edge_tick_share_{case}.png")
    plt.savefig(path, dpi=200)
    plt.close()
    print(f"wrote {path}")


def plot_work_mean(case, rows, out_dir):
    roles = ["ai", "logger"]

    plt.figure(figsize=(7, 4.5))

    for role in roles:
        rs = [r for r in rows if r["role"] == role]
        rs.sort(key=lambda x: x["alpha"])

        if not rs:
            continue

        xs = [r["alpha"] for r in rs]
        ys = [r["work_mean"] for r in rs]
        threads = rs[0]["threads"]

        plt.plot(xs, ys, marker="o", label=f"{role}, threads={threads}")

    plt.xlabel("alpha")
    plt.ylabel("work mean")
    plt.title(f"Throughput workload, case={case}")
    plt.grid(True)
    plt.legend()
    plt.tight_layout()

    path = os.path.join(out_dir, f"edge_work_mean_{case}.png")
    plt.savefig(path, dpi=200)
    plt.close()
    print(f"wrote {path}")


def plot_effective_vs_tick(case, rows, out_dir):
    roles = ["control", "ai", "logger"]

    plt.figure(figsize=(8, 5))

    for role in roles:
        rs = [r for r in rows if r["role"] == role]
        rs.sort(key=lambda x: x["alpha"])

        if not rs:
            continue

        xs = [r["alpha"] for r in rs]
        ys_eff = [r["effective_share_mean"] for r in rs]
        ys_tick = [r["tick_share_mean"] for r in rs]
        threads = rs[0]["threads"]

        plt.plot(xs, ys_eff, marker="o", linestyle="--",
                 label=f"{role}, threads={threads}, effective")
        plt.plot(xs, ys_tick, marker="x", linestyle="-",
                 label=f"{role}, threads={threads}, tick")

    plt.xlabel("alpha")
    plt.ylabel("share")
    plt.title(f"Effective share vs actual tick share, case={case}")
    plt.ylim(0, 1)
    plt.grid(True)
    plt.legend(fontsize=8)
    plt.tight_layout()

    path = os.path.join(out_dir, f"edge_effective_vs_tick_{case}.png")
    plt.savefig(path, dpi=200)
    plt.close()
    print(f"wrote {path}")


def plot_tradeoff(case, rows, out_dir):
    alphas = sorted(set(r["alpha"] for r in rows))

    xs = []
    ys = []
    labels = []

    for alpha in alphas:
        control = find_role(rows, alpha, "control")
        ai = find_role(rows, alpha, "ai")

        if control is None or ai is None:
            continue

        xs.append(control["miss_rate_mean"])
        ys.append(ai["work_mean"])
        labels.append(alpha)

    if not xs:
        return

    plt.figure(figsize=(7, 4.5))
    plt.plot(xs, ys, marker="o")

    for x, y, alpha in zip(xs, ys, labels):
        plt.text(x, y, f" α={alpha}", fontsize=8)

    plt.xlabel("control miss rate")
    plt.ylabel("AI work mean")
    plt.title(f"Deadline-throughput trade-off, case={case}")
    plt.grid(True)
    plt.tight_layout()

    path = os.path.join(out_dir, f"edge_tradeoff_{case}.png")
    plt.savefig(path, dpi=200)
    plt.close()
    print(f"wrote {path}")


def print_case_summary(case, rows):
    print()
    print(f"=== case {case} ===")
    print("alpha  control_miss_rate  control_tick  ai_tick  ai_work  logger_tick  logger_work")

    alphas = sorted(set(r["alpha"] for r in rows))

    for alpha in alphas:
        control = find_role(rows, alpha, "control")
        ai = find_role(rows, alpha, "ai")
        logger = find_role(rows, alpha, "logger")

        if control is None or ai is None or logger is None:
            continue

        print(
            f"{alpha:<5} "
            f"{control['miss_rate_mean']:<18.4f} "
            f"{control['tick_share_mean']:<13.4f} "
            f"{ai['tick_share_mean']:<8.4f} "
            f"{ai['work_mean']:<8.2f} "
            f"{logger['tick_share_mean']:<12.4f} "
            f"{logger['work_mean']:<8.2f}"
        )


def main():
    if len(sys.argv) < 2:
        print("usage: plot_edge_deadline_summary.py <edge_deadline_summary.csv> [out_dir]")
        print("example: python3 scripts/plot_edge_deadline_summary.py logs/edge_deadline_summary.csv logs/figs_edge")
        return 1

    csv_path = sys.argv[1]
    out_dir = sys.argv[2] if len(sys.argv) >= 3 else "logs/figs_edge"

    os.makedirs(out_dir, exist_ok=True)

    rows = read_rows(csv_path)
    groups = group_by_case(rows)

    for case, case_rows in sorted(groups.items()):
        print_case_summary(case, case_rows)

        plot_miss_rate(case, case_rows, out_dir)
        plot_tick_share(case, case_rows, out_dir)
        plot_work_mean(case, case_rows, out_dir)
        plot_effective_vs_tick(case, case_rows, out_dir)
        plot_tradeoff(case, case_rows, out_dir)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())