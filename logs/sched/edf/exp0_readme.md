# 00 · 基线 EDF（α=1，无退避）

## 目的

建立"无保护"基线，验证自适应 α 的必要性：
- 调度框架能正常拉起三组负载、输出完整 CSV
- α=1（无缩放）下 stride 公平分配 CPU（ctrl 拿 ~2/3）
- 但 ai 14 线程抢占导致 ctrl 的 burn 被打碎，deadline 100% miss
- → 需要 α 旋钮在多线程进程和 deadline 进程之间调节

## 配置

| 任务 | tickets | 线程 | 类型 | period | burn | 备注 |
|------|---------|------|------|--------|------|------|
| ctrl | 300 | 1 | in-parent jobs | 4 | 400000 | deadline 任务，tickets 占 2/3 |
| ai | 100 | 14 | spin | — | 12000 | 14 线程全活跃，抢占 ctrl |
| log | 50 | 8 | spin | — | 12000 | 后台负载 |

- 平台：riscv64 (QEMU), SMP=1, 1 tick = 17.51 ms
- total = 36000 ticks（~10.5 分钟/rep）
- α = 1 固定（scale(n,1) ≈ 1，effective ≈ tickets）
- 6 reps（1 warmup + 5 formal），Window 1 跳过（启动噪声）

## 运行

```bash
# 在 RmikuOS shell 里
/ $ ./sched/sexp0_edf > /tmp/sexp0_edf.csv

# 或宿主机重定向
./run.sh riscv64 debug < <(echo "./sched/sexp0_edf") 2>&1 \
  | tee ./logs/sched/edf/sexp0_edf.csv
```

## 统计

```bash
python3 ./scripts/sched/stat_exp0.py ./logs/sched/edf/sexp0_edf.csv
```

## 通过标准

| 指标 | 期望 | 含义 |
|------|------|------|
| ctrl CPU share | ≈ 66% (±5%) | stride 公平分配（300:100:50 = 2/3） |
| ctrl miss rate | > 95% | ai 抢占打碎 burn → deadline miss |
| W/D/S/J 行非空 | ✓ | 框架正常 |
| rep 间 std | < 3% | 可复现 |

## 实测结果（2026-07-28，6 reps）

```
ctrl CPU share = 65.3 ± 0.3%
ai   CPU share = 23.1 ± 0.3%
log  CPU share = 11.6 ± 0.0%
ctrl miss rate = 100.00 ± 0.00%
Jain index     = 0.7392 ± 0.0119
```

| 指标 | 实测 | 预期 | 结论 |
|------|------|------|------|
| ctrl share | 65.3 ± 0.3% | ~66% | ✅ stride 公平 |
| ctrl miss | 100.0 ± 0.0% | >95% | ✅ ai 抢占致命 |
| 可复现性 | std < 1% | <3% | ✅ 稳定 |

[exp0_miss_rate](exp0_miss_rate.png)

*逐窗口 ctrl miss rate，6 reps 叠加，全程 100%。*

[exp0_cpu_share](exp0_cpu_share.png)

*逐窗口 CPU share，ctrl 稳定 ~65%，ai ~23%，log ~12%。*

[exp0_summary](exp0_summary.png)

*汇总柱状图：ctrl miss 100% + CPU share 65:23:12。*

## 结论

**PASS。**

即使 stride 公平分配 CPU（ctrl 拿 2/3），ai 的 14 线程抢占导致 ctrl 的 burn(2 tick) 被打碎成 1 tick 碎片，实际完成时间 >> deadline(4 tick)，miss rate 100%。

**这说明纯 stride 公平不足以保护 deadline 任务**——需要 α 旋钮降低多线程进程的 effective_tickets，让 ctrl 获得更多连续 CPU 时间。自适应 α 的必要性在此基线上成立。

## 产出文件

```
logs/sched/edf/
├── sexp0_edf.csv          # 原始输出（6 reps）
├── exp0_miss_rate.png     # 逐窗口 ctrl miss rate
├── exp0_cpu_share.png     # 逐窗口 CPU share
└── exp0_summary.png       # 汇总柱状图
```

## 复现

```bash
# 编译（build.py 自动处理 user/sched/*.c）
./run.sh riscv64 debug

# 在 shell 里跑
/ $ ./sched/sexp0_edf > /tmp/sexp0_edf.csv

# 宿主机统计
python3 ./scripts/sched/stat_exp0.py ./logs/sched/edf/sexp0_edf.csv
```

## 注意事项

- **shell 干扰已排除**：shell 的 `waitpid(WNOHANG)` 轮询不 sleep，tickets=100 会吃 CPU。在 shell main 开头 `set_my_tickets(1)` 修复。
- **Window 1 跳过**：start_delay(80 tick) 残留导致 Window 1 的 run_delta 偏高（~105 而非 100），stat 脚本自动跳过。
- **ctrl burn(400000) > 单次时间片**：burn 需要 ~2 tick 连续运行，但 timer 中断每 tick 抢占，burn 被打碎是 miss 的直接原因。
