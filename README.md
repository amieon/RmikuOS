# RmikuOS

RmikuOS 是一个从零实现的教学型操作系统内核，主要用于学习操作系统、体系结构、虚拟化设备、文件系统和调度器设计。

目前 RmikuOS 支持：

* `riscv64`
* `loongarch64`

系统可以在 QEMU 上启动用户态 shell，并从真实的 virtio 块设备中加载 ext4 rootfs。当前系统已经支持用户程序加载、系统调用、进程与线程、VFS、只读 ext4 文件系统、virtio 块设备驱动、基础 shell，以及一套用于调度器实验的用户态 workload 与自适应调度控制器。

RmikuOS 不是一个只会打印 `Hello, world` 的玩具内核。它的目标是逐步构建一个小而完整、能运行真实用户程序、能做系统实验的教学型 OS。

```text
 ____            _ _          ___  ____
|  _ \ _ __ ___ (_) | ___   / _ \/ ___|
| |_) | '_ ` _ \| | |/ / | | | | \___ \
|  _ <| | | | | | |   <| |_| |_| |___) |
|_| \_\_| |_| |_|_|_|\_\\___/___/|____/

        RmikuOS
```

---

## Screenshots

### Boot and Shell

![RmikuOS shell](docs/images/rmikuos_shell.png)

### ext4 Rootfs

![ext4 rootfs](docs/images/ext4_rootfs.png)

### Alpha-Scaled Scheduler

<!-- 插图：alpha 机制实验，effective_tickets / tick_share 随 alpha 变化 -->
![alpha effective vs tick share](logs/figs/alpha_effective_vs_tick_3_8_13.png)

### Adaptive Alpha Controller (AIMD)

<!-- 插图：AIMD 在恒定负载下的 alpha 自适应轨迹（爬升→撞墙→退避→再探测的锯齿） -->
![adaptive alpha trace](TODO_INSERT_adaptive_alpha_trace.png)

### Dynamic Load: AIMD vs Fixed Alpha

<!-- 插图（重点图）：动态负载下 AIMD 跟随负载 vs 固定 alpha。上半 alpha 轨迹 + 下半累计 miss -->
![dynamic load comparison](TODO_INSERT_dynamic_load_alpha_and_miss.png)

---

## Features

### Multi-Architecture Support

RmikuOS 目前支持两个 64 位架构：

```text
riscv64
loongarch64
```

两个架构共用大部分内核逻辑，包括：

* 任务管理
* 进程与线程
* 虚拟内存
* 系统调用
* VFS
* ext4 rootfs
* block cache
* shell 和用户程序
* 调度器与调度实验框架

架构相关部分主要集中在：

* trap handling
* 上下文切换
* 页表切换
* 时钟中断
* QEMU 设备发现
* virtio transport

不同架构使用不同的 virtio transport：

```text
riscv64      -> virtio-mmio
loongarch64 -> virtio-pci
```

---

### User Programs and Shell

RmikuOS 支持从 ext4 rootfs 中加载用户程序。

当前 shell 支持：

* `ls`
* `cat`
* `cd`
* `pwd`
* `stat`
* 外部命令执行
* 相对路径
* 绝对路径
* `argc / argv`
* 当前工作目录 `cwd`

示例：

```text
/ $ ls
bin
etc
home
share
tmp

/ $ cat /etc/motd
Welcome to RmikuOS real ext4 rootfs!

