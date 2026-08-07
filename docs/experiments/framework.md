<!-- 本文档由 RmikuOS README 拆分而来,内容为原文摘录 -->
[← 返回 RmikuOS 主页](../../README.md)

---

## Scheduler Experiments

# RmikuOS Scheduling Lab — 标定

## 平台

| 平台 | 角色 | 1 tick | R² | burn dev | drift | 备注 |
|------|------|--------|-----|---------|-------|------|
| riscv64 (QEMU, SMP=1) | **主力** | 13.98 ms | 1.0000 | 6% | 0% | ✅ 全部通过 |
| loongarch64 (QEMU) | 验证 | 125.59 ms | 1.0000 | 6% | 0% | ✅ 全部通过 |

> **标定环境**：宿主机重启后，除虚拟机外无其他程序运行。
> 此前在宿主机高负载下标定的数据（1 tick=66.44ms, R²=0.9943, burn dev=32%）
> 系 QEMU 被宿主机调度抢占所致，**已作废**。

---

## Phase 1: Tick ↔ Time 线性性

### riscv64 (SMP=1)

| span (ticks) | mean (us) | std (us) | std/mean | rate (us/tick) |
|-------------|-----------|----------|----------|----------------|
| 200 | 2,976,101 | 42,125 | 1.4% | 14,881 |
| 500 | 7,436,144 | 106,975 | 1.4% | 14,872 |
| 1000 | 14,673,191 | 237,950 | 1.6% | 14,673 |
| 2000 | 28,454,304 | 356,188 | 1.3% | 14,227 |
| 4000 | 56,283,658 | 616,328 | 1.1% | 14,071 |

**线性回归：**
- slope = **13,984.5 us/tick**（即 1 tick ≈ 13.98 ms）
- intercept = 428,568 us
- R² = **1.0000**

**✅ VERDICT: LINEAR（R² ≥ 0.999）**

std/mean 全部 ≤ 1.6%，非常稳定。span 越大 std 越大但比例不变，是正常的统计涨落。

![riscv64 linearity](../../logs/sched/calibrate/calib_linearity_riscv64.png)

### loongarch64

| span (ticks) | mean (us) | std (us) | std/mean | rate (us/tick) |
|-------------|-----------|----------|----------|----------------|
| 200 | 24,985,887 | 445,866 | 1.8% | 124,929 |
| 500 | 62,483,373 | 402,709 | 0.6% | 124,967 |
| 1000 | 124,614,713 | 1,029,461 | 0.8% | 124,615 |
| 2000 | 251,646,112 | 3,156,270 | 1.3% | 125,823 |
| 4000 | 501,839,934 | 5,522,841 | 1.1% | 125,460 |

**线性回归：**
- slope = **125,586.1 us/tick**（即 1 tick ≈ 125.59 ms）
- intercept = −288,526 us
- R² = **1.0000**

**✅ VERDICT: LINEAR**

![loongarch64 linearity](../../logs/sched/calibrate/calib_linearity_loongarch.png)

---

## Phase 2: Burn 负载（sl_burn）

### riscv64 (SMP=1)

| iters | ticks (mean ± std) | std/mean | 备注 |
|-------|-------------------|----------|------|
| 50,000 | 0.444 ± 0.079 | 18% | 量化噪声区 |
| 100,000 | 0.778 ± 0.127 | 16% | 量化噪声区 |
| 200,000 | 1.345 ± 0.086 | 6% | |
| **400,000** | **2.722 ± 0.252** | 9% | **锚点** |
| 800,000 | 6.090 ± 0.326 | 5% | |
| 1,600,000 | 11.552 ± 0.710 | 6% | |

- **iters_per_tick ≈ 146,950**（由锚点 400000 / 2.722 得出）
- burn 线性度：burn(1.6M)/burn(400k) = 11.552/2.722 = 4.24，iters 比为 4.0
- **deviation = 6% ≤ 10% → ✅ LINEAR**
- 小 iters（< 200k）std 相对大是 tick 整数量化伪影（0/1 舍入）

![riscv64 burn](../../logs/sched/calibrate/calib_burn_riscv64.png)

### loongarch64

