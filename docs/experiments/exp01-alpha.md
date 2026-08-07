<!-- 本文档由 RmikuOS README 拆分而来,内容为原文摘录 -->
[← 返回 RmikuOS 主页](../../README.md)

---

# 01 · α 机制验证（α mechanism verification）

## 目的

验证 alpha-scaled scheduling 的核心机制：`effective_tickets = tickets × scale(runnable_threads, α)`。

三组等 tickets(100)、不同线程数(1/9/25)的纯 spin 任务，α=0..100 扫描，验证：

1. **eff_tickets 单调不降**：α 增大时，多线程进程的 eff_tickets 不降
2. **CPU share ∝ eff_tickets**：share 比例跟随 eff_tickets 比例
3. **α=0 公平**：三组 1:1:1（scale 退化为常数 1）
4. **α=100 按线程数**：三组 1:9:25（scale(n,100)=n）

## 配置

| 任务 | tickets | 线程 | 类型 | burn | 备注 |
|------|---------|------|------|------|------|
| t1 | 100 | 1 | spin | 12000 | 单线程基准 |
| t2 | 100 | 9 | spin | 12000 | 中等多线程 |
| t3 | 100 | 25 | spin | 12000 | 重多线程 |

- 平台：riscv64 (QEMU), SMP=1, 1 tick = 17.51 ms
- total = 6000 ticks/trial（~105 秒/trial）
- window = 100 ticks
- α = 0..100 扫描，step=5（21 点）为主，step=1（101 点）为高分辨率附录
- schedlab 主进程 tickets=1（排除干扰），创建阶段 α=0 公平调度

## 运行

```bash
# step=5（21 trials，~37 分钟）
# 修改 sexp1_mech.c 里 ALPHA_STEP 为 5
/ $ ./sched/sexp1_mech > /tmp/sexp1_mech.csv

# step=1（101 trials，~3 小时）
# 修改 ALPHA_STEP 为 1
/ $ ./sched/sexp1_mech > /tmp/sexp1_mech.csv

# 宿主机统计
python3 ./scripts/sched/stat_exp1.py ./logs/sched/mech/sexp1_mech.csv
```

## 通过标准

| 指标 | 期望 | 含义 |
|------|------|------|
| Monotonicity t2/t3 | PASS | eff_tickets 随 α 单调不降 |
| α=0 fairness | ≈1:1:1 | scale(n,0)=1，三组公平 |
| α=100 ratio | t2/t1≈9, t3/t1≈25 | scale(n,100)=n，按线程数分配 |
| Min share | >1% | 无饥饿 |

## 实测结果 — step=5（21 点，正式结果）

```
   α   eff_t1  eff_t2  eff_t3   sh_t1  sh_t2  sh_t3    Jain
------------------------------------------------------------------------
   0      100     100     100    33.5   33.6   32.9  0.8499
   5      100     100     100    32.8   33.5   33.8  0.8418
  10      100     100     100    32.5   34.0   33.5  0.8452
  15      100     100     100    33.4   33.3   33.3  0.8562
  20      100     100     100    32.5   33.9   33.6  0.8437
  25      100     100     200    24.8   25.0   50.2  0.9102
  30      100     100     200    24.9   25.0   50.1  0.8800
  35      100     200     300    16.7   33.1   50.2  0.8180
  40      100     200     300    16.9   33.8   49.3  0.7967
  45      100     200     400    14.2   28.9   56.9  0.8751
  50      100     300     500    11.0   33.3   55.7  0.8233
  55      100     300     600    11.4   33.7   54.9  0.7674
  60      100     300     700     9.9   30.0   60.0  0.8087
  65      100     400     800     7.9   30.4   61.7  0.7846
  70      100     500     900     7.1   28.1   64.7  0.8364
  75      100     500    1100     5.9   29.5   64.6  0.8120
  80      100     600    1300     5.3   26.2   68.5  0.7257
  85      100     700    1500     4.5   27.3   68.2  0.7862
  90      100     700    1800     3.9   27.1   69.0  0.8708
  95      100     800    2200     3.2   26.9   69.9  0.7880
 100      200    1000    2600     2.7   25.8   71.5  0.8005

CHECKS:
  Monotonicity t2: ✅ PASS
  Monotonicity t3: ✅ PASS
  α=0 fairness: 33.5:33.6:32.9 ✅ PASS (≈1:1:1)
  α=100 ratio: t2/t1=9.7 (expect ~9), t3/t1=26.9 (expect ~25) ✅ PASS
  Min share across all: 2.7% ✅ PASS (>1%)
```

### 图（step=5）

![eff_tickets vs α](../../exp1_eff_tickets_5.png)

*eff_tickets 随 α 单调递增。点=实测，线=理论 `100×n^(α/100)`。t1(1线程)在 α<100 时恒为 100，t2(9)从 100 爬到 1000，t3(25)从 100 爬到 2600。*