/ $ cd /bin
/bin $ hello
```

第一个用户进程不再依赖内核内置 app table，而是通过 VFS 从 ext4 rootfs 中加载：

```text
/bin/shell
```

---

### Process and Thread

RmikuOS 当前支持基础进程管理：

* `fork`
* `exec`
* `waitpid`
* `exit`
* 进程地址空间复制
* 用户程序 ELF 加载
* 用户态参数传递
* 进程级 fd table

同时支持用户态线程：

* `thread_create`
* `thread_exit`
* `thread_join`
* 同进程线程共享地址空间
* 同进程线程共享 fd table
* 每个线程拥有独立 trap context 和 kernel stack

线程机制使得 RmikuOS 可以构造多线程 workload，并进一步研究进程级公平、线程级并行度和 deadline workload 之间的调度关系。

---

### VFS and File Descriptors

系统实现了基础 VFS 和 fd table。

当前支持：

* `open`
* `close`
* `read`
* `write`
* `getdents`
* `stat`
* `fstat`
* `chdir`
* `getcwd`
* `exec`

标准输入输出也通过 fd 统一处理：

```text
fd 0 -> stdin
fd 1 -> stdout
```

---

### ext4 Rootfs

RmikuOS 使用 ext4 镜像作为 rootfs。

rootfs 由宿主机上的目录模板生成：

```text
user/rootfs/
```

用户可以像组织普通 Linux rootfs 一样组织目录：

```text
user/rootfs/
├── etc/
│   └── motd
├── home/
│   └── miku/
│       └── readme.txt
├── share/
│   ├── docs/
│   └── ascii/
├── tmp/
├── dev/
└── proc/
```

构建脚本会把用户程序编译产物复制到：

```text
/bin
```

因此最终 rootfs 大致形如：

```text
/
├── bin/
│   ├── shell
│   ├── hello
│   ├── ls
│   ├── cat
│   ├── alpha_arg_test
│   ├── edge_deadline_arg_test
│   ├── adaptive_alpha_test
│   └── dynamic_load_exp
├── etc/
│   └── motd
├── home/
├── share/
├── tmp/
├── dev/
└── proc/
```

---

### Virtio Block Device

RmikuOS 当前已经不再只依赖内核内置 ramdisk，而是可以从 QEMU 挂载的真实磁盘镜像读取 ext4 rootfs。

整体路径如下：

```text
User Program
    ↓
Syscall
    ↓
VFS
    ↓
read-only ext4
    ↓
BlockCache
    ↓
BlockDevice
    ├── RamDisk
    ├── VirtioMmioBlockDevice
    └── VirtioPciBlockDevice
```

#### RISC-V virtio-mmio

在 RISC-V QEMU `virt` 机器上，系统通过 virtio-mmio 扫描 virtio block device。

流程：

```text
扫描 virtio-mmio slot
识别 virtio-blk
初始化 legacy virtio-mmio device
配置 virtqueue
提交 block read request
读取 ext4 rootfs
```

#### LoongArch64 virtio-pci

在 LoongArch64 QEMU `virt` 机器上，系统通过 PCI/PCIe 枚举 virtio block device。

流程：

```text
映射 PCI ECAM
枚举 PCI bus/device/function
找到 vendor=0x1af4 的 virtio-blk-pci
分配 BAR
解析 virtio PCI capabilities
初始化 modern virtio-pci device
配置 virtqueue
提交 block read request
读取 ext4 rootfs
```

---

## Scheduler

RmikuOS 实现了基于 stride scheduling 的调度器，并在其上加入了 **alpha-scaled scheduling** 机制，用于在「进程级公平」和「线程级并行度」之间连续调节。alpha 既可以手动固定，也可以由用户态的 **AIMD 自适应控制器**根据 deadline 反馈在运行时动态调整。

### Stride Scheduling

基础 stride 调度器使用 ticket 表达进程权重：

```text
stride = BIG_STRIDE / tickets
```

每次调度选择 `pass` 最小的任务运行，运行后增加对应 stride。这使得调度器在长期运行中近似按照 tickets 比例分配 CPU 时间。

---

### Alpha-Scaled Stride Scheduling

普通进程级 stride 调度只关注进程本身的 tickets。对于多线程进程，这会带来一个问题：

```text
一个单线程 control 进程
一个多线程 AI 进程
一个多线程 logger 进程
```

如果只按照进程 tickets 分配 CPU，多线程进程的并行度无法体现在进程级调度权重中。

RmikuOS 引入 alpha-scaled scheduling：

```text
effective_tickets = base_tickets * scale(ready_threads, alpha)
```

其中缩放因子为：

```text
scale(n, alpha) = n ^ (alpha / 100)
```

即：

```text
alpha = 0   -> n^0 = 1     更接近进程级公平
alpha = 50  -> sqrt(n)     线程数的平方根加权
alpha = 100 -> n^1 = n     完全线程数加权
```

直观理解：

```text
alpha 越小：
    多线程进程不会因为线程多而获得太多额外 CPU。
    更适合 deadline / control workload。

