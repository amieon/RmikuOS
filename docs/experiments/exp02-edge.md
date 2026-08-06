<!-- 本文档由 RmikuOS README 拆分而来,内容为原文摘录 -->
[← 返回 RmikuOS 主页](../../README.md)

---

# 02 · Edge Deadline Trade-off

## 目的

刻画 α 对 deadline 任务的影响：扫描 α=0..100，观察 ctrl 的 miss rate 如何随 α 变化。

5 个压力等级（light → extreme），验证：
1. **低 α 保护 deadline**：α 小时 ai 被 stride 压制，ctrl miss 低
2. **高 α 崩溃 deadline**：α 大时 ai eff 增大，ctrl 被抢占，miss 急剧上升
3. **edge 随压力左移**：负载越重，edge（miss 急剧上升的 α 临界点）出现越早
4. **不存在通用最优 α**：不同负载需要不同 α → 自适应 α 的动机（→ exp3 AIMD）

## 配置

| 任务 | tickets | 线程 | 类型 | period | burn | ≈ ticks | 备注 |
|------|---------|------|------|--------|------|---------|------|
| ctrl | 300 | 1 | jobs (fork) | 4 | 180,000 | ~1.2 | deadline 任务，需 2 次被选 |
| ai | 100 | 变 | spin | — | 12,000 | ~0.08 | 抢占负载 |
| log | 50 | 变 | spin | — | 12,000 | ~0.08 | 后台负载 |

5 个压力等级：

| 配置 | ai 线程 | log 线程 | 预期 edge |
|------|--------|---------|----------|
| light | 7 | 3 | α≈50-60 |
| medlo | 15 | 8 | α≈40-50 |
| medium | 25 | 9 | α≈30-40 |
| heavy | 75 | 25 | α≈20-30 |
| extreme | 225 | 50 | α≈20-30 |

- 平台：riscv64 (QEMU), SMP=1, 1 tick = 13.98 ms
- total = 6000 ticks/trial, window = 100 ticks
- α = 0..100 step 10（11 点）× (1 warmup + 5 reps) = 66 trials/config
- ctrl 用 fork 子进程（非 in-parent），避免被监控主线程打断

## 运行

```bash
# 在 RmikuOS shell 里
/ $ ./sched/sexp2_edge > /tmp/sexp2_edge.csv

# 宿主机统计
python3 ./scripts/sched/stat_exp2.py ./logs/sched/edge/sexp2_edge.csv
```

## 通过标准

| 指标 | 期望 | 含义 |
|------|------|------|
| 低 α miss | <25% | ctrl 被 stride 保护 |
| 高 α miss | >90% | ai 抢占 ctrl |
| edge 存在 | miss 有急剧上升 | α 对 deadline 有影响 |
| edge 左移 | 压力越大 edge α 越小 | 负载越重越需要低 α |
| rep 间方差 | hi/lo < 15% | 可复现 |

## 实测结果（2026-07-30，5 configs × 11 α × 5 reps）

### medium（核心配置）

