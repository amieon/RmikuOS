#!/usr/bin/env python3
import csv
import os
import sys
from collections import defaultdict

import matplotlib.pyplot as plt


def f(x):
    if x == "":
        return 0.0
    return float(x)


def read_summary(path):
    rows = []
    with open(path, "r", encoding="utf-8") as fp:
        reader = csv.DictReader(fp)
        for row in reader:
            row["alpha"] = int(row["alpha"])
            row["role"] = int(row["role"])
            row["threads"] = int(float(row["threads"]))
            row["n"] = int(float(row["n"]))

            for k in [
                "effective_share_mean",
                "tick_share_mean",
                "work_share_mean",
                "effective_mean",
                "run_ticks_mean",
                "work_mean",
            ]:
                row[k] = f(row.get(k, ""))

            rows.append(row)

    return rows


def group_by_case(rows):
    groups = defaultdict(list)

    for row in rows:
        groups[row["threads_case"]].append(row)

    return groups


def plot_effective_vs_tick(case, rows, out_dir):
    roles = sorted(set(r["role"] for r in rows))

    plt.figure(figsize=(8, 5))

    for role in roles:
        rs = [r for r in rows if r["role"] == role]
        rs.sort(key=lambda x: x["alpha"])

        xs = [r["alpha"] for r in rs]
        ys_tick = [r["tick_share_mean"] for r in rs]
        ys_eff = [r["effective_share_mean"] for r in rs]

        threads = rs[0]["threads"]

        plt.plot(
            xs,
            ys_eff,
            marker="o",
            linestyle="--",
            label=f"role {role}, threads={threads}, effective",
        )

        plt.plot(
            xs,
            ys_tick,
            marker="x",
            linestyle="-",
            label=f"role {role}, threads={threads}, tick",
        )

    plt.xlabel("alpha")
    plt.ylabel("share")
    plt.title(f"Alpha effect on CPU share, threads_case={case}")
    plt.ylim(0, 1)
    plt.grid(True)
    plt.legend(fontsize=8)
    plt.tight_layout()

    path = os.path.join(out_dir, f"alpha_effective_vs_tick_{case}.png")
    plt.savefig(path, dpi=200)
    plt.close()

    print(f"wrote {path}")


def plot_work_share(case, rows, out_dir):
    roles = sorted(set(r["role"] for r in rows))

    plt.figure(figsize=(8, 5))

    for role in roles:
        rs = [r for r in rows if r["role"] == role]
        rs.sort(key=lambda x: x["alpha"])

        xs = [r["alpha"] for r in rs]
        ys = [r["work_share_mean"] for r in rs]

        threads = rs[0]["threads"]

        plt.plot(
            xs,
            ys,
            marker="o",
            linestyle="-",
            label=f"role {role}, threads={threads}",
        )

    plt.xlabel("alpha")
    plt.ylabel("work share")
    plt.title(f"Alpha effect on throughput share, threads_case={case}")
    plt.ylim(0, 1)
    plt.grid(True)
    plt.legend(fontsize=8)
    plt.tight_layout()

    path = os.path.join(out_dir, f"alpha_work_share_{case}.png")
    plt.savefig(path, dpi=200)
    plt.close()

    print(f"wrote {path}")


def plot_effective_tickets(case, rows, out_dir):
    roles = sorted(set(r["role"] for r in rows))

    plt.figure(figsize=(8, 5))

    for role in roles:
        rs = [r for r in rows if r["role"] == role]
        rs.sort(key=lambda x: x["alpha"])

        xs = [r["alpha"] for r in rs]
        ys = [r["effective_mean"] for r in rs]

        threads = rs[0]["threads"]

        plt.plot(
            xs,
            ys,
            marker="o",
            linestyle="-",
            label=f"role {role}, threads={threads}",
        )

    plt.xlabel("alpha")
    plt.ylabel("effective tickets")
    plt.title(f"Effective tickets under different alpha, threads_case={case}")
    plt.grid(True)
    plt.legend(fontsize=8)
    plt.tight_layout()

    path = os.path.join(out_dir, f"alpha_effective_tickets_{case}.png")
    plt.savefig(path, dpi=200)
    plt.close()

    print(f"wrote {path}")


def main():
    if len(sys.argv) < 2:
        print("usage: plot_alpha_summary.py <alpha_summary.csv> [out_dir]")
        print("example: python3 scripts/plot_alpha_summary.py logs/alpha_summary.csv logs/figs")
        return 1

    summary_path = sys.argv[1]
    out_dir = sys.argv[2] if len(sys.argv) >= 3 else "logs/figs"

    os.makedirs(out_dir, exist_ok=True)

    rows = read_summary(summary_path)
    groups = group_by_case(rows)

    for case, case_rows in sorted(groups.items()):
        plot_effective_vs_tick(case, case_rows, out_dir)
        plot_work_share(case, case_rows, out_dir)
        plot_effective_tickets(case, case_rows, out_dir)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())