alpha 越大：
    多线程进程会获得更高 effective_tickets。
    更适合 AI、batch、logger 等 throughput workload。
```

alpha 不是一个固定最优参数，而是一个可解释的调度旋钮。

#### Continuous Alpha (连续 alpha)

早期实现中 alpha 只能取离散五档 `{0, 25, 50, 75, 100}`，缩放因子用嵌套整数开方拼出 `n^0.25`、`n^0.75` 等几个点。为了让自适应控制器能停在档位之间的连续甜点上，RmikuOS 把 alpha 推广为 **`[0, 100]` 上的任意整数**：

* `sched_thread_scale(n, alpha)` 用**纯整数定点 + 连续开方**计算 `n^(alpha/100)`，无浮点，no_std 友好；
* 全 alpha 范围**单调不降**，端点精确（`alpha=0 -> 1`、`alpha=100 -> n`），在所有锚点上精度不低于旧的离散实现；
* 由于该函数在调度热路径上被频繁调用（每次 pick 对每个就绪进程都会算一次），内核侧用一张**按需扩容的缓存**保存当前 alpha 下各 `ready_threads` 的因子，alpha 变化时整表重算，其余时间 O(1) 查表。

---

### Scheduler Syscalls

为了进行调度实验，RmikuOS 提供了若干调度相关系统调用：

```text
set_my_tickets(tickets)
set_sched_alpha(alpha)         // alpha ∈ [0, 100]，连续
get_sched_alpha()
get_process_sched_stat(pid, &stat)
reset_sched_stat()
get_ticks()
```

其中 `get_process_sched_stat` 可以观察：

```text
pid
tickets
effective_tickets
ready_threads
run_ticks
stride
pass
```

这些接口使得用户态可以构造 workload、采集调度行为，并实现自适应调度策略。

---

## Scheduler Experiments

RmikuOS 的调度器实验分为四层，逐层递进：

```text
1. Alpha mechanism test          —— 验证机制
2. Edge deadline trade-off test  —— 刻画 trade-off
3. Adaptive alpha controller     —— AIMD 自适应（恒定负载）
4. Dynamic load experiment       —— AIMD vs 固定 alpha（突变负载）
```

实验遵循 **mechanism / policy separation**：

```text
Kernel mechanism:
    alpha-scaled stride scheduling（连续 alpha + 缓存）

Kernel observability:
    调度统计 syscalls（含 deadline / tardiness 原始量）

User-space policy:
    AIMD 自适应 alpha 控制器
