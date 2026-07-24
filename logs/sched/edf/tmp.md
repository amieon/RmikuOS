# 00 · 校准：纯 EDF 基线（α=1，无退避）

## 目的

验证实验环境可用，并建立"无保护"基线：
- 调度框架能正常拉起三组负载、输出完整 CSV
- α=1 下 ctrl 的 deadline miss rate ≈ 100%（证明自适应 α 的必要性）

## 运行

```bash
./sched/schedlab edf
```

## 统计

```bash
python3 ./scripts/sched/stat_exp0.py ./logs/sched/edf/sexp0_edf.csv
# 多 rep:
python3 ./scripts/sched/stat_exp0.py ./logs/sched/edf/sexp0_edf_r{1,2,3}.csv
```

输出：stdout 摘要 + `exp0_miss_rate.png` / `exp0_cpu_share.png` / `exp0_summary.png`

## 通过标准

| 指标 | 期望 |
|------|------|
| ctrl miss rate | > 95% |
| ctrl CPU share | < 25%（远低于 tickets 应得的 66.7%） |
| W/D/S/J 行均非空 | 框架正常 |

## 实测结果（2026-07-25，单 rep）

```
ctrl: jobs=5599  miss=5598  miss_rate=99.98%
      avg_late=6703  late_max=11860  avg_resp=6706
CPU share: ctrl=17.6%  ai=54.9%  log=27.5%
Jain index: mean=0.855
```

[exp0_miss_rate](.logs/sched/edf/exp0_miss_rate.png)

结论：纯 EDF 下 ctrl 几乎全部 miss，CPU 被 spin 负载挤占。基线成立。

## 产出文件

```
logs/sched/edf/
├── sexp0_edf.csv          # 原始输出
├── exp0_miss_rate.png     # 逐窗口 ctrl miss rate
├── exp0_cpu_share.png     # 逐窗口 CPU 份额
└── exp0_summary.png       # 汇总柱状图
```