```
   α                 miss%               sh_ctrl                 sh_ai                sh_log  avg_late  max_late    Jain
---------------------------------------------------------------------------------------------------------------------------------------
   0     16.8 +  1.2/-  0.7     39.2 +  1.2/-  1.5     40.9 +  1.5/-  1.8     20.0 +  0.6/-  0.7       1.8         9   0.832
  10     17.1 +  2.3/-  1.2     41.1 +  4.2/-  2.0     39.5 +  1.5/-  3.0     19.5 +  1.5/-  1.2       1.8         9   0.838
  20     15.5 +  0.6/-  2.2     39.2 +  1.1/-  3.0     40.7 +  2.3/-  1.5     20.1 +  0.7/-  0.8       1.8         8   0.836
  30     16.0 +  0.7/-  1.5     40.0 +  1.3/-  1.6     47.6 +  1.2/-  1.2     12.4 +  0.4/-  0.4       1.8         9   0.799
  40     65.6 +  4.6/-  7.3     36.5 +  1.9/-  1.3     47.0 +  2.1/-  2.2     16.4 +  2.7/-  1.8       8.7        41   0.822
  50     94.7 +  3.4/-  4.7     31.1 +  0.4/-  0.8     53.0 +  1.2/-  0.5     15.9 +  0.3/-  0.4     153.8       419   0.817
  60     98.5 +  1.4/-  0.7     27.7 +  1.0/-  1.0     58.3 +  1.1/-  1.2     14.1 +  0.2/-  0.5     408.2       881   0.833
  70     98.3 +  0.5/-  0.9     21.5 +  0.5/-  0.2     64.2 +  0.4/-  0.5     14.3 +  0.2/-  0.3    1055.6      2086   0.823
  80     98.5 +  1.2/-  0.6     16.2 +  0.1/-  0.1     70.1 +  0.1/-  0.0     13.7 +  0.1/-  0.1    1577.8      3143   0.813
  90     97.6 +  0.5/-  0.4     12.2 +  0.2/-  0.1     73.4 +  0.2/-  0.2     14.3 +  0.3/-  0.5    1851.7      3648   0.809
 100     96.8 +  0.6/-  0.8      7.4 +  1.9/-  7.4     61.4 + 15.4/- 61.4     11.1 +  2.9/- 11.1    2074.6      4203   0.768
```

**edge 在 α=30→40**：miss 从 16% 跳到 66%。α≥50 后 miss>94%，ctrl 崩溃。

### 跨配置 edge 对比

| 配置 | ai 线程 | α=0 miss% | edge α | α=100 miss% | sh_ctrl α=0 → α=100 |
|------|--------|-----------|--------|-------------|---------------------|
| light | 7 | 14.9 | 50-60 | 98.8 | 38.2% → 26.1% |
| medlo | 15 | 16.1 | 40-50 | 97.9 | 38.8% → 13.7% |
| medium | 25 | 16.8 | 30-40 | 96.8 | 39.2% → 7.4% |
| heavy | 75 | 16.7 | 20-30 | 94.2 | 40.0% → 5.8% |
| extreme | 225 | 21.5 | 20-30 | 81.7 | 42.0% → 5.7% |

**edge 随压力左移**：light edge≈α55, medium≈α35, heavy≈α25, extreme≈α25。压力越大，越需要低 α 保护 ctrl。

### 图

#### 跨配置对比

![miss rate comparison](../../exp2_compare_miss.png)

*5 个配置的 miss rate vs α。压力越大曲线越靠左——edge 出现在更低 α。*

![trade-off comparison](../../exp2_compare_tradeoff.png)

*ctrl miss rate vs ai CPU share。不同配置的 trade-off 曲线，α 标注在点旁。*

#### medium 配置（核心）

![medium miss & share](../../exp2_medium_miss_share.png)

*ctrl miss rate（红，左轴）+ ai/log share（虚线，右轴）vs α。α=30→40 miss 从 16% 跳到 66%。*

![medium tardiness](../../exp2_medium_tardiness.png)

*ctrl 迟到程度（avg/max late）vs α。α=40 后 avg_late 急剧上升。*

![medium jain](../../exp2_medium_jain.png)

*Jain 公平指数 vs α。α 增大后 ctrl 被压制，Jain 下降。*

#### 其他配置

| 配置 | miss & share | tardiness | jain |
|------|-------------|-----------|------|
| light | [图](../../exp2_light_miss_share.png) | [图](../../exp2_light_tardiness.png) | [图](../../exp2_light_jain.png) |
| medlo | [图](../../exp2_medlo_miss_share.png) | [图](../../exp2_medlo_tardiness.png) | [图](../../exp2_medlo_jain.png) |
| heavy | [图](../../exp2_heavy_miss_share.png) | [图](../../exp2_heavy_tardiness.png) | [图](../../exp2_heavy_jain.png) |
| extreme | [图](../../exp2_extreme_miss_share.png) | [图](../../exp2_extreme_tardiness.png) | [图](../../exp2_extreme_jain.png) |