```

内核只提供「连续可调的旋钮」和「可观测的统计」，所有控制策略都在用户态实现。

---

### 1. Alpha Mechanism Test

测试程序：`alpha_arg_test`

```text
/ $ alpha_arg_test 50 1 5 7
```

固定每个进程的 base tickets，只改变 alpha 和进程线程数，验证：

```text
effective_tickets 是否随 alpha 和 ready_threads 改变
实际 run_ticks 是否跟随 effective_tickets
```

<!-- 插图：alpha 机制实验 -->
![alpha effective vs tick share](logs/figs/alpha_effective_vs_tick_3_8_13.png)

结论：alpha=0 时多线程进程不会因为线程数更多而获得明显额外 CPU；alpha 增大后，多线程进程的 effective_tickets 上升，实际 tick_share 也随之上升。alpha-scaled scheduling 机制按预期工作。

---

### 2. Edge Deadline Trade-off Test

测试程序：`edge_deadline_arg_test`

```text
/ $ edge_deadline_arg_test 50 1 14 8
```

构造三类 workload：

```text
control:  周期性 deadline workload，关注 jobs / deadline miss / tardiness
AI:       多线程 throughput workload，关注 work counter
logger:   background throughput workload，作为后台干扰负载
```

#### Observability: 从二元 miss 到 tardiness / jitter

除了二元的 deadline miss，control workload 还在用户态自行统计更细的 deadline 质量指标，并以原始整数聚合量的形式打印（平均/标准差等推导留给宿主机的 Python 脚本完成）：

```text
lateness_sum / lateness_max     迟到量（tardiness）：迟了多少，而不只是迟没迟
resp_sum / resp_sumsq           响应时间的和与平方和 -> 均值与标准差（jitter）
resp_min / resp_max             响应时间范围
```

这样即使在 deadline miss 长期为 0 的负载下，响应时间 jitter 仍能反映抢占压力的变化——硬指标看不见的压力，软指标先看见。

结论：alpha 较小时 control 获得较高 CPU share、miss / tardiness 较低，AI throughput 较低；alpha 较大时 AI 的 effective_tickets 上升、work 增加，但 control 在高负载下 miss / tardiness 上升。alpha 因此形成 **deadline safety 与 throughput 之间的 trade-off**。

---

### 3. Adaptive Alpha Controller (AIMD)

测试程序：`adaptive_alpha_test`

```text
/ $ adaptive_alpha_test 50 1 14 8           # adaptive（默认）
/ $ adaptive_alpha_test 50 1 14 8 fixed     # 固定 alpha baseline
```

控制器不把策略硬编码进内核，而是在用户态以 **AIMD（加性增、乘性减）** 消费 control 的 tardiness 信号，在运行时调节连续 alpha：

```text
加性增 (Additive Increase):
    control 连续安全（窗口内无新增迟到）时，alpha += INC，小步向上探测吞吐。

乘性减 (Multiplicative Decrease):
    窗口内出现明显迟到时，按危险程度分档乘性回退（见下）。

滞回带 (Hysteresis):
    在 SAFE 与 DANGER 之间设灰区，单次偶发 miss 不触发调整，抑制抖动。

冷却 (Cooldown):
    刚回退后保留一个观察窗口，避免把上一窗口的 backlog 误判为当前 alpha 不安全。