| iters | ticks (mean ± std) | std/mean | 备注 |
|-------|-------------------|----------|------|
| 50,000 | 0.482 ± 0.088 | 18% | 量化噪声区 |
| 100,000 | 0.983 ± 0.147 | 15% | 量化噪声区 |
| 200,000 | 1.886 ± 0.238 | 13% | |
| **400,000** | **3.535 ± 0.368** | 10% | **锚点** |
| 800,000 | 7.029 ± 1.118 | 16% | |
| 1,600,000 | 15.020 ± 1.602 | 11% | |

- **iters_per_tick ≈ 113,154**
- burn 线性度：deviation = 6% ≤ 10% → ✅ LINEAR

![loongarch64 burn](../../logs/sched/calibrate/calib_burn_loongarch.png)

---

## Phase 3: 漂移

| 平台 | pre (us/tick ×1000) | post (us/tick ×1000) | drift |
|------|---------------------|----------------------|-------|
| riscv64 (SMP=1) | 14,562,515 | 14,649,211 | **0%** |
| loongarch64 | 120,795,020 | 120,778,716 | **0%** |

**✅ 两平台 drift = 0%**，正式实验前标定完全有效。

![drift summary](../../logs/sched/calibrate/calib_drift.png)

---

## 推荐 burn 值

### riscv64（iters_per_tick ≈ 147,000，线性 dev=6%）

| 目标 ticks | burn(iters) | 实测验证 | 备注 |
|-----------|-------------|---------|------|
| 1 | 147,000 | — | 线性外推 |
| 2 | 294,000 | — | 线性外推 |
| 3 | 441,000 | — | 线性外推 |
| **~2.7** | **400,000** | **2.722** | **锚点** |
| 4 | 588,000 | — | 线性外推 |
| 5 | 735,000 | — | 线性外推 |
| ~6.1 | 800,000 | 6.090 | 实测 |
| 8 | 1,176,000 | — | 线性外推 |
| ~11.6 | 1,600,000 | 11.552 | 实测 |

### loongarch64（iters_per_tick ≈ 113,000，线性 dev=6%）

| 目标 ticks | burn(iters) | 实测验证 | 备注 |
|-----------|-------------|---------|------|
| 1 | 113,000 | — | 线性外推 |
| 2 | 226,000 | — | 线性外推 |
| 3 | 339,000 | — | 线性外推 |
| **~3.5** | **400,000** | **3.535** | **锚点** |
| 4 | 453,000 | — | 线性外推 |
| ~7.0 | 800,000 | 7.029 | 实测 |
| ~15.0 | 1,600,000 | 15.020 | 实测 |

---

## 实验任务配置

### exp0 / exp2 配置（riscv64 新机器）

| 任务 | period | burn(iters) | ≈ ticks | 设计意图 |
|------|--------|-------------|---------|----------|
| ctrl | 4 | 400,000 | ~2.7 | deadline 任务，需 3 次被选完成 |
| ai | — | 12,000 | ~0.08 | spin 负载（轻量） |
| log | — | 12,000 | ~0.08 | spin 负载（轻量） |

**ctrl period=4, burn≈2.7 tick**：
- ctrl 需要在 4 tick 内被选 ~3 次完成 burn
- α=0 时 ctrl stride 小，快速被选 3 次，finish ≈ release+3 < deadline=4 → 不 miss
- α=100 时 ctrl 被选间隔 ~8 tick，finish ≈ release+16 > deadline=4 → miss
- 中间 α 产生平滑 trade-off

### exp1 配置（不变）

exp1 的 spin burn=12,000（~0.08 tick）不依赖绝对时间，只看 share 比例。旧机器数据仍然有效，无需重跑。

---

## 注意事项

1. **标定时宿主机必须空闲**：重启后除虚拟机外不开其他程序。此前高负载标定（R²=0.9943, burn dev=32%）系 QEMU 被宿主机调度抢占所致。
2. **两平台 burn 均线性**（dev=6% ≤ 10%），可用 iters_per_tick 线性外推。
3. **所有时间指标用 tick**，报告里换算时注明 **1 tick = 13.98 ms**（riscv64, SMP=1）。
4. **旧标定数据（1 tick = 17.51 ms 或 66.44 ms）已作废**，不可引用。
5. **跨机器必须重标定**：不同宿主机/CPU 频率/QEMU 版本会导致 tick 速率变化。

---

## 产出文件

