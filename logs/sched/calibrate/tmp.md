
# RmikuOS Scheduling Lab — Mechanism/Policy Separation

自研 SMP 内核上的调度实验框架。内核只提供旋钮（period, cpu_budget, priority），
调度策略（EDF / AIMD 退避 / AdamW 轨迹）全部在用户态 `schedlab.c` 中实现。

## 平台

| 平台 | 角色 | 1 tick | 备注 |
|------|------|--------|------|
| riscv64 (QEMU) | **主力** | 21.95 ms | 快 5.2×，全部实验在此跑 |
| loongarch64 (QEMU) | 验证 | 113.84 ms | 仅跑 3 个配置，证明跨平台 |

本实验遵循严格的自底向上验证原则。所有策略的有效性验证均在单核（SMP=1）环境下完成，以隔离硬件并发带来的非确定性噪声，确保观测到的 Deadline 质量纯粹反映调度算法的数学特性。SMP 扩展实验仅作为系统级压力测试，用于评估调度器在并发路径下的锁开销与扩展性边界，不作为核心算法优劣的判据。

## 标定结果（calibrate, reps=5）

### Tick ↔ Time 线性性

两平台 R² = 1.0000，五个跨度（200–4000 tick）完美线性。
Tick 是可信的调度时钟，后续指标统一以 tick 为单位。

### Burn 负载（sl_burn）

| iters | riscv64 (ticks) | loongarch64 (ticks) |
|-------|-----------------|---------------------|
| 50 000 | 0.41 ± 0.98 | 0.43 ± 0.25 |
| 100 000 | 0.73 ± 0.79 | 0.90 ± 0.54 |
| 200 000 | 1.46 ± 0.15 | 1.65 ± 0.33 |
| **400 000** | **2.95 ± 0.51** | **3.45 ± 0.14** |
| 800 000 | 6.14 ± 0.32 | 6.73 ± 0.27 |
| 1 600 000 | 11.53 ± 1.26 | 13.04 ± 0.76 |

- 小 iters（< 200k）噪声大是 tick 量化伪影（0/1 舍入），非 bug。
- burn 与 iters 线性（偏差 ≤ 5%），可按比例缩放。
- **两平台 burn(400000) ≈ 3 ticks，定性一致。**

### 漂移

**正式实验前必须确认 < 5%**
- loongarch64: 0%（稳定）
- riscv64: 3%（宿主机瞬时负载）

### 推荐 burn 值（riscv64 锚点）

| 目标 ticks | burn(iters) |
|-----------|-------------|
| 1 | 136 000 |
| 2 | 271 000 |
| 3 | 407 000 |
| 4 | 543 000 |
| 5 | 678 000 |

## 实验任务配置

三组任务，`schedlab` 统一驱动：

| 任务 | period | cpu_budget | burn | 设计意图 |
|------|--------|------------|------|----------|
| ctrl | 4 | 2 | 400 000 (~3 ticks) | 1.5× 超载，触发 AIMD 退避 |
| rt | 2 | 1 | 136 000 (~1 tick) | 刚好满载，硬实时基准 |
| bg | 8 | 6 | 400 000 (~3 ticks) | 轻载，填充空闲 |

ctrl 的 burn(3 ticks) > cpu_budget(2 ticks) 是**故意的**：
确保 miss deadline 事件持续发生，让退避机制有东西可退。

## 实验列表

| # | 名称 | 核心问题 | 关键指标 |
|---|------|----------|----------|
| 0 | 基线 EDF | 无退避时 ctrl miss rate | miss_per_1000 |
| 1 | 固定退避 | 固定 α 的 trade-off 曲线 | miss vs latency |
| 2 | AIMD 自适应 | α 收敛轨迹 | α(t), miss(t) |
| 3 | AdamW 轨迹 | 动量+权重衰减 vs AIMD | 收敛速度, 振荡 |
| 4 | SMP 扩展 | 8 核下策略是否仍有效 | per-core miss |
| 5 | 扰动恢复 | 突发负载后 α 恢复时间 | recovery ticks |

每个实验跑 **5 reps**（riscv64），取 mean ± std。
LoongArch 验证：实验 2/4/5 各跑 1 rep。

## 文件结构

```
user/sched/
├── calibrate.c      # 标定工具 (v2)
├── schedlab.c       # 实验主驱动
├── schedlab.h       # 共享定义 (sl_burn, 参数结构)
└── README.md        # 本文件

output/
├── calib_riscv.log
├── calib_loong.log
├── exp0_edf.csv
├── exp1_fixed.csv
├── ...
└── plots/           # 宿主机 Python 出图
```

## 快速开始

```bash
./calibrate 5
```

## 注意事项