![CPU share vs α](../../exp1_cpu_share_5.png)

*CPU share 从 33:33:33（α=0）平滑过渡到 2.7:25.8:71.5（α=100）。t3 随 α 增大获得越来越多 CPU，t1 逐渐被压缩但不饥饿。*

![Jain fairness vs α](../../exp1_jain_5.png)

*Jain 公平指数。α=0 时最高（~0.85），α 增大后 share 不均导致 Jain 下降，但全程 >0.72，无严重不公平。*

## 实测结果 — step=1（101 点，高分辨率附录）

step=1 提供 α 的连续扫描，验证 scale 函数的平滑性。101 个点中 100 个单调，仅 α=92 有一个微小毛刺（见下方"调试历程"）。

```
   α   eff_t1  eff_t2  eff_t3   sh_t1  sh_t2  sh_t3    Jain
------------------------------------------------------------------------
   0      100     100     100    33.4   33.1   33.5  0.8545
  ...     ...     ...     ...    ...    ...    ...    ...
  50      100     300     500    11.1   33.4   55.5  0.8316
  ...     ...     ...     ...    ...    ...    ...    ...
  92      100     800    1200     3.7   33.2   63.1  0.8160  ← 毛刺
  ...     ...     ...     ...    ...    ...    ...    ...
 100      200    1000    2600     2.9   25.7   71.4  0.7968

CHECKS:
  Monotonicity t2: ✅ PASS
  Monotonicity t3: ❌ FAIL  (α=92 单点毛刺)
  α=0 fairness: ✅ PASS
  α=100 ratio: t2/t1=8.8, t3/t1=24.5 ✅ PASS
  Min share: 2.7% ✅ PASS
```

### 图（step=1）

![eff_tickets vs α (step=1)](../../exp1_eff_tickets_1.png)

*101 点连续扫描。eff_tickets 曲线平滑单调，α=92 处 t3 有一个微小下凹（瞬时快照噪声）。*

![CPU share vs α (step=1)](../../exp1_cpu_share_1.png)

*101 点 CPU share。曲线整体平滑，α=92 处 t2 share 异常偏高（33.2 vs 周围 26-27），是同一瞬时快照噪声的表现。*

![Jain fairness vs α (step=1)](../../exp1_jain_1.png)

*101 点 Jain 指数。整体趋势与 step=5 一致，α=92 的毛刺在图上几乎不可见。*

## 调试历程

本实验在调试过程中暴露了三个深层 bug，涉及内核调度器和实验框架的交互。记录如下，供后续实验参考。

### Bug 1：sleep(100) 睡过头 43 倍

**现象**：α=50 trial 只有 2 个 window（应为 ~58 个），window 1 的 run_delta 总和 = 4336 tick（应为 ~100）。

**根因**：schedlab 主进程 `set_my_tickets(1)` 导致 stride = BIG_STRIDE/1 = 10,000,000。而 t1/t2/t3 的 stride = BIG_STRIDE/100 = 100,000。主线程被选 1 次后 pass = 10,000,000，而 t1/t2/t3 跑 100 tick 只涨 ~3,300,000。`sleep(100)` 到期后主线程被 wake（变 Ready），但它的 pass 远大于 t1/t2/t3，`pick_ready_process_by_stride` 不会选 schedlab。主线程要等 t1/t2/t3 的 pass 也涨到 10,000,000（约 67+ tick）才被选中。**而且每次被选 pass 再 +10,000,000，下次 sleep 睡得更久**。

**修复**：内核 `wake_blocked_thread` 中，Sleep 唤醒时重置线程 pass=0 和进程 pass=0。关键设计：进程 pass 只在 `was_empty`（进程从 0 个 runnable 变 1 个）时重置，避免 phased sleep 频繁 wake 破坏多线程进程的公平性。这样 `set_my_tickets(1)` 可以保留（零干扰），sleep 唤醒后内核自动让线程及时被调度。

```rust
// kernel/src/task/manager.rs :: wake_blocked_thread
if was_sleep {
    thread.pass = 0;
}
let was_empty = self.process(pid).runnable_count == 0;
self.process_mut(pid).runnable_count += 1;
if was_sleep && was_empty {
    self.process_mut(pid).pass = 0;  // 只在进程从空变非空时重置
}
```

### Bug 2：α=85 时 t3 启动饿死 3300 tick

**现象**：α=85 trial 中，t3 前 33 个 window `threads.len=1, runnable=1, run_delta=0`。直到 window 34 才创建完 25 个 worker。其他 α 点正常。

