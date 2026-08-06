[← 返回 RmikuOS 主页](../README.md)

---

## User Programs in C, C++, Rust, Java and Lua

RmikuOS 的用户程序可以用 **C、C++、Rust, Java and Lua** 编写。前三者编译成相同格式的静态 ELF、走完全相同的系统调用 ABI（号在 `a7`/`r11`，参数在 `a0..`/`r4..`，触发 `ecall` / `syscall 0`，返回值在 `a0`/`r4`），因此在内核看来完全等价——**支持 C++ 用户程序内核侧零改动**，只是多了一条产出兼容 ELF 的编译流程。 后者通过javac编译成class文件后用cpp编写的jvm进行使用。

### syscall ABI 是语言无关的

系统调用的本质是「按约定把号和参数放进寄存器，触发陷入指令」。这套约定定义在 ELF + 寄存器层面，与源语言无关：C 用一小段汇编（`syscall_<arch>.S`）实现，C++ 复用同一套汇编，Rust 用 inline asm 实现，三者只要寄存器约定一致，内核 trap handler 取参数的方式就完全相同。这也是为什么加入 C++ 支持不需要改内核——它加载的是 ELF、执行的是机器码、通过寄存器约定交互。

### C 用户库（分层头文件）

C 侧的用户库按依赖层次拆分为一组单一职责的头文件，用户程序只需 `#include "user.h"` 即可获得全部接口：

```text
types.h     基础类型(usize / isize)
syscall.h   系统调用号 + syscall3 / syscall6
flag.h      open flags(O_RDONLY / O_WRONLY / O_RDWR / O_CREAT / O_TRUNC / O_APPEND)
io.h        strlen + read/write + open/close/create + puts/put_char
process.h   exit/fork/waitpid/getpid/yield/sleep + exec
fs.h        dirent/stat + getdents/stat/chdir/getcwd + mkdir/unlink/rmdir + lseek/ftruncate/fsync/truncate/rename
mem.h       PROT_* + mmap/munmap + malloc/free/calloc
lock.h      spinlock / mutex
thread.h    thread_create/exit/join + 栈管理
sched.h     tickets / alpha / sched_proc_stat / get_ticks
ipc.h       pipe / dup2
net.h       socket 封装(socket/bind/connect/listen/accept/send/recv/sendto/recvfrom/close)
string.h    标准字符串/内存函数(strlen/strstr/memmove 等,static inline)+ trim / copy_str / read_file
fmt.h       parse_int / put_int / put_hex / uprintf / snprintf 族
env.h       getenv/getenv_r / setenv / unsetenv / clearenv / listenv
```

### 用户态堆分配器（Slab + First-Fit 混合）

RmikuOS 的内核 `mmap` 只提供**裸页级匿名映射**（`mmap(len, prot)` 按页分配，`munmap(addr, len)` 局部解除），不做堆管理。用户态通过一套**混合分配器**实现细粒度 `malloc/free`，核心思想是**向内核批量要页，在用户态精细分配**。

#### 架构

```
用户程序
   │
   ▼
malloc/free ──► Slab 分配器 (小对象, O(1))
   │                │
   │                ▼
   │         首次适应分配器 (大对象, ≥1024B)
   │                │
   └────────────────┘
            │
            ▼
      syscall mmap (按页向内核申请)
            │
            ▼
      内核页分配器
```

#### 小对象快速路径：Slab 分配器

对 ≤1024 B 的对象按 size class 分档（16, 32, 48, 64, 96, 128, 192, 256, 384, 512, 768, 1024），每档维护一个自由链表：

- **分配**：O(1) 弹出一个空闲对象
- **释放**：O(1) 压回自由链表
- **批量填充**：某档耗尽时，一次性 `mmap` 一个 chunk（默认 64 KB），切成多个对象挂入链表，摊平系统调用开销

每个 slab 对象头部嵌入 `size_t` 标记（高位置 `SLAB_MAGIC`），`free` 时通过魔数识别走 slab 路径还是大对象路径，无需额外元数据结构。

#### 大对象路径：首次适应 + 分裂/合并

大于 1024 B 的请求走首次适应（First-Fit）链表：

- 在已有空闲块中找第一个足够大的块
- **分裂**：若块远大于请求，切出尾部作为新空闲块，减少内部碎片
- **扩展**：无合适块时，向内核 `mmap` 申请新 chunk（页对齐），挂入链表
- **合并**：释放时检查相邻块是否均为空闲，是则合并为更大块，减少外部碎片

#### 延迟归还策略