```
logs/sched/calibrate/
├── calib_linearity_riscv64.png    # riscv64 tick 线性性（R²=1.0）
├── calib_linearity_loongarch.png  # loongarch tick 线性性（R²=1.0）
├── calib_burn_riscv64.png         # riscv64 burn 标定（dev=6%）
├── calib_burn_loongarch.png       # loongarch burn 标定（dev=6%）
├── calib_drift.png                # 漂移对比（两平台 0%）
└── calib_summary.png              # 汇总
```



# schedlab.h —— RmikuOS 调度实验框架

> 单一头文件承载全部调度实验的负载生成、窗口监控、策略回调与 CSV 输出。
> 6 个实验（mech/edge/aimd/dyn/phase/adamw）共用同一套基础设施。

## 设计动机

调度实验需要回答"自适应 α 控制器到底有没有用"。但要公平地对比不同控制器（fixed / AIMD / AdamW），必须保证：

1. **负载可复现**：三组任务（ctrl deadline / ai spin / log spin）在不同实验间完全一致
2. **反馈可读**：控制器需要每窗口拿到 ctrl 的 deadline 反馈（miss/late）
3. **策略可插拔**：换控制器只需改一个回调函数指针
4. **数据可分析**：所有原始量以 CSV 输出，统计交给宿主机 Python

## v1 → v2 架构演变

```
v1（废弃）: 所有负载组都 fork 成独立子进程
  ✗ 控制器在运行中拿不到 deadline 反馈
  ✗ J 行（jobs 汇总）是子进程退出才打的，运行中不可见

v2（当前）: ctrl 搬进监控进程（in-parent jobs 组）
  ✓ ctrl 线程直接跑在监控进程里
  ✓ 统计走进程内共享计数器（AMO，__sync_fetch_and_add）
  ✓ 控制器零 syscall 读取 D 行差分反馈
  ✓ 与原 40_dynamic_load_exp.c 的结构一致
```

## 数据结构

### sl_group_t —— 负载组描述

```c
typedef struct {
    char     name[SL_NAME_LEN];    // "ctrl" / "ai" / "log"
    int      tickets;              // 进程 tickets（stride 基础权重）
    int      threads;              // 线程数
    sl_kind_t kind;                // SL_SPIN（持续 burn）或 SL_JOBS（周期 deadline）
    int      flags;                // SL_F_IN_PARENT / SL_F_PHASED
    int      light_active;         // SL_F_PHASED：轻相位活跃线程数
    int      period_ticks;         // SL_JOBS：释放周期
    int      job_cpu_ticks;        // 记账用
    unsigned long burn;            // burn 迭代数
    int      pid;                  // 运行时填充；in-parent 组 = getpid()
} sl_group_t;
```

### sl_gstats_t —— 每组统计（进程内共享，AMO 更新）

```c
typedef struct {
    unsigned long work;        // spin: burn 迭代总数（吞吐量，K 行）
    unsigned long jobs, miss, late_sum, late_max;       // jobs 组
    unsigned long resp_sum, resp_sumsq, resp_min, resp_max;  // 响应时间
} sl_gstats_t;
```

### sl_window_t —— 单窗口测量快照

```c
typedef struct {
    int window_no;
    int alpha;
    int remain_windows;        // late-probe 保护用
    int nprocs;
    sl_proc_t procs[SL_MAX_GROUPS];
    int jain_q;
    int max_slowdown_q;
    /* 窗口 deadline 差分（仅 in-parent jobs 组；否则为 0） */
    unsigned long jobs_delta, miss_delta, late_delta;
} sl_window_t;
```

### sl_cfg —— 单次运行配置

```c
typedef struct {
    unsigned long total_ticks;    // 实验总时长
    int           window_ticks;   // 窗口长度（默认 100）
    int           alpha0;         // 初始 α
    unsigned long start_delay;    // 0 = 用默认 80
    sl_policy_t   policy;         // NULL = 固定 alpha0
    void         *policy_ud;      // 策略私有状态（aimd/adamw 参数）
} sl_cfg;
```

## 全局状态（全部零初始化，.bss！）

```c
static sl_group_t  sl_groups[SL_MAX_GROUPS];
static sl_gstats_t sl_gstats[SL_MAX_GROUPS];
static int         sl_ngroups;
static unsigned long sl_t0, sl_t_end;
static int         sl_window;
static int         sl_l_ratio_permil;   // 相位比例（exp5 用，0=等分）
```