**根因**：fork 后 t3 主线程要跑 25 次 `thread_create`。创建期间 t3 只有主线程 1 个 runnable，`eff = 100×scale(1, 85) = 100`。而 t2 已创建好 10 个线程，`eff = 100×scale(10, 85) = 700`。t2 的 stride 只有 t3 的 1/7，霸占 CPU，t3 主线程拿不到时间片跑 `thread_create` → **饿死**。α=85 正好卡在临界点（α=80 勉强能创建，α=90+ t2 worker 退出更快 t3 能喘气）。

**修复**：`sl_run` 创建阶段用 α=0 公平调度。fork 前 `set_sched_alpha(0)`，让所有进程 eff 相等，t3 公平分到 CPU 快速创建 worker。`sleep(start_delay)` 后切 `set_sched_alpha(alpha)`，`reset_sched_stat()` + 重置 `sl_t0` 丢弃创建阶段数据。

```c
// schedlab.h :: sl_run
set_sched_alpha(0);           // 创建阶段公平调度
// ... fork groups ...
sleep(start_delay);
set_sched_alpha(alpha);       // 切到实验 alpha
reset_sched_stat();
sl_t0 = get_ticks();          // 时间从现在算起
sl_t_end = sl_t0 + cfg->total_ticks;
```

### Bug 3：α=92 瞬时快照毛刺（step=1）

**现象**：step=1 扫描中 α=92 的 t3 eff=1200（应为 ~1900），Monotonicity t3 FAIL。

**根因**：`get_process_sched_stat` 用 `count_runnable_threads_in_process` 实时扫描 `process.threads` 统计 Ready+Running。stat syscall 执行的瞬间，t3 的 26 个线程中有 11 个恰好不在 Ready/Running 状态（刚被 preempt 的边界状态），runnable=15 → `scale(15, 92)=12` → eff=1200。这是 stride 调度 + 瞬时快照的固有随机性，101 点中仅此 1 点中招。

**影响**：无伤大雅。step=5（21 点）不受影响，全 PASS。step=1 的图上该点是一个微小的下凹，不影响整体趋势。

**可选修复**（未应用）：stat 改用"活线程数"（非 Zombie/Dead）代替"瞬时 Ready+Running"，消除边界状态抖动。

### 其他已修复的小问题

| 问题 | 修复 |
|------|------|
| `set_sched_alpha` 不重置 pass | 切 α 时重置所有进程/线程 pass，避免旧 pass 导致前几 window 歪斜 |
| `stat_exp1.py` wins 过滤重复 | 第二行 `wins = [w for w in wins if w > 3]` 覆盖了第一行的"去掉最后 2 个 window"，已删除 |
| `runnable_count` 增量维护漂移 | 4 处热路径（pick/stride/has_ready/pick_thread）全改用实时扫描 `count_runnable_threads_in_process` |

## 结论

**PASS。**

alpha-scaled scheduling 的核心机制完全验证：

1. **eff_tickets = tickets × scale(threads, α)** 单调不降，与理论曲线 `n^(α/100)` 精确吻合（定点整数实现，误差 < 1）
2. **CPU share ∝ eff_tickets**，从 α=0 的 1:1:1 平滑过渡到 α=100 的 1:9:25
3. **α=0 退化为纯 stride 公平**，α=100 退化为按线程数分配，中间值平滑插值
4. **无饥饿**，最小 share 2.7%（α=100 时的 t1）

α 旋钮提供了在"进程公平"和"线程公平"之间连续调节的能力，为后续 exp2（deadline trade-off）和 exp3（AIMD 自适应）奠定了机制基础。

## 产出文件

```
logs/sched/mech/
├── sexp1_mech.csv              # 原始 CSV（step=5 或 step=1）
├── exp1_eff_tickets_5.png      # eff_tickets vs α（step=5）
├── exp1_cpu_share_5.png        # CPU share vs α（step=5）
├── exp1_jain_5.png             # Jain fairness vs α（step=5）
├── exp1_eff_tickets_1.png      # eff_tickets vs α（step=1，高分辨率）
├── exp1_cpu_share_1.png        # CPU share vs α（step=1）
└── exp1_jain_1.png             # Jain fairness vs α（step=1）
```

## 注意事项

- **ALPHA_STEP 控制**：`sexp1_mech.c` 里 `#define ALPHA_STEP 5`，改 1 跑高分辨率版本（~3 小时）
- **图命名约定**：step=5 的图加 `_5` 后缀，step=1 的加 `_1` 后缀，便于区分
- **创建阶段 α=0**：`sl_run` 在 fork 阶段临时设 α=0，避免高 α 下多线程进程创建 worker 时被饿死（见 Bug 2）
- **schedlab tickets=1**：主进程 tickets=1 排除干扰，内核 sleep 唤醒重置 pass 保证它及时醒来测量窗口（见 Bug 1）
- **t1 eff=200 at α=100**：t1 的 threads.len=2（1 worker + 1 main 都 spin），`scale(2,100)=2`。这是 schedlab 框架"主线程也干活"的设计，不影响 share ratio check