`free` 后内存块**不立即 `munmap` 还回内核**，而是留在用户态空闲池复用。该策略基于两个观察：

1. **工作负载局部性**：同一进程短期内重复申请/释放同尺寸内存的概率极高，缓存可避免频繁陷入内核
2. **页粒度不匹配**：`munmap` 必须以页为单位，而 `malloc` 分配的块远小于页，立即归还会导致无法复用的碎片页

进程地址空间足够时，该策略零开销；需要严格收缩时，用户可显式调用 `munmap`。

#### 线程安全

分配器全局持有一把 `mutex_t`，`malloc`/`free` 入口加锁、出口解锁。由于当前用户程序以单线程或**粗粒度同步**为主，该设计简单可预测；若后续引入 per-thread arena，可进一步消除竞争。


### C++ 用户库与 `stdcompat.h` 桥接

RmikuOS 进一步支持 **C++17**，并通过一个零侵入的桥接头文件 `stdcompat.h`，让原本依赖标准库的 C++ 项目几乎**零改动**即可在裸运行时上编译运行。

**设计：`std` 接口桥接到裸实现**

`stdcompat.h` 不实现完整的 ISO C++ 标准库，而是提供一层**兼容接口**：

```text
原代码写 std::vector<T>，实际调用 mv::Vector<T>
原代码写 std::exp(x)，实际调用 mymath::exp(x)
原代码写 std::printf(fmt, ...)，实际调用 uvprintf(fmt, ...)
```

所有桥接通过 `namespace std { using ... }` 和模板特化完成，算法代码本身无需修改。例如：

```cpp
#include "my/stdcompat.h"   // 仅此一行替换
// 以下代码与标准库版本完全一致：
std::vector<float> v;
std::exp(1.0);
std::printf("hello\n");
```

**裸运行时数学库**

`stdcompat.h` 的底层依赖一组从零实现的数学函数（`exp`/`log`/`sqrt`/`pow`/`sin`/`cos`），采用 fdlibm 风格的参数约减 + Remez 优化多项式，**不依赖任何外部 libc**：

- `exp`：双精度全精度，相对误差 `< 1e-15`
- `log`：双精度全精度，相对误差 `< 1e-15`
- `pow`：基于 `exp(log)`，整数指数走快速幂优化

AdamW 优化器中的 `pow(b1, t)` 进一步通过**递推**（`b1t *= b1`）消除每步的幂运算，避免在训练热路径上调用数学库。

**案例：VeryEasyGCN 图神经网络**

