#!/usr/bin/env python3
"""
plot_jvm_bench.py -- Plot RmikuOS JVM AOT vs Interpreter benchmarks

Usage:
    python3 plot_jvm_bench.py

Requires:
    pip install matplotlib

Input files (plain text, no extension):
    ./logs/jvm/old_riscv
    ./logs/jvm/old_loongarch
    ./logs/jvm/new_riscv
    ./logs/jvm/new_loongarch

Output:
    ./logs/jvm/bench_abs.png      -- Absolute time (linear scale)
    ./logs/jvm/bench_speedup.png  -- Speedup ratio
    ./logs/jvm/bench_arch.png     -- Cross-architecture comparison
"""

import re
import matplotlib.pyplot as plt
import numpy as np
from pathlib import Path

# ---------- Config ----------
LOG_DIR = Path("./logs/jvm")
BENCHES = [
    "alu_mix",
    "array_rw",
    "branch_heavy",
    "static_call",
    "mul_lcg",
    "object_field",
    "string_ldc",
]

LABELS = {
    "old_riscv":      "RISC-V INT",
    "old_loongarch":  "LoongArch INT",
    "new_riscv":      "RISC-V AOT",
    "new_loongarch":  "LoongArch AOT",
}

COLORS = {
    "old_riscv":      "#E74C3C",
    "old_loongarch":  "#E67E22",
    "new_riscv":      "#2ECC71",
    "new_loongarch":  "#3498DB",
}

# ---------- Parse logs ----------
def parse_log(path: Path) -> dict:
    if not path.exists():
        print(f"[warn] missing: {path}")
        return {}
    text = path.read_text()
    pattern = r'\[BENCH-BEGIN\]\s+(\S+).*?\[time\].*?\(\s*(\d+)\s*ms\)'
    found = {}
    for m in re.finditer(pattern, text, re.DOTALL):
        found[m.group(1)] = int(m.group(2))
    return found

data = {k: parse_log(LOG_DIR / k) for k in LABELS}

for key, vals in data.items():
    missing = [b for b in BENCHES if b not in vals]
    if missing:
        print(f"[warn] {key} missing: {missing}")

# ---------- Figure 1: Absolute time (LINEAR scale) ----------
fig, ax = plt.subplots(figsize=(14, 6))

x = np.arange(len(BENCHES))
width = 0.18
offset = 0

for key in LABELS:
    vals = [data[key].get(b, 0) for b in BENCHES]
    bars = ax.bar(x + offset, vals, width, label=LABELS[key], color=COLORS[key])
    for bar, v in zip(bars, vals):
        if v > 0:
            ax.text(bar.get_x() + bar.get_width()/2, bar.get_height() + 50,
                    str(v), ha='center', va='bottom', fontsize=7, rotation=90)
    offset += width

ax.set_ylabel("Time (ms)")
ax.set_title("RmikuOS JVM Benchmark -- Absolute Time (Linear Scale)")
ax.set_xticks(x + width * 1.5)
ax.set_xticklabels(BENCHES, rotation=30, ha='right')
ax.legend(loc='upper right')
ax.grid(axis='y', linestyle='--', alpha=0.3)
# Linear scale: bars are proportional to actual time
# Small bars may look tiny, but value labels show exact numbers
max_val = max(max(v for v in d.values()) for d in data.values() if d)
ax.set_ylim(0, max_val * 1.15)

plt.tight_layout()
out1 = LOG_DIR / "bench_abs.png"
plt.savefig(out1, dpi=300)
print(f"saved: {out1}")
plt.close()

# ---------- Figure 2: Speedup ----------
fig, ax = plt.subplots(figsize=(10, 5))

speedup_riscv = []
speedup_loongarch = []
for b in BENCHES:
    old_r = data["old_riscv"].get(b, 0)
    new_r = data["new_riscv"].get(b, 0)
    old_l = data["old_loongarch"].get(b, 0)
    new_l = data["new_loongarch"].get(b, 0)
    speedup_riscv.append(old_r / new_r if new_r > 0 else 0)
    speedup_loongarch.append(old_l / new_l if new_l > 0 else 0)

x = np.arange(len(BENCHES))
width = 0.35

bars1 = ax.bar(x - width/2, speedup_riscv, width, label="RISC-V Speedup", color=COLORS["new_riscv"])
bars2 = ax.bar(x + width/2, speedup_loongarch, width, label="LoongArch Speedup", color=COLORS["new_loongarch"])

for bar in bars1:
    h = bar.get_height()
    if h > 0:
        ax.text(bar.get_x() + bar.get_width()/2, h, f'{h:.1f}x',
                ha='center', va='bottom', fontsize=8)
for bar in bars2:
    h = bar.get_height()
    if h > 0:
        ax.text(bar.get_x() + bar.get_width()/2, h, f'{h:.1f}x',
                ha='center', va='bottom', fontsize=8)

ax.axhline(y=1, color='gray', linestyle='--', linewidth=1)
ax.set_ylabel("Speedup (x)")
ax.set_title("AOT Speedup = Interpreter Time / AOT Time")
ax.set_xticks(x)
ax.set_xticklabels(BENCHES, rotation=30, ha='right')
ax.legend()
ax.grid(axis='y', linestyle='--', alpha=0.3)

plt.tight_layout()
out2 = LOG_DIR / "bench_speedup.png"
plt.savefig(out2, dpi=300)
print(f"saved: {out2}")
plt.close()

# ---------- Figure 3: Cross-arch ratio (AOT) ----------
fig, ax = plt.subplots(figsize=(10, 5))

ratio = []
for b in BENCHES:
    new_r = data["new_riscv"].get(b, 0)
    new_l = data["new_loongarch"].get(b, 0)
    ratio.append(new_l / new_r if new_r > 0 else 0)

bars = ax.bar(BENCHES, ratio, color="#9B59B6")
for bar in bars:
    h = bar.get_height()
    if h > 0:
        ax.text(bar.get_x() + bar.get_width()/2, h, f'{h:.1f}x',
                ha='center', va='bottom', fontsize=8)

ax.axhline(y=1, color='gray', linestyle='--', linewidth=1)
ax.set_ylabel("LoongArch Time / RISC-V Time")
ax.set_title("AOT Cross-Architecture Performance (>1 means LoongArch is slower)")
ax.set_xticklabels(BENCHES, rotation=30, ha='right')
ax.grid(axis='y', linestyle='--', alpha=0.3)

plt.tight_layout()
out3 = LOG_DIR / "bench_arch.png"
plt.savefig(out3, dpi=300)
print(f"saved: {out3}")
plt.close()

print("done.")