```

#### 分档乘性退避 (Tiered Backoff)

普通 AIMD 的乘性减是固定比例的，但「轻微超载」和「瞬间全崩」用同样的退避力度并不合理。RmikuOS 让退避量正比于危险程度（按 `miss_per_1000` 分档）：

```text
miss_per_1000 >= 900   ->  alpha *= 0.4   // 几乎全崩，一步逃逸
miss_per_1000 >= 500   ->  alpha *= 0.6   // 重度
否则                    ->  alpha *= 0.8   // 轻度，温和回退
```

「伤得越重退得越狠」显著压缩了负载突变瞬间的损失窗口（见动态负载实验）。

#### 恒定负载下的结论

在恒定负载下，AIMD 在**无需预先知道最优 alpha** 的情况下，自动收敛到接近最优固定策略的工作点，并能停在离散档位够不到的连续甜点（如 alpha=49、77）上。横跨轻、中、重多种负载验证（含未参与调参的负载 case），AIMD 大多达到或超过固定策略：**用与最佳固定 alpha 相当的 deadline 质量，换取更高的吞吐**，即免去人工逐负载试参的过程。

<!-- 插图：AIMD 恒定负载下的 alpha 自适应轨迹 -->
![adaptive alpha trace](TODO_INSERT_adaptive_alpha_trace.png)

<!-- 插图（可选）：恒定负载 AIMD vs 固定 alpha 的 tardiness-throughput 帕累托图 -->
![adaptive vs fixed pareto](TODO_INSERT_aimd_vs_fixed_pareto.png)

> 说明：当前实验多为单次或少量重复运行，结果存在噪声，极轻负载下尤为明显；增加重复次数做统计聚合是后续工作。

---

### 4. Dynamic Load Experiment

测试程序：`dynamic_load_exp`

```text
/ $ dynamic_load_exp 50 1 100 16            # adaptive
/ $ dynamic_load_exp 90 1 100 16 fixed      # 固定 alpha baseline
```

恒定负载下「最优 alpha」不变，AIMD 找到甜点后即停，因此只能「贴着」最优固定策略。为了展示自适应的本质价值，该实验在**同一次运行内**让 AI 负载分三段突变：

```text
phase 0 (轻)：仅少量 AI 线程活跃，control 空闲 -> alpha 应爬高抢吞吐
phase 1 (重)：全部 AI 线程活跃，control 受压 -> alpha 应快速退避保 deadline
phase 2 (轻)：AI 退回少量，负载回落       -> alpha 应重新爬高
```

固定 alpha 在变化负载下必然顾此失彼：固定高在 phase 1 害死 control，固定低在 phase 0/2 浪费吞吐。AIMD 则**跟着负载呼吸**——在 phase 0/2 爬高、phase 1 瞬间分档退避。

<!-- 插图（重点图）：上半 alpha 轨迹（AIMD 呼吸曲线 vs 固定水平线），下半累计 control miss -->
![dynamic load comparison](TODO_INSERT_dynamic_load_alpha_and_miss.png)

一组代表性结果（control=1, ai=100, logger=16，轻→重→轻）：

| 策略            | control miss | max tardiness | AI work |
| --------------- | ------------ | ------------- | ------- |
| fixed α=90      | 502 / 786    | 775           | 324656  |
| fixed α=60      | 154 / 900    | 19            | 209507  |
| fixed α=30      | 0 / 900      | 0             | 137438  |
| **AIMD (自适应)** | **92 / 900** | 94            | 207556  |

结论：与最佳折中固定值 α=60 相比，AIMD 在**吞吐基本持平**（207556 vs 209507）的同时，把 deadline miss **降低约 40%**（154 → 92）。这是固定策略做不到的帕累托改进——因为 AIMD 能在负载突变瞬间按危险程度快速退避，而任何固定 alpha 只能被动挨打。

---

## Build and Run

### RISC-V 64

```bash
./run.sh riscv64 debug      # Debug
./run.sh riscv64 release    # Release
```

RISC-V 使用 QEMU `virt` 机器和 virtio-mmio 块设备。

### LoongArch64

```bash
./run.sh loongarch64 debug      # Debug
./run.sh loongarch64 release    # Release
```

LoongArch64 使用 QEMU `virt` 机器和 virtio-pci 块设备。

---

## Rootfs

rootfs 模板目录是 `user/rootfs/`，用户程序源码放在 `user/src/`。构建后用户程序进入 `user/build/<arch>/`，随后被打包进 ext4 镜像的 `/bin`。

生成的 rootfs 镜像位于：

```text
target/fs-riscv64.img
target/fs-loongarch64.img
```

修改 `user/rootfs` 或 `user/src` 后重新运行 `./run.sh <arch> debug`，即可在系统 shell 中看到新的文件结构和用户程序。

---

## Experiment Workflow

调度器实验通常在 LoongArch64 上运行：

```bash
./run.sh loongarch64 debug
```

进入 RmikuOS shell 后执行：

```text
/ $ alpha_arg_test 50 1 5 7
/ $ edge_deadline_arg_test 50 1 14 8
/ $ adaptive_alpha_test 50 1 25 9
/ $ dynamic_load_exp 50 1 100 16
```

也可以通过宿主机重定向批量输入命令并抓取日志：

```bash
./run.sh loongarch64 debug < logs/adaptive_alpha_cmds.txt 2>&1 \
  | tee logs/adaptive_alpha_raw.log