## 负载注册 API

| 函数 | 用途 |
|------|------|
| `sl_add_spin(name, tk, threads, burn)` | 全程满载 spin 组（子进程） |
| `sl_add_spin_phased(name, tk, threads, burn, light_active)` | 四段相位 spin 组 |
| `sl_add_jobs(name, tk, threads, period, cpu, burn)` | 周期 deadline jobs 组  独立子进程） |
| `sl_add_jobs_parent(name, tk, threads, period, cpu, burn)` | 周期 deadline jobs 组（跑在监控进程内） |

**为什么 ctrl 用 in-parent？** 控制器需要每窗口读 ctrl 的 miss/late。in-parent 组通过进程内共享计数器（`__sync_fetch_and_add`）统计，控制器零 syscall 读取。

**为什么 ai/log 用 fork？** 避免监控主线程的 sleep/wake 打断它们的连续运行，保证负载纯净。

## 负载执行

### sl_burn —— CPU 密集计算

```c
static void sl_burn(unsigned long iters) {
    volatile unsigned long x = 1;
    for (unsigned long i = 0; i < iters; i++) x = x * 1664525UL + 1013904223UL;
    (void)x;
}
```

线性同余乘法序列。标定：riscv64 上 `iters_per_tick ≈ 147,000`（burn=400,000 ≈ 2.7 tick，线性 dev=6%）。

### sl_spin_fn —— spin 线程主循环

```c
while (get_ticks() < sl_t_end) {
    long zzz = sl_phased_sleep(g, idx);   // 相位判断，轻相位休眠
    if (zzz > 0) { sleep((usize)zzz); continue; }
    sl_burn(g->burn);
    __sync_fetch_and_add(&sl_gstats[gi].work, 1);  // 吞吐计数
}
```

### sl_job_fn —— deadline 线程主循环

```c
unsigned long release = get_ticks();
while (get_ticks() < sl_t_end) {
    sl_burn(g->burn);
    unsigned long finish = get_ticks();
    unsigned long deadline = release + period_ticks;
    unsigned long resp = finish - release;
    // 统计 jobs/miss/late/resp，AMO 更新
    release += period_ticks;
    long ahead = release - get_ticks();
    if (ahead > 0) sleep((usize)ahead);
}
```

**deadline 语义**：release 时刻开始 burn，必须在 `release + period` 前完成，否则 miss。ctrl 的 burn(~1.2 tick) 需要被调度 2 次以上才能完成，period=4-5 给余量。

## 相位机制（四段 L-H-L-H）

### 等分（默认）

```c
static int sl_phase_now(void) {
    unsigned long span = sl_t_end - sl_t0;
    unsigned long seg = span / 4;       // 四等分
    return min(off / seg, 3);
}
```

### 非等分（exp5 新增，sl_l_ratio_permil）

```c
/* 每个 L-H 周期各占 span/2，L段 = half × ratio/1000 */
if (sl_l_ratio_permil > 0) {
    unsigned long half = span / 2;
    unsigned long l_seg = half * sl_l_ratio_permil / 1000;
    if (off < l_seg) return 0;        /* L1 */
    if (off < half) return 1;         /* H1 */
    if (off < half + l_seg) return 2; /* L2 */
    return 3;                          /* H2 */
}
```

- `sl_l_ratio_permil = 800` → 40/10/40/10（L 占 80%）
- `sl_l_ratio_permil = 200` → 10/40/10/40（H 占 80%）
- `= 0`（默认）→ 25/25/25/25 等分，exp4 不受影响

### sl_phased_sleep    — 轻相位休眠

```c
static long sl_phased_sleep(const sl_group_t *g, int idx) {
    if (!(g->flags & SL_F_PHASED)) return 0;       // 非 phased 组不睡
    if (idx >= 0 && idx < g->light_active) return 0; // 保底活跃线程不睡
    int ph = sl_phase_now();
    if (ph == 1 || ph == 3) return 0;               // 重相位全员活跃
    /* 轻相位：睡到下一边界 */
    return boundary - get_ticks();
}
```

**关键设计**：`idx < light_active` 的线程轻相位保底活跃，其余线程休眠。轻负载恰好 = light_active 个线程，不多不少。

## 策略（sl_policy_t）

### AIMD —— 启发式规则

