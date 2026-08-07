[← 返回 RmikuOS 主页](../README.md)

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



---

## SMP and Timing Notes

## SMP and Timing Notes

RmikuOS 已经支持 RISC-V 64 与 LoongArch64 的多核启动、per-hart timer、IPI reschedule、基本 TLB shootdown 与多核调度状态维护。调度器使用 `running_on` 记录线程当前所在 hart，避免同一线程被多个 hart 同时取走；timer 与 IPI 路径用于触发抢占和唤醒空闲 hart。

在 QEMU 软件模拟环境中，尤其是 Windows / VMware / Linux / QEMU 多层嵌套时，guest 看到的 hart 数量不一定等于宿主真正并行执行的 CPU 数量。因此：

```text
-smp 8 适合验证多核正确性：
    多核启动
    timer / IPI
    waitpid / reap
    running_on 状态
    TLB shootdown
    锁与死锁检测

-smp 8 不一定适合判断真实性能扩展：
    QEMU TCG 可能只使用少量 host CPU 线程
    串口输出和调试日志会显著污染性能结果
    跨 hart 读取 raw time counter 可能不适合作为 wall-clock
```

因此，性能测试中推荐区分两类时间：

* `get_ticks()`：内核逻辑 tick，适合 sleep、timeout、调度统计和粗粒度观察；
* `read_time()` / monotonic time：基于架构时间计数器并由内核做单调化处理，适合 benchmark 计时。

多核 benchmark 建议使用只在父进程最终打印一次的 quiet 版本，避免 child 频繁 `printf` 把串口 IO 测进去。对于 CPU-bound scaling，可以分别测试「每个 worker 固定工作量」与「总工作量固定」两种模式，并结合宿主机 `top -H` / `ps -L` 观察 QEMU 是否真的有多个执行线程吃满 CPU。