- burn 值两平台通用（400000），不需要分别调。
- 所有时间指标用 tick，报告里换算时注明 1 tick = 21.95 ms。
- `get_ticks()` 是整数，burn < 1 tick 时测量无意义，别用。



# RmikuOS Scheduling Lab — Mechanism/Policy Separation

自研 SMP 内核上的调度实验框架。内核只提供旋钮（period, cpu_budget, priority），
调度策略（EDF / AIMD 退避 / AdamW 轨迹）全部在用户态 `schedlab.c` 中实现。

## 平台

| 平台 | 角色 | 1 tick | 备注 |
|------|------|--------|------|
| riscv64 (QEMU) | **主力** | 21.95 ms | 快 5.2×，全部实验在此跑 |
| loongarch64 (QEMU) | 验证 | 113.84 ms | 仅跑 3 个配置，证明跨平台 |

## 标定结果（calibrate, reps=5）

### Tick ↔ Time 线性性

两平台 R² = 1.0000，五个跨度（200–4000 tick）完美线性。
Tick 是可信的调度时钟，后续指标统一以 tick 为单位。

[linearity](calib_linearity.png)

### Burn 负载（sl_burn）

| iters | riscv64 (ticks) | loongarch64 (ticks) |
|-------|-----------------|---------------------|
| 50 000 | 0.41 ± 0.98 | 0.43 ± 0.25 |
| 100 000 | 0.73 ± 0.79 | 0.90 ± 0.54 |
| 200 000 | 1.46 ± 0.15 | 1.65 ± 0.33 |
| **400 000** | **2.95 ± 0.51** | **3.45 ± 0.14** |
| 800 000 | 6.14 ± 0.32 | 6.73 ± 0.27 |
| 1 600 000 | 11.53 ± 1.26 | 13.04 ± 0.76 |

- 小 iters（< 200k）噪声大是 tick 量化伪影（0/1 舍入），非 bug。
- burn 与 iters 线性（偏差 ≤ 5%），可按比例缩放。
- **两平台 burn(400000) ≈ 3 ticks，定性一致。**

[Burn](calib_burn.png)

### 漂移

**正式实验前必须确认 < 5%**
- loongarch64: 0%（稳定）
- riscv64: 3%（宿主机瞬时负载）

[Summary](calib_summary.png)

### 推荐 burn 值（riscv64 锚点）

| 目标 ticks | burn(iters) |
|-----------|-------------|
| 1 | 136 000 |
| 2 | 271 000 |
| 3 | 407 000 |
| 4 | 543 000 |
| 5 | 678 000 |

## 实验任务配置

三组任务，`schedlab` 统一驱动：

| 任务 | period | cpu_budget | burn | 设计意图 |
|------|--------|------------|------|----------|
| ctrl | 4 | 2 | 400 000 (~3 ticks) | 1.5× 超载，触发 AIMD 退避 |
| rt | 2 | 1 | 136 000 (~1 tick) | 刚好满载，硬实时基准 |
| bg | 8 | 6 | 400 000 (~3 ticks) | 轻载，填充空闲 |

ctrl 的 burn(3 ticks) > cpu_budget(2 ticks) 是**故意的**：
确保 miss deadline 事件持续发生，让退避机制有东西可退。

## 实验列表

| # | 名称 | 核心问题 | 关键指标 |
|---|------|----------|----------|
| 0 | 基线 EDF | 无退避时 ctrl miss rate | miss_per_1000 |
| 1 | 固定退避 | 固定 α 的 trade-off 曲线 | miss vs latency |
| 2 | AIMD 自适应 | α 收敛轨迹 | α(t), miss(t) |
| 3 | AdamW 轨迹 | 动量+权重衰减 vs AIMD | 收敛速度, 振荡 |
| 4 | SMP 扩展 | 8 核下策略是否仍有效 | per-core miss |
| 5 | 扰动恢复 | 突发负载后 α 恢复时间 | recovery ticks |

每个实验跑 **5 reps**（riscv64），取 mean ± std。
LoongArch 验证：实验 2/4/5 各跑 1 rep。

## 文件结构

```
user/sched/
├── calibrate.c      # 标定工具 (v2)
├── schedlab.c       # 实验主驱动
├── schedlab.h       # 共享定义 (sl_burn, 参数结构)
└── README.md        # 本文件

output/
├── calib_riscv.log
├── calib_loong.log
├── exp0_edf.csv
├── exp1_fixed.csv
├── ...
└── plots/           # 宿主机 Python 出图
```

## 快速开始

```bash
./calibrate 5
```

## 注意事项

- burn 值两平台通用（400000），不需要分别调。
- 所有时间指标用 tick，报告里换算时注明 1 tick = 21.95 ms。
- `get_ticks()` 是整数，burn < 1 tick 时测量无意义，别用。