```c
typedef struct {
    int alpha;
    int inc;             // 爬升步长 = 5
    int backoff;         // 退避比例 = 80(%)
    int safe_lateness;   // 迟到 ≤ 此值算安全 = 0
    int danger_lateness; // 迟到 ≥ 此值算危险 = 25
    int safe_windows;    // 连续安全窗口计数
    int cooldown;        // 退避后冷却 = 3
} sl_aimd_t;
```

决策逻辑（每窗口）：
```
cooldown > 0           → cool（冷却中，什么都不做）
late_delta >= danger   → down（退避 ×80%，miss 越重退越多）
late_delta <= safe     → safe_windows++，≥2 且可 probe → up（+inc）
否则                   → gray（灰区，什么都不做）
```

**调参历程**：
- `cooldown=1 → 3`、`safe_windows>=1 → >=2`：减少 set_sched_alpha 调  频率（syscall + scale 缓存重算开销算在 ctrl_run 里，压低 ai_run）
- `inc=5`：决定稳态高度 `α_steady = (p/q)·inc/(1−b) ∝ inc`

### SPSA-AdamW —— 梯度优化

```c
typedef struct {
    long long m, v;      // Adam 一阶/二阶矩（定点）
    long long t;
    int alpha;
    int alpha_f;         // α × 1024（定点）
    int lr;              // 步长 = 3
    int target;          // weight decay 目标 = 25
    int delta;           // SPSA 扰动 = 5
    int prev_probe;
    long long prev_loss;
} sl_adamw_t;
```

```
loss = miss_per_1000 + late_per_job  (封顶 4000)

SPSA 梯度估计:
  g = (loss_后 - loss_prev) × 1024 / (2 × delta × probe方向)
  （交替探测 α±5，用两次 loss 差估梯度）

AdamW 更新:
  m = 0.9m + 0.1g
  v = 0.99v + 0.01g²
  step = lr × 1024 × m / isqrt(v)   （isqrt = 整数 Newton 迭代）
  decay = (target×1024 - α_f) × 2%
  α_f -= step - decay                （梯度下降 + 正则回拉）
```

**与 AIMD 的本质差异**：AIMD 在 late=0 时主动 probe up（贪婪试探），AdamW 在 loss=0 时梯度=0，不知道往哪走，只被 weight decay 拉回 target。**但实测 AdamW 全面碾压 AIMD**——SPSA 扰动是探索机制，α 高频震荡（spike 抢 CPU、dip 让 ctrl 恢复），净效果 burn 更高 + miss 更低。

## 监控与运行（sl_run 生命周期）

```
1. 初始化
   reset_sched_stat()         # 清零统计
   set_sched_alpha(0)         # 创建阶段公平调度（避免高 α 饿死创建线程）
   sl_t0 = get_ticks()
   sl_t_end = sl_t0 + total

2. 创建负载组
   in-parent 组: thread_create 到监控进程
   fork 组:      fork + sl_child_main（主线程 join 不 spin）

3. start_delay 后切换到实验 α
   set_sched_alpha(alpha)     # 切实验 α
   reset_sched_stat()         # 丢弃创建阶段数据
   sl_t0 = get_ticks()        # 时间从这算起

4. 窗口循环
   while (get_ticks() < sl_t_end) {
       sleep(window_ticks);
       sl_measure_window(&w, win, alpha, ...);   # W/D 行
       if (cfg->policy) {
           int new_alpha = cfg->policy(&w, cfg->policy_ud);  # A 行
           if (new_alpha != alpha) {
               alpha = new_alpha;
               set_sched_alpha(alpha);           # 只在变化时调
           }
       }
       printf("S,...");                          # S 行
   }

5. 收尾
   in-parent 组自报 J 行
   waitpid 回收 fork 子进程
   sleep(2) 等 worker 退出
```

### sl_measure_window —— 窗口测量

```c
for each group:
    get_process_sched_stat(pid, &st)      # 内核 syscall 读 eff/run_ticks
    p->run_delta = st.run_ticks - prev_run[i]   # CPU 分配差分
    # in-parent 组：
    w->jobs_delta = sl_gstats[i].jobs - prev_jobs[i]    # D 行
    w->miss_delta = sl_gstats[i].miss - prev_miss[i]
    w->late_delta = sl_gstats[i].late_sum - prev_late[i]

# 公平性指标
entitled_q = eff_tickets × 1000 / total_eff    # 应得份额
share_q    = run_delta × 1000 / total_run      # 实际份额
slowdown_q = entitled_q × 1000 / share_q       # 慢度（>1000 = 落后）
jain_q     = Σ|share-entitled|² 的 Jain 指数
```