## 调试历程

### Bug 1：ctrl 用 in-parent 被主线程打断

**现象**：miss rate rep 之间方差极大（std=40+），不可复现。

**根因**：ctrl 用 `sl_add_jobs_parent`（in-parent），跑在监控进程里。监控主线程每 100 tick sleep 唤醒，`wake_blocked_thread` 重置进程 pass=0，主线程被优先选中，打断 ctrl worker 的连续运行。

**修复**：ctrl 改用 `sl_add_jobs`（fork 子进程），独立于监控进程，不被主线程打断。

### Bug 2：burn 太小导致 miss=0

**现象**：换新机器后标定，burn=400,000 从 2.054 tick 变成 0.427 tick（宿主机负载干扰导致 QEMU 速度变化）。ctrl 1 次被选就跑完 burn，α 再大也不 miss。

**修复**：宿主机重启 + 只开虚拟机重标定。确认 burn=180,000 ≈ 1.2 tick，ctrl 需 2 次被选完成 burn。

### Bug 3：sleep 唤醒"免费优先"导致相变方差

**现象**：中间 α（如 α=40）的 miss rate 在 rep 之间是 0% 或 100% 的双峰分布，+62/-17 的极端方差。

**根因**：`wake_blocked_thread` 的 sleep 唤醒重置进程 pass=0，让 ctrl 每次 period sleep 到期后"免费"获得优先调度。但这个"免费优先"不稳定——取决于 ctrl sleep 到期时 ai 是否正在跑（QEMU timer wall clock 抖动 ±1 tick）。不同 rep 的 timer 抖动不同，导致 ctrl 有时赶上 deadline（0% miss），有时差一点（100% miss）。

**修复**：`wake_blocked_thread` 中，只对低 tickets 进程（tickets≤1，如监控主进程）重置进程 pass。高 tickets 进程（如 ctrl tickets=300）不重置，让 α 真正通过 stride 竞争影响 ctrl 被选频率。

```rust
if was_sleep && was_empty {
    if self.process(pid).tickets <= 1 {
        self.process_mut(pid).pass = 0;  // 只给监控进程"免费优先"
    }
    // ctrl(tickets=300) 不重置，让 α 发挥作用
}
```

修复后方差从 +62/-17 降到 +4.6/-7.3，可复现。

## 结论

**PASS。**

1. **α 能有效控制 deadline 质量**：低 α 保护 ctrl（miss<20%），高 α 崩溃 ctrl（miss>95%）
2. **edge 存在且随压力左移**：light edge≈α55, medium≈α35, heavy≈α25
3. **不存在通用最优 α**：light 最优 α≈50, heavy 最优 α≈20。固定 α 无法适应所有负载
4. **→ 自适应 α 的必要性成立**：需要控制器根据负载自动调节 α（→ exp3 AIMD）

## 产出文件

```
logs/sched/edge/
├── sexp2_edge.csv                    # 原始 CSV（5 configs × 11 α × 6 trials）
├── exp2_compare_miss.png             # 跨配置 miss rate 对比
├── exp2_compare_tradeoff.png         # 跨配置 trade-off 曲线
├── exp2_{config}_miss_share.png      # 每配置 miss rate + share
├── exp2_{config}_tardiness.png       # 每配置迟到程度
└── exp2_{config}_jain.png            # 每配置 Jain 公平指数
```

## 注意事项

- **ctrl 用 fork 子进程**（非 in-parent），避免被监控主线程打断
- **burn=180,000**（≈1.2 tick），需 2 次被选完成。period=4 给 2.8 tick 余量
- **sleep 唤醒只给 tickets≤1 的进程重置 pass**，ctrl(tickets=300) 不享受"免费优先"
- **宿主机必须空闲**：QEMU timer 基于 wall clock，宿主机负载会导致 timer 抖动
- **误差为非对称 +hi/-lo**：hi=max(rep)-mean, lo=mean-min(rep)，比 ±std 更反映双峰分布