作为验证，我将独立项目 [VeryEasyGCN](https://github.com/amieon/VeryEasyGCN)（纯 C++ 实现的 GCN/GAT 图神经网络，含完整反向传播与数值梯度检验）完整移植到 RmikuOS 上运行。

**移植改动量**：仅把 `#include <vector>` 等标准库头文件替换为 `#include "my/stdcompat.h"`，**算法代码零改动**。

**运行示例**（真实 Cora 数据集，2708 节点，1433 特征，7 类）：

```text
/ $ train_cora /gcn/cora.content /gcn/cora.cites
[dataset] Cora: nodes=2708 features=1433 classes=7 nnz=13264 | train=140 val=500 test=1000

optimizer=AdamW lr=0.009999 wd=0.000500 dropout=0.500000
epoch | train_loss | train_acc | val_acc
    0 | 1.945590 | 0.464285 | 0.475999
   20 | 1.750075 | 0.835714 | 0.721999
   40 | 1.289533 | 0.942857 | 0.788000
   60 | 0.794022 | 0.971428 | 0.817999
   80 | 0.469486 | 0.978571 | 0.824000
  100 | 0.333761 | 0.992857 | 0.812000
  120 | 0.228583 | 0.992857 | 0.816000
  140 | 0.179066 | 0.992857 | 0.808000
  160 | 0.138511 | 1.000000 | 0.804000
  180 | 0.115274 | 1.000000 | 0.813999
  199 | 0.085100 | 1.000000 | 0.812000

==> final TEST accuracy = 0.783000
```

**与标准结果对比**：

| 模型 | VeryEasyGCN (标准库) | RmikuOS (裸运行时) | 差距  |
| ---- | -------------------- | ------------------ | ----- |
| GCN  | **78.5%**            | **78.3%**          | 0.2%  |
| GAT  | **76.1%**            | **77.5%**          | -1.4% |

裸运行时的数值精度与 Windows/Linux 标准库版本**逐位一致**，准确率落在同一区间。

**数值精度验证**（`gradcheck`，解析梯度 vs 中心差分，double）：

```text
/ $ gradcheck
gradient check (analytic vs numeric, central diff, double)
  W1  relative error = 2.706e-09, absolute error = 2.019e-10
  W2  relative error = 1.297e-08, absolute error = 2.058e-12
  AS1 relative error = 2.453e-09, absolute error = 2.817e-11
  AS2 relative error = 1.779e-08, absolute error = 7.565e-13
  AD1 relative error = 1.404e-08, absolute error = 1.437e-12
  AD2 relative error = 3.709e-08, absolute error = 8.988e-13
  -> PASS (backward is correct)
  -> PASS (backward is correct)
```

**标准库桥接覆盖**：`std::vector`/`std::string`/`std::unordered_map`/`std::ifstream`/`std::istringstream`/`std::exp`/`std::log`/`std::sqrt`/`std::pow`/`std::mt19937` 全部通过 `stdcompat.h` 桥接到裸运行时实现，算法代码无需任何改动。



### Rust 用户程序

Rust 用户程序以 `#![no_std]` + `#![no_main]` 编写，自定义 `_start` 入口（置于 `.text.entry` 段，匹配链接脚本的加载地址 `0x10000`），并提供 `panic_handler`。RmikuOS 支持两种 Rust 程序形态：

* **单文件 Rust**：自包含的单个 `.rs`（自带 syscall 封装与 `_start`），用 `rustc` 直接编译，适合短小的测试程序，放在 `user/src` / `user/tests`。
* **cargo workspace Rust**：依赖公共库 `ulib` 的程序，通过 `use ulib::...` 正规模块引用，用 `cargo` 构建整个 workspace，适合较大的工程，放在 `user/rust/programs/<crate>`。

公共库 `ulib` 是一个 no_std crate，按模块对应 C 的用户库分层：

```text
ulib::number    系统调用号
ulib::syscall   syscall3 / syscall6(架构分离,inline asm)
ulib::io        read/write/open/close/create/puts
ulib::process   exit/fork/waitpid/getpid/yield/exec
ulib::fs        Stat/DirEntry + stat/getdents/mkdir/unlink/rmdir/chdir/getcwd + lseek/ftruncate/fsync/truncate/rename
ulib::sched     tickets/alpha/SchedProcStat/get_ticks
```

一个使用 `ulib` 的程序长这样：

```rust
#![no_std]
#![no_main]

use ulib::io::puts;
use ulib::process::exit;

#[no_mangle]
#[link_section = ".text.entry"]
pub extern "C" fn _start() -> ! {
    puts("hello from rust ulib\n");
    exit(0);
}

#[panic_handler]
fn panic(_: &core::panic::PanicInfo) -> ! {
    exit(1);
}
```

### 两个架构的链接差异

riscv64 与 loongarch64 在 Rust 程序链接上有一处必须注意的差异：

* **riscv64** 经 `rust-lld` 直接链接，链接器本身不引入 C 运行时，只需链接脚本与 `relocation-model=static`。
* **loongarch64** 经 `loongarch64-unknown-linux-gnu-gcc` 链接，该 gcc 默认引入 `crt1.o` 与 libc，会与 no_std 的自定义 `_start` 冲突（`multiple definition of _start` / 未定义的 `__libc_start_main`），因此需要额外传入 `-nostartfiles -nostdlib` 禁用标准启动文件与标准库。

此外，内核与用户程序在 loongarch 下共用 target triple `loongarch64-unknown-none`，根目录 `.cargo/config.toml` 中给内核设置的链接脚本会经 cargo 的配置层叠继承污染用户程序构建；用户程序构建改用 `RUSTFLAGS` 环境变量传链接参数（覆盖而非追加 config 中的 rustflags）以隔离。


### Java：手写 JVM 与装载期 AOT

标准库版本：[RmikuJVM](https://github.com/amieon/RmikuJVM)

Java 是 RmikuOS 的第四种用户态语言——运行时不是移植的，而是从零实现的 JVM：class 文件解析、字节码解释器，以及一个**装载期 AOT 编译器**（类加载时把方法体直接编译为宿主机器码，riscv64 与 loongarch64 各一个手写后端，发射器即指令编码器，无外部依赖）。JVM 本身也是一个经 `stdcompat.h` 桥接的 C++ 用户程序，顺带压测了桥接层的线程 / 进程 / 内存桩。

```text
Ray.class → classfile 解析（常量池 / 方法 / Code 属性）
                  │
          ┌───────┴────────┐
          ▼                ▼
     字节码解释器      装载期 AOT（逐方法编译为机器码）
                          │
      统一入口 entry(AotFrame*) · helper ABI（20 个回调）
      AotFrame 帧链挂 vm.aot_top，供 GC 保守扫描
                          │
      riscv64 后端 / loongarch64 后端（共享 aot_common 驱动）
```

Java 侧经 `Rmiku.*` native 类桥接系统调用（IO / 线程 / 内存 / 进程 / 网络），与 C / C++ / Rust 用户程序共享同一套 syscall ABI，同一份 `.class` 双架构直接运行。

**旗舰验证：RmikuRay**——100×40 ASCII 光线追踪器，纯 Java、全程 16.16 定点（**零浮点指令**）：Phong 光照、Blinn 高光、硬阴影、一次镜面反射、棋盘格地板、天空渐变 + 太阳圆盘。两个 `Rmiku.Thread` worker 各渲半幅，经 `/tmp/ray_bandN.txt` 拼帧。


### 功能

- **Class 文件解析器**：完整支持常量池、方法表、字段表、异常表
- **栈机解释器**：约 80 条指令（iadd、imul、goto、invokestatic、invokevirtual、new、数组、ldc、athrow 等）
- **Mark-Sweep GC**：单核 STW，自适应触发阈值
- **装载期 AOT**：类加载时把字节码翻译成宿主机器码（无 JIT 预热，热点方法无需回退解释器）
- **双架构后端**：RISC-V RV64GC 和 LoongArch64 共用同一套翻译器，仅代码生成函数不同
- **本地方法桥接**：Java `native` 方法通过分发表直接映射到系统调用（print、文件 IO、exit）
- **裸机友好数据结构**：手写 Treap 替代 std::map，极简 FILE 封装，不依赖 STL

---

### 架构

```
Java 源码 (.java)
       |
   javac（宿主机编译）
       v
  字节码 (.class)
       |
  +------------------+
  |  类加载器         |  <-- 解析常量池，解析符号引用
  +------------------+
       |
  +------------------+
  |  AOT 翻译器       |  <-- 字节码 → RISC-V / LoongArch 机器码
  +------------------+
       |
  +------------------+
  |  机器码           |  <-- mprotect 改 RX 权限，直接跳转执行
  +------------------+
       |
   RmikuOS 系统调用
```

### 性能

所有数据在 RmikuOS 裸机（QEMU）上实测，通过硬件时钟 `rdcycle` / `rdtime.d` 读取真实时间。

#### 绝对时间

![绝对时间](../logs/jvm/bench_abs.png)


#### 结论分析

**1. 纯整数运算：AOT 极其成功**

- `alu_mix` **50 倍+** 加速，20M 次循环从 5.4 秒降到 0.1 秒
- `branch_heavy` **30 倍+** 加速，10M 次分支从 3.6 秒降到 0.1 秒
- `mul_lcg` **16-18 倍** 加速

这是 AOT 的核心价值：把 `iadd`/`imul`/`ishl`/`goto` 等指令翻译成原生机器码，消除了解释器的 dispatch 开销。

**2. 数组访问：5-6 倍，合理**

瓶颈在内存读写，AOT 后也是 `ld`/`st` 指令，提升有限。

**3. 方法调用、对象、字符串：几乎没提升（1-1.5 倍）**

这是**问题区域**，不是"没提升"，而是 AOT 代码生成有缺陷：

- `static_call`：AOT 后 494ms vs 旧版 811ms，只快 1.6 倍。`invokestatic` 应该翻译成直接 `call` 指令，如果还是走桩函数或解释器 fallback，就会这样。
- `object_field`：100K 次 `new` + `getfield`/`putfield`，AOT 后 352ms vs 403ms。`new` 指令的 AOT 可能还在调用 `heap.alloc_object`（这个无法避免），但字段访问应该内联成偏移访问。
- `string_ldc`：1M 次字符串常量加载，AOT 后 628ms vs 753ms。`ldc` 加载字符串常量涉及 `heap.alloc_string`，这是 native 调用，AOT 优化不了。

**4. LoongArch 解释器比 RISC-V 慢 8 倍，AOT 后只慢 8.5 倍**

- 旧版 `alu_mix`：LoongArch 42s vs RISC-V 5.4s（**7.8 倍慢**）
- 新版 `alu_mix`：LoongArch 865ms vs RISC-V 101ms（**8.5 倍慢**）

AOT 没有缩小差距，说明 LoongArch 的 codegen 后端生成的机器码质量比 RISC-V 差，或者 LoongArch CPU 频率更低。

#### 加速比

![加速比](../logs/jvm/bench_speedup.png)

| 测试项                     | RISC-V 加速比 | LoongArch 加速比 | 瓶颈说明                        |
| -------------------------- | ------------- | ---------------- | ------------------------------- |
| `alu_mix`（位运算）        | **53.7 倍**   | **48.9 倍**      | 解释器取指/译码/分发开销        |
| `branch_heavy`（分支密集） | **35.5 倍**   | **29.2 倍**      | 分支预测 + switch 跳转表失效    |
| `mul_lcg`（乘加）          | **17.9 倍**   | **15.8 倍**      | 整数 ALU                        |
| `array_rw`（数组读写）     | 4.9 倍        | 5.9 倍           | 内存 load/store（AOT 无法优化） |
| `static_call`（静态调用）  | 1.6 倍        | 1.5 倍           | 方法解析仍走解释路径            |
| `object_field`（对象字段） | 1.1 倍        | 1.1 倍           | `new` + `getfield` 被分配器主导 |
| `string_ldc`（字符串常量） | 1.2 倍        | 1.3 倍           | 每次 `ldc` 都调 `alloc_string`  |

#### 跨架构对比（AOT 模式）

![跨架构](../logs/jvm/bench_arch.png)

LoongArch64 AOT 在相同 QEMU 主机上比 RISC-V AOT 慢约 8 倍，反映的是代码生成后端质量与指令集特性差异，而非 AOT 本身问题。


核心源码（`classfile.cpp`、`heap.cpp`、`interp.cpp`）在标准库版和裸机版之间完全共享，仅外围 IO（`native.cpp`、`main.cpp`）和头文件路径不同。

---

#### 为什么选装载期 AOT（而不是 JIT）

1. **无预热**：每个方法在首次调用前已编译完成，嵌入式场景延迟可预测。
2. **无运行时编译器驻留内存**：翻译器本身极小（约 1KB），不占用持久化的 JIT 编译器堆。
3. **实现简单**：无 OSR、无去优化、无投机内联。单遍模板替换即可。
4. **双架构友好**：RISC-V 和 LoongArch 后端共用同一套翻译循环，仅 `emit_*` 函数不同。

---


### Lua：零改动移植 Lua 5.4

Lua 是 RmikuOS 的第五种用户态语言。与 GCN 移植（把 `#include <vector>` 替换为 `#include "my/stdcompat.h"`，算法代码零改动但头文件要换）不同，**Lua 5.4 的官方源码一行未改**——只是删掉了动态链接（`loadlib.c`）和环境系统（`loslib.c` 的 `os.execute`/`os.tmpname` 等）相关的几个文件，其余 30 余个 `.c` 文件原样编译。

#### 桥接层：lcompat

Lua 5.4 的 C 源码依赖一组标准库头文件。RmikuOS 在 `user/include/` 下提供了一组同名裸运行时头文件，把 Lua 的标准库调用桥接到 RmikuOS 的 syscall ABI：

```text
Lua 源码 #include <stdio.h>    →  user/include/stdio.h   (vfprintf/vsnprintf/printf/fopen...)
Lua 源码 #include <stdlib.h>   →  user/include/stdlib.h  (malloc/free/realloc/strtol/exit...)
Lua 源码 #include <math.h>     →  user/include/math.h    (exp/log/sqrt/sin/cos... 裸运行时)
Lua 源码 #include <setjmp.h>   →  user/include/setjmp.h  (双架构 naked 汇编)
Lua 源码 #include <string.h>   →  user/include/string.h  (memcpy/memset 在 lib/string.c)
...
```

桥接的关键约束：

* **`setjmp`/`longjmp`**：Lua 用它做 `pcall` 错误传播和协程切换，是 `no_std` 环境下最棘手的依赖。RmikuOS 用 `__attribute__((naked))` 函数手写了两架构的汇编实现（riscv64 存 `s0-s11`+`sp`+`ra`，loongarch64 存 `ra`+`sp`+`fp`+`s0-s8`），无外部依赖。
* **`realloc` 与 `malloc_payload_size`**：Lua 的 `l_alloc` 默认调 `realloc`，而 `realloc` 需要知道旧块大小才能正确拷贝。RmikuOS 在 `mem.h` 加了 `malloc_payload_size()`，从 slab header（`SLAB_MAGIC | sc`）或大对象 header（`block->size`）查询实际分配大小，`realloc` 用 `min(old, new)` 限制 `memcpy` 长度，杜绝越界读。
* **`vsnprintf` 的 `%g`**：Lua 的 `print` 对浮点数用 `%.14g`。RmikuOS 的 `vsnprintf` 补全了 `%g`/`%f`/`%e` case，并在开头检查 NaN/infinity（`v != v` 判 NaN，`v > 1e308` 判 inf），避免 `math.huge`（无穷大）触发 `while (m >= 10.0)` 死循环。

#### 删除的文件

```text
loadlib.c        动态链接库加载（dlopen/dlsym）
loslib.c 部分    os.execute / os.exit / os.tmpname / os.getenv（环境系统）
liolib.c 部分    临时文件、popen（管道进程）
```

保留的库：`base` / `string` / `table` / `math` / `coroutine` / `io`（文件读写接 VFS）/ `os`（time/clock/date）。

#### 移植过程中踩出的四个 bug

移植过程中踩出的四个 bug，每一个都不是 Lua 的问题，而是 RmikuOS 基础设施的缺陷被 Lua 暴露：

1. **链接脚本 `.data` 权限**：`linker-riscv64.ld` 把 `*(.data)` 塞进了 `.text` 输出 section，导致 `.data` 段继承 `R E`（只读+可执行）权限。`stdio.h` 引入的 `_stdout` FILE 全局变量落在只读页，`__init_stdout` 写它 → store page fault。修复：`.data` 独立 section + `ALIGN(0x1000)`，两个架构都改。
2. **`vsnprintf` 缺 `%g`**：`vfprintf` 有 `%g` case 但 `vsnprintf` 没有，Lua 的 `print` 走 `snprintf` → 浮点数输出 `%g` 字面量。修复：补全浮点格式化 case。
3. **`realloc` 越界读**：`realloc` 的 `memcpy(np, p, s)` 用新大小 `s` 拷贝，扩大时越界读旧块后的内存，读到 mmap 区未映射页 → `memcpy` 里 load page fault。修复：`malloc_payload_size` + `min(old, new)`。
4. **`%g` 对 infinity 死循环**：`math.huge`（`1.0/0.0`）传入 `%g` 的 `%e` 路径，`while (m >= 10.0) { m /= 10.0; }` 对 `INFINITY` 死循环。修复：开头加 NaN/inf 检查。

四个 bug 的定位过程也是一次 trap 诊断方法的实战：`stval` 直接指向出问题的虚拟地址，`sepc` + `objdump -d` 反汇编定位到 `memcpy`，`readelf -l` 看段权限暴露链接脚本问题。

#### 测试验证

9 个递进测试脚本覆盖 Lua 5.4 核心特性：

```text
01_hello       print / 整数浮点算术 / 字符串拼接
02_types       type / math.type / ipairs+pairs / table 库
03_closure     upvalue 捕获与共享 / 递归 / 尾调用（10000 层不爆栈）/ vararg
04_control     if/while/repeat/for / 自定义迭代器 / break
05_math        exp/log/sqrt/sin/cos/random / sin²+cos²=1 / math.huge=inf
06_string      format/gsub/gmatch/match / 模式捕获 / 字符码
07_pcall       error/pcall/assert/嵌套/level       ← setjmp/longjmp 压测
08_coroutine   resume/yield/wrap/深层 yield/协程内 error  ← 协程切换压测
09_metatable   运算符重载/继承/__index/__call/proxy
```

`pcall` 和 `coroutine` 全部通过，证明手写的 `setjmp/longjmp` 不仅支持错误传播，还支持协程的跨栈帧切换——这是 Lua 5.4 最考验实现的特性。

#### 与其他语言移植的对比

| 语言      | 移植方式             | 源码改动                                             | 桥接层                                  |
| --------- | -------------------- | ---------------------------------------------------- | --------------------------------------- |
| C         | 原生                 | 无                                                   | 无（直接用 user 库）                    |
| C++ (GCN) | 头文件替换           | 算法零改动，`#include <vector>` → `"my/stdcompat.h"` | `stdcompat.h`                           |
| Rust      | 原生 no_std          | 无                                                   | `ulib` crate                            |
| Java      | 自研 JVM             | 无（javac 编译）                                     | 手写 JVM + `Rmiku.*` native             |
| **Lua**   | **官方源码原样编译** | **零改动**                                           | **lcompat（同名头文件 + setjmp 汇编）** |

Lua 移植是唯一一种"官方源码一行不改、只提供桥接头文件"的方式——这得益于 Lua 5.4 源码的干净设计（纯 C89、无平台 ifdef、所有系统依赖都经 `luaconf.h` 的 `luai_*` 宏集中配置）。



### Scheme：从零手写的微型 Lisp（TCO + 标记清除 GC）

Scheme 是 RmikuOS 的第六种用户态语言——**从零手写**（`user/c/scheme/scheme.c`，约 850 行 C），不移植任何现成实现。语言本体：S 表达式读取器（`'` 糖、`;` 注释、字符串、多行括号补全）、环境链与闭包、特殊形式（`quote if define lambda begin set! cond let and or`）、内建（算术/比较/列表/谓词/IO）、交互 REPL + 文件模式。两个"正经语言"的标配：

**① 尾调用优化（TCO）**——Scheme 的灵魂。`eval` 用主循环而非纯递归：尾位置的 lambda 调用直接 `e=body; env=new; continue` 迭代，**不压栈**。关键在 **tail 标志从特殊形式传播**：`if`/`cond`/`let`/`begin`/`and`/`or` 的尾分支都要把 tail 置 1，否则尾表达式会被当"非尾"求值、重新递归——100 万层直接爆 64KB 用户栈。修复后 `(loop 1000000 0)` 平稳跑完。

**② 标记-清除 GC（mark-sweep）**——6MB 用户堆逼出来的。无 GC 时 TCO 循环每轮泄漏 Env + cons，100 万次 = 300MB → `malloc` 返回 NULL → 空指针崩。GC 根集：全局环境 + eval 主循环活跃（表达式/环境）+ REPL 当前输入；每 2 万次分配在**主循环顶部**触发（递归子调用期间不触发，避免扫描未完成的中间值）；sweep 回收未标记的 Obj/Env，符号字符串归 intern 表持有不回收。

```text
/programs/scheme /codes/scm_tco.scm    # sum 1..1000000 = 500000500000（TCO + GC 同框）
/programs/scheme /codes/scm_fib.scm    # fib(20)=6765 + cond 分级 + let
/programs/scheme /codes/scm_hello.scm  # Hello, RmikuScheme! + 闭包加法器
/programs/scheme                       # 交互 REPL
```

踩坑实录（跨语言移植的通用教训）：C 的字符串字面量指针 ≠ intern 表指针——特殊形式必须**预 intern 后按指针比较**（Python 验证器靠语言级字符串 intern 掩盖了这个差异）；TCO 主循环里嵌 `for`（cond 子句）时 `continue` 属于内层循环——要用 `matched + break` 跳出；无 GC 的深循环在受限堆上必然耗尽——"教学 OS 无 GC"只适合短程序。

---

### SQLite 3.50：交互式数据库 shell（官方 amalgamation 零改动）

SQLite 是 RmikuOS 的嵌入式数据库：官方 `sqlite3.c` + `shell.c` amalgamation **原样编译、一行未改**，在 QEMU 里跑出完整的交互式 `sqlite>` shell，数据通过自定义 VFS 真实落盘到 FAT。

#### 自定义 VFS（SQLITE_OS_OTHER）

SQLite 官方的 `os_unix.c` / `os_win.c` 依赖 pthread / dlfcn 等宿主机制，因此用 `-DSQLITE_OS_OTHER=1` 整体关掉，改为 RmikuOS 自己的 VFS（`user/sqlite3/rmiku_vfs.h`）：

```text
sqlite3_file  ←→  RmikuFile{ fd, path }        把 sqlite 文件对象挂到 RmikuOS fd
xRead / xWrite / xTruncate / xSync            全部走真实 syscall（lseek + read/write + ftruncate + fsync）
xLock / xUnlock                               单进程 no-op
xRandomness                                   自带 LCG
journal/wal 探测                              -journal/-wal/-shm 后缀短路, 不触发无谓 stat
```

日志用 MEMORY 模式（`PRAGMA journal_mode=MEMORY`），主数据库文件走真实写盘——`xWrite → write syscall → FAT 驱动`，所以关掉重开数据库仍能读到数据。

#### 编译宏裁剪（user/build.py）

```text
-SQLITE_OS_OTHER=1           不编 os_unix/os_win, 用 rmiku VFS
-SQLITE_THREADSAFE=0         关线程/互斥, 避免 pthread.h
-SQLITE_OMIT_LOAD_EXTENSION  关 dlopen/dlsym, 避免 dlfcn.h
-SQLITE_OMIT_DEPRECATED      去掉废弃接口
-SQLITE_OMIT_DATETIME_FUNCS  关 date.c 的宿主时间函数
-SQLITE_OMIT_POPEN           无管道, .import "|cmd" 报错提示
-SQLITE_NOHAVE_SYSTEM        无 /bin/sh, .shell/.system/edit() 不编入
```

#### libc 补齐：POSIX 头 + shim

shell.c 是标准 POSIX 程序，依赖一组 RmikuOS 之前没有的 libc 设施，全部补齐：

* **POSIX 头**（`user/include/`）：`sys/stat.h`、`sys/types.h`、`sys/time.h`、`sys/resource.h`、`unistd.h`、`dirent.h`、`pwd.h`、`fcntl.h`、`limits.h`、`memory.h`、`utime.h`
* **stat 统一（方案 A）**：`fs.h` 成为用户态唯一 `struct stat`（POSIX `st_*` 字段 + 内部翻译内核 32 字节布局，类型位并入 `st_mode`），`sys/stat.h` 等头退化为薄壳——不再有"内核版 vs POSIX 版"双结构
* **shim 实现**（`rmiku_shims.c`）：`isatty` / `access` / `strdup` / `opendir`/`readdir`/`closedir`（基于 SYS_GETDENTS 真实现）/ `getpwuid` / `symlink` / `readlink`
* **file.h 增强**：FILE 池槽位回收（不再"开 8 次就满"）、`fseek`/`ftell`/`rewind`/`ungetc`（基于真实 lseek）、`sscanf`/`setvbuf`/`fileno`
* **终端回显**：内核无终端驱动，libc 在 stdin 读取层做"行模式 + 回显"（仅字符设备回显、管道不回显），sqlite3/lua 等交互程序获得正常终端体验

#### 使用

```text
/programs/sqlite3                   裸跑 → 交互式 sqlite> shell（内存库）
/programs/sqlite3 /fat/test.db      带库文件 → 落盘数据库
/programs/sqlite3_probe             落盘探针（建表/插入/关掉重开验证）
```

交互会话：

```text
sqlite> .tables
sqlite> CREATE TABLE t(id INTEGER PRIMARY KEY, name TEXT);
sqlite> INSERT INTO t(name) VALUES('miku');
sqlite> SELECT * FROM t;
1|miku
sqlite> .schema
sqlite> .quit
```

#### 与其他移植的对比

| 语言/应用 | 移植方式             | 源码改动 |
| --------- | -------------------- | -------- |
| C         | 原生                 | 无       |
| C++ (GCN) | 头文件替换           | 算法零改动 |
| Rust      | 原生 no_std          | 无       |
| Java      | 自研 JVM             | 无       |
| Lua       | 官方源码原样编译     | 零改动   |
| **SQLite**| **amalgamation 原样编译 + 自定义 VFS** | **零改动** |

SQLite 是 RmikuOS 上最大的第三方移植（`sqlite3.c` 单文件约 9.5MB 源码），验证了"官方源码不动、靠 libc 头文件 + VFS 桥接"的移植路线在数据库这类重型依赖场景同样成立。

### 统一构建

构建脚本 `user/build.py` 按来源与语言分派编译，一条 `./run.sh <arch>` 即可全部编好并打包进镜像：

```text
来源                          语言/方式            装入
─────────────────────────────────────────────────────
user/src/*.c                  C(系统)              /bin
user/tests/*.c                C(测试)              /tests
user/tests/*.cpp              C++(测试)            /tests
user/tests/*.rs               单文件 Rust(rustc)   /tests
user/rust/programs/*          cargo Rust(ulib)    /programs
user/c/*                      C(gcc)              /programs
user/cpp/*                    C++(g++)            /programs
user/gcn/*.cpp                C++(GCN)            /gcn
user/java/*.java              Java(javac)         /jvm
user/sqlite3/                 SQLite(自定义VFS)   /programs(sqlite3 / sqlite3_probe)
```

---

### Process&Thread Map

![process&thread map](../docs/images/process.png)

### 彩蛋：Brainfuck 解释器（8 指令的抽象语言）

凑数的抽象语言彩蛋——**不算第六种用户态语言**（真正支持的语言是上面五个），只是一个 8 指令的图灵完备玩具解释器（`/programs/brainfuck`）：

```
/programs/brainfuck /codes/hello.bf     # Hello World!
/programs/brainfuck /codes/mult.bf      # d（10x10 循环乘法）
/programs/brainfuck /codes/echo.bf      # 读 4 个字符原样回显（测 ',' 指令）
```

实现要点：磁带 `malloc(30000)`、**括号配对预处理 jump 表**（一遍扫描算出每个 `[` 的匹配 `]`，执行时 O(1) 跳转）、纯用户态零新头文件（只依赖 `user.h`）。`/codes/` 里带 3 个示例程序。踩过的一个雷：RmikuOS 用户栈只有 64KB，源缓冲必须上堆（64KB 局部数组会撑爆栈）。

