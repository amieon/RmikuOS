#!/usr/bin/env python3
# plot_tcp.py —— 从 loss_sweep.csv 和 *.events.txt 生成 TCP 实验图
#
# 用法(在 RmikuOS 仓库根目录):
#   python3 scripts/plot_tcp.py                      # 全自动
#   python3 scripts/plot_tcp.py --cwnd-file logs/tcp/cubic-l100-s1M.events.txt
#
# 输入:
#   logs/tcp/loss_sweep.csv
#   logs/tcp/*.events.txt
# 输出:
#   logs/tcp/figs/fig1_cwnd_sawtooth.png   cwnd 锯齿(需当时 CWND_TRACE=true 采过 [cwnd])
#   logs/tcp/figs/fig2_recovery.png        重传路径对比:RTO vs 快速重传
#   logs/tcp/figs/fig3_loss_accuracy.png   丢包装置精度:实测降窗数 vs 理论丢包数
#   logs/tcp/figs/fig4_time_vs_loss.png    耗时 vs 丢包率(带噪声警告)
#   logs/tcp/figs/fig5_karn.png            rtt 样本数随丢包率下降(Karn 规则)

import argparse
import csv
import re
from pathlib import Path

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

SEG_PAYLOAD = 1460          # MAX_PAYLOAD
SIZE_SEGS = {"64K": 65536 // SEG_PAYLOAD, "256K": 262144 // SEG_PAYLOAD,
             "1M": 1048576 // SEG_PAYLOAD}


def load_csv(path):
    rows = []
    with open(path) as f:
        for r in csv.DictReader(f):
            r = {k.strip(): v.strip() for k, v in r.items()}
            for k in ("loss", "runs", "conn", "rtx_rto", "rtx_fast",
                      "loss_events", "rtt_sample", "rtt_samples"):
                if k in r:
                    try:
                        r[k.replace("rtt_samples", "rtt_sample")] = int(r[k])
                    except (ValueError, KeyError):
                        pass
            r["median_s"] = float(r["median_s"])
            r["mean_s"] = float(r["mean_s"])
            rows.append(r)
    return rows


def pick(rows, version, size):
    out = [r for r in rows if r["version"] == version and r["size"] == size
           and int(r.get("conn", 0)) > 0]
    out.sort(key=lambda r: int(r["loss"]))
    return out


# ---------- fig1: cwnd 锯齿 ----------
CWND_RE = re.compile(r"\[cwnd\] t=(\d+) cwnd=(\d+) ssthresh=(\d+) why=(\w+)")


def fig_cwnd(events_dir, out_dir, cwnd_file=None):
    if cwnd_file is None:
        best, best_n = None, 0
        for p in events_dir.glob("cubic*.events.txt"):
            m = re.search(r"-l(\d+)-", p.name)
            if not m or int(m.group(1)) == 0:
                continue  # 锯齿必须来自丢包档,l0 的"锯齿"是跨连接拼接假象
            n = sum(1 for line in p.open(errors="ignore") if "[cwnd]" in line)
            if n > best_n:
                best, best_n = p, n
        if best is None:
            print("[fig1] loss>0 的 events 里没有 [cwnd];"
                  "请 CWND_TRACE=true 编一次,curl 单发一个 1M 采集")
            return
        cwnd_file = best
    ts, cw, loss_t, loss_w = [], [], [], []
    for line in open(cwnd_file, errors="ignore"):
        if "[tcp-stat]" in line and ts:
            break  # 只画第一条连接,多条连接拼接会产生假锯齿
        m = CWND_RE.search(line)
        if not m:
            continue
        t, w, why = int(m[1]), int(m[2]), m[4]
        ts.append(t); cw.append(w)
        if why == "loss":
            loss_t.append(t); loss_w.append(w)
    if not ts:
        print(f"[fig1] {cwnd_file} 里没有 [cwnd] 行,跳过")
        return
    t0 = ts[0]
    ts = [(t - t0) / 1000.0 for t in ts]
    loss_t = [(t - t0) / 1000.0 for t in loss_t]

    fig, ax = plt.subplots(figsize=(9, 4.5))
    ax.plot(ts, cw, lw=0.9, color="#1f77b4", label="cwnd")
    if loss_t:
        ax.scatter(loss_t, loss_w, color="red", zorder=5, s=28,
                   label=f"loss event (x0.7, n={len(loss_t)})")
    ax.set_xlabel("time since connection start (s)")
    ax.set_ylabel("cwnd (segments)")
    ax.set_title(f"CUBIC sawtooth — {Path(cwnd_file).name}")
    ax.legend()
    ax.grid(alpha=0.3)
    fig.tight_layout()
    fig.savefig(out_dir / "fig1_cwnd_sawtooth.png", dpi=150)
    print(f"[fig1] {len(loss_t)} 次降窗 -> fig1_cwnd_sawtooth.png  (源: {Path(cwnd_file).name})")


# ---------- fig2: 重传路径对比 ----------
def fig_recovery(rows, out_dir, size="1M"):
    old, cub = pick(rows, "old", size), pick(rows, "cubic", size)
    losses = sorted({int(r["loss"]) for r in old + cub} - {0})
    if not losses:
        print("[fig2] 无丢包数据,跳过"); return
    om = {int(r["loss"]): r for r in old}
    cm = {int(r["loss"]): r for r in cub}
    x = range(len(losses))
    w = 0.38
    old_rto = [om[l]["rtx_rto"] if l in om else 0 for l in losses]
    cub_fast = [cm[l]["rtx_fast"] if l in cm else 0 for l in losses]
    cub_rto = [cm[l]["rtx_rto"] if l in cm else 0 for l in losses]

    fig, ax = plt.subplots(figsize=(8, 4.5))
    ax.bar([i - w / 2 for i in x], old_rto, w, label="old: RTO retransmit",
           color="#d62728")
    ax.bar([i + w / 2 for i in x], cub_fast, w, label="cubic: fast retransmit",
           color="#2ca02c")
    ax.bar([i + w / 2 for i in x], cub_rto, w, bottom=cub_fast,
           label="cubic: RTO retransmit", color="#ff9896")
    ax.set_xticks(list(x))
    ax.set_xticklabels([f"1/{l}" for l in losses])
    ax.set_xlabel("loss rate (one drop per N segments)")
    ax.set_ylabel("retransmissions (7 connections)")
    ax.set_title(f"Recovery path: RTO vs fast retransmit — {size}")
    ax.legend()
    ax.grid(axis="y", alpha=0.3)
    fig.tight_layout()
    fig.savefig(out_dir / "fig2_recovery.png", dpi=150)
    print("[fig2] -> fig2_recovery.png")


# ---------- fig3: 丢包装置精度 ----------
def fig_accuracy(rows, out_dir, size="1M"):
    cub = [r for r in pick(rows, "cubic", size) if int(r["loss"]) > 0]
    if not cub:
        print("[fig3] 无丢包数据,跳过"); return
    losses = [int(r["loss"]) for r in cub]
    theory = [SIZE_SEGS[size] / l for l in losses]
    actual = [r["loss_events"] / int(r["conn"]) for r in cub]

    fig, ax = plt.subplots(figsize=(6, 6))
    ax.plot([0, max(theory) * 1.1], [0, max(theory) * 1.1], "--",
            color="gray", label="ideal (measured = theory)")
    ax.scatter(theory, actual, s=60, color="#1f77b4", zorder=5,
               label="measured (loss_events / conn)")
    for t, a, l in zip(theory, actual, losses):
        ax.annotate(f"1/{l}", (t, a), textcoords="offset points",
                    xytext=(8, 4), fontsize=9)
    ax.set_xlabel(f"theoretical drops per connection ({SIZE_SEGS[size]} segs / LOSS)")
    ax.set_ylabel("measured loss events per connection")
    ax.set_title(f"Loss injector accuracy — cubic {size}")
    ax.legend()
    ax.grid(alpha=0.3)
    fig.tight_layout()
    fig.savefig(out_dir / "fig3_loss_accuracy.png", dpi=150)
    print("[fig3] -> fig3_loss_accuracy.png")


# ---------- fig4: 耗时 vs 丢包率 ----------
def fig_time(rows, out_dir, sizes=("64K", "256K", "1M")):
    fig, axes = plt.subplots(1, len(sizes), figsize=(5 * len(sizes), 4.2))
    for ax, size in zip(axes, sizes):
        for ver, color, marker in (("old", "#d62728", "o"),
                                   ("cubic", "#1f77b4", "s")):
            rs = pick(rows, ver, size)
            if not rs:
                continue
            xs = [int(r["loss"]) for r in rs]
            ys = [r["median_s"] for r in rs]
            ax.plot(xs, ys, marker=marker, color=color, label=ver)
        ax.set_xscale("log")
        ax.set_xlabel("LOSS_EVERY (log)")
        ax.set_ylabel("median time (s)")
        ax.set_title(size)
        ax.grid(alpha=0.3, which="both")
        ax.legend()
    fig.suptitle("Transfer time vs loss rate  "
                 "(WARNING: cross-row session drift, e.g. 1M@l200 — "
                 "see report limitations)", fontsize=10)
    fig.tight_layout()
    fig.savefig(out_dir / "fig4_time_vs_loss.png", dpi=150)
    print("[fig4] -> fig4_time_vs_loss.png")


# ---------- fig5: Karn 规则 ----------
def fig_karn(rows, out_dir, size="1M"):
    fig, ax = plt.subplots(figsize=(7, 4.5))
    for ver, color, marker in (("old", "#d62728", "o"), ("cubic", "#1f77b4", "s")):
        rs = pick(rows, ver, size)
        if not rs:
            continue
        xs = [int(r["loss"]) for r in rs]
        ys = [int(r.get("rtt_sample", 0)) for r in rs]
        ax.plot(xs, ys, marker=marker, color=color, label=ver)
    ax.set_xscale("log")
    ax.set_xlabel("LOSS_EVERY (log)")
    ax.set_ylabel("RTT samples (7 connections)")
    ax.set_title(f"Karn's rule: retransmitted segments yield no RTT samples — {size}")
    ax.grid(alpha=0.3, which="both")
    ax.legend()
    fig.tight_layout()
    fig.savefig(out_dir / "fig5_karn.png", dpi=150)
    print("[fig5] -> fig5_karn.png")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--csv", default="logs/tcp/loss_sweep.csv")
    ap.add_argument("--events-dir", default="logs/tcp")
    ap.add_argument("--out", default="logs/tcp/figs")
    ap.add_argument("--cwnd-file", default=None,
                    help="画锯齿用的 events 文件;默认自动挑 [cwnd] 行最多的 cubic 文件")
    args = ap.parse_args()

    out_dir = Path(args.out)
    out_dir.mkdir(parents=True, exist_ok=True)
    rows = load_csv(args.csv)
    print(f"读取 {args.csv}: {len(rows)} 行")

    fig_cwnd(Path(args.events_dir), out_dir, args.cwnd_file)
    fig_recovery(rows, out_dir)
    fig_accuracy(rows, out_dir)
    fig_time(rows, out_dir)
    fig_karn(rows, out_dir)
    print(f"全部输出到 {out_dir}/")


if __name__ == "__main__":
    main()