```

分析脚本将原始日志转换为 CSV 并生成图表：

```bash
# AIMD 轨迹 / 聚合统计 / tardiness / jitter
python3 scripts/plot_adaptive_alpha_log.py \
  logs/adaptive_alpha_raw.log logs/figs_adaptive

# AIMD vs 固定 alpha 的 tardiness-throughput 对照
python3 scripts/plot_aimd_vs_fixed.py \
  logs/adaptive_alpha_raw.log logs/figs_compare

# 动态负载：alpha 轨迹 + 累计 miss 对照
python3 scripts/plot_dynamic_load.py \
  logs/dynamic_raw.log logs/figs_dynamic
```

---

## Current Architecture

```text
                    User Programs
                         │
                         ▼
                      Syscall
                         │
        ┌────────────────┼────────────────┐
        ▼                ▼                ▼
       VFS            Scheduler        Process/Thread
        │                │                │
        ▼                ▼                ▼
  read-only ext4   alpha-scaled       address space
        │          stride scheduler    fd table
        ▼          (continuous alpha
   Block Cache       + AIMD policy)
        │
        ▼
   BlockDevice
   /         \
  /           \
virtio-mmio  virtio-pci
 RISC-V      LoongArch64
```

---

## Current Status

已经完成：

* RISC-V 64 / LoongArch64 内核启动
* trap handling、syscall、进程调度
* stride scheduling
* alpha-scaled scheduling（**连续 alpha** `[0,100]`，纯整数幂 + 热路径缓存）
* 调度统计接口（含 deadline / tardiness / jitter 原始量）
* `fork / exec / waitpid`
* 用户态线程 `thread_create / thread_exit / thread_join`
* 用户态 shell、`argc / argv`
* fd table、`open / close / read / write`、`stat / fstat`、`getdents`、`cwd / chdir / getcwd`
* VFS、read-only ext4 rootfs
* BlockDevice trait、RamDisk、BlockCache
* RISC-V virtio-mmio / LoongArch64 virtio-pci block device
* 从 ext4 `/bin/shell` 启动 init shell
* rootfs overlay、文件系统压力测试
* alpha mechanism test
* edge deadline 实验（含 tardiness / jitter 观测）
* **AIMD 自适应 alpha 控制器（含分档退避）**
* **固定 alpha 对照实验**
* **动态负载实验（AIMD vs 固定 alpha 的帕累托对照）**

---

## Roadmap

### Writable Filesystem

短期目标先实现一个简单的内存文件系统（`tmpfs / ramfs`），而非直接做完整可写 ext4：

* `open(O_CREAT)`、`write`、`mkdir`、`unlink`
* 临时可写目录

### Scheduler

* 多重复实验 + 统计聚合，给现有结论加上误差带
* per-process alpha 或调度 class（让 control/AI/logger 各自一档，而非全局单旋钮）
* 更复杂的反馈控制器（如以 tardiness 为误差信号的 PI 控制）
* 更丰富的动态负载模式（多阶段、随机突变）

### Network

后续实现 virtio-net，并逐步支持 Ethernet frame / ARP / IPv4 / ICMP / UDP / 简单 socket API。

---

## Project Goal

RmikuOS 的目标不是追求一次性实现完整 Unix，而是逐步构建一个能真实运行、能调试、能扩展、能做实验的教学型操作系统。

当前阶段的重点是：

```text
让用户程序从真实 ext4 rootfs 中运行；
让 RISC-V 和 LoongArch64 都能通过 virtio 块设备访问 QEMU 磁盘；
在教学内核中实现可解释、可观测、可实验、可自适应的调度器机制。
```