## CSV 输出格式

| 行 | 格式 | 含义 |
|----|------|------|
| **W** | `W,win,alpha,pid,name,run_delta,eff_tickets,ready_threads` | 窗口 CPU 分配（每进程每窗口） |
| **D** | `D,win,alpha,jobs_delta,miss_delta,late_delta` | deadline 差分（仅 in-parent jobs 组） |
| **A** | `A,win,alpha_before,alpha_after,action` | 控制器决策轨迹（AIMD 和 AdamW 都输出） |
| **J** | `J,pid,name,threads,jobs,miss,late_sum,late_max,resp_sum,resp_sumsq,resp_min,resp_max` | jobs 组收尾汇总 |
| **K** | `K,pid,name,threads,work` | spin 组收尾：吞吐（burn 迭代数） |
| **S** | `S,win,next_alpha,jain_q,max_slowdown_q` | 公平性指标 |

**A 行的坑**：A 行是最重要的调试信息（α 轨迹 + 动作），任何新策略都必须输出。AdamW 首版漏掉 A 行，导致 α 轨迹拿不到，只能从 W 行的 alpha 字段（probe 值）fallback 提取。

## 内核依赖

```
fork / thread_create / thread_exit / sleep / get_ticks / getpid
exit / waitpid
set_my_tickets / set_sched_alpha / get_process_sched_stat / reset_sched_stat
```

无新增 syscall——v2 架构用 in-parent jobs + 共享计数器，控制器零 syscall 读取反馈。

## 致命教训（必须遵守）

### .bss 零初始化

> **本头文件所有文件级可变变量必须零初始化（.bss）！**
> 非零初始化会进 .data，而用户链接脚本把 .data 捆进只读 .text，
> 写入即 store page fault。任何"默认初值"都在 sl_run/init 函数里赋。

### 其他血泪教训

| 教训 | 后果 | 修复 |
|------|------|------|
| `set_sched_alpha` 每窗口调用 | pass 反复清零，stride 无法累积 | 只在 α 变化时调 |
| `set_sched_alpha` 调用太频繁 | syscall + scale 缓存重算开销压低 ai_run | cooldown=3 / safe_windows>=2 |
| 创建阶段用实验 α | 高 α 下多线程进程创建 worker 被饿死 | 创建阶段 α=0，start_delay 后切换 |
| sleep 唤醒重置 pass | ctrl 获得"免费优先"，miss 方差不可复现 | 只对 tickets≤1 的进程重置 |
| 主线程也跑负载 | runnable=N+1，不符合实验语义 | 主线程 join 不 spin |
| 新策略忘加 A 行 | α 轨迹拿不到 | 任何策略必须输出 A 行 |

## 常量

```c
#define SL_MAX_GROUPS  8      // 最大负载组
#define SL_MAX_THREADS 512    // 最大线程数（64→512，容纳 ai=100 的原配置）
#define SL_NAME_LEN    16     // 组名长度
```

## 各实验如何使用本框架

| 实验 | 负载 | 策略 | 相位 | 特殊点 |
|------|------|------|------|--------|
| exp0 edf | ctrl jobs(fork) + ai spin + log spin | 无（α=1 固定） | 无 | 基线：miss 100% |
| exp1 mech | 3×spin(1/9/25t, 等 tickets) | 无（α 扫描） | 无 | 验证 scale 机制 |
| exp2 edge | ctrl jobs(fork) + ai/log spin | 无（α 扫描） | 无 | 5 配置刻画 edge |
| exp3 aimd | ctrl jobs(in-parent) + ai/log spin | AIMD | 无 | 恒定负载三起点收敛 |
| exp4 dyn | ctrl in-parent + ai phased + log | AIMD | 等分 L-H-L-H | 动态负载自适应 |
| exp5 phase | 同 exp4 | AIMD | 非等分（sl_l_ratio_permil） | L 段比例影响 |
| exp6 adamw | 同 exp4 | SPSA-AdamW | 等分+非等分 | 梯度优化对照 |



