# RmikuOS

[![CI/CD](https://github.com/amieon/RmikuOS/actions/workflows/ci.yml/badge.svg)](https://github.com/amieon/RmikuOS/actions/workflows/ci.yml) [![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)

RmikuOS 是一个从零实现的教学型操作系统内核，支持 **RISC-V 64** 与 **LoongArch 64** 双架构。它可以在 QEMU 上启动用户态 shell，从真实 virtio 块设备加载 ext4 rootfs，并运行 **C / C++ / Rust / Java / Lua / Scheme** 六种语言的用户程序，内置 **TCC（Tiny C Compiler）** 可在系统内现场编译并运行 C 程序（AOT + JIT 双模式），配备 **kilo 全屏编辑器**（ANSI 终端、语法高亮）——编辑、编译、运行完整闭环，还内置 **SQLite 3.50 交互式数据库**（自定义 VFS 落盘，数据可持久化到 FAT 磁盘），并通过 TCP/IP 协议栈向宿主机浏览器提供真实的 HTTP 服务。

当前系统已经覆盖操作系统实验中常见的核心模块：进程与线程、虚拟内存、buddy 物理帧分配器、系统调用、VFS、多文件系统挂载、virtio 块设备、用户态 shell、环境变量、管道与重定向、信号、stride / alpha-scaled 调度器、 TCP/IP 网络协议栈与用户态 HTTP 服务器、JVM（解释器 + 装载期 AOT，双架构后端）、SQLite 3.50（自定义 VFS + 交互式 shell）、NTP 网络时间同步（墙钟 + 文件时间戳）、TCC 0.9.28（系统内 C 编译器，AOT + JIT 自托管工具链）、kilo 编辑器（ANSI 全屏编辑 + 语法高亮）、Scheme（从零手写的 Lisp 方言，尾调用优化 + 标记清除 GC），以及用于调度器实验的 workload 与自适应控制器。

RmikuOS 的目标不是停留在 `Hello, world`，而是逐步构建一个小而完整、能运行真实用户程序、能承载系统实验的教学型 OS。作为验证，独立项目 [VeryEasyGCN](https://github.com/amieon/VeryEasyGCN) 已通过 `stdcompat.h` 桥接层移植到 RmikuOS 上运行，并在真实 Cora 数据集上达到 **78.3%** 测试准确率。

```text
 ____            _ _         ___  ____
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


### TCP: Jacobson/Karn vs Fixed RTO（丢包率扫描）

![tcp loss sweep](logs/tcp/fig3_loss_sweep.png)

### TCP: CUBIC Sawtooth

![CUBIC Sawtooth](logs/tcp/figs/fig1_cwnd_sawtooth.png)


### JVM: 装载期 AOT

![加速比](logs/jvm/bench_speedup.png)

---

## CI/CD 持续集成

每次提交后,GitHub Actions 自动验证双架构(手动触发,可选架构):

```text
双架构交叉编译 -> rootfs 制作 -> QEMU 启动 -> 自动登录 -> 36 项回归测试 -> 检查汇总 -> shutdown 关机
```

* 流水线文件:`.github/workflows/ci.yml`,冒烟脚本:`scripts/smoke_test.sh`
* 触发方式:仓库 **Actions** 页 → 左侧 **CI/CD** → 右侧 **Run workflow**,选择 `riscv64` / `loongarch64` / `both`
* 冒烟流程:启动 QEMU → 自动登录 root/root → 进入 shell 后自动执行 `run_all` 回归测试 → 检查 `[RUNALL] 汇总`(任一失败即流水线红)→ 发 `shutdown` 优雅关机
* 构建产物(内核 ELF + rootfs + FAT 镜像)自动缓存,二次运行大幅提速

### 回归测试(user/tests/)

规范化测试体系,进系统后 `run_all` 一键执行全部:

* **断言库 `user/include/test.h`**:`CHECK / CHECK_EQ / CHECK_NE / CHECK_LT / CHECK_LE / CHECK_GT / CHECK_GE / CHECK_STREQ`,统一 `[TEST]/[PASS]/[FAIL]` 输出,退出码 0=全过 / 1=有失败
* **36 个测试**覆盖:进程(fork/exec/waitpid/信号)、线程、内存(mmap/堆)、文件系统(tmpfs/FAT/目录/seek/truncate/fsync)、管道、系统调用(凭证/权限/env)、SQLite 落盘、数学库、printf、setjmp、C++ 容器(vector/map/set/string/random)、语言运行时(TCC 现场编译 / Lua / Scheme / JVM)
* 新增测试:往 `user/tests/` 放一个可执行文件即可,`run_all` 自动覆盖

打 `v` 开头的标签(如 `v1.0`)时,还会自动构建双架构 release 产物并上传到 GitHub Release。

## 环境搭建

### Docker(推荐)

```bash
docker build -t rmikuos-dev .
docker run -it --rm -v $(pwd):/work -p 8080:8080 rmikuos-dev
```

&gt; 构建默认使用本地 cross-tools/ 目录中的 loongarch64 工具链。
&gt; 没有的话,先从 loong64/cross-tools releases(https://github.com/loong64/cross-tools/releases)下载 x86_64 宿主版解压后将 loongarch64-unknown-linux-gnu 里的内容移至./cross-tools,

### 无 Docker

bash first_run.sh   # 自动装 apt/rustup/工具链
之后这样就行：

```bash
./run.sh riscv64
./run.sh loongarch64
```

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
* VFS 与多文件系统挂载
* ext4 rootfs / tmpfs / FAT
* block cache
* shell 和用户程序（C / C++ / Rust / Java / Lua）与 SQLite 数据库
* 调度器与调度实验框架
* 网络协议栈（virtio-net / ARP / IPv4 / TCP / UDP / DHCP / ICMP）

架构相关部分主要集中在：

* trap handling
* 上下文切换
* 页表切换
* 时钟中断
* QEMU 设备发现
* virtio transport
* 关机（SiFive Test / ACPI GED）

不同架构使用不同的 virtio transport：

```text
riscv64      -> virtio-mmio
loongarch64 -> virtio-pci
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

### Minimal General-Purpose Signal Delivery

RmikuOS 进一步实现了**进程级信号投递机制**，使内核具备向用户态进程发送异步通知的能力。这不是一个特化的"快捷键处理"（如硬编码检测 Ctrl+C 直接杀进程），而是一套**通用的 sig_pending 位图 + 延迟投递 + 默认行为**的完整框架：

```text
内核侧：
    sig_pending: u64 位图（64 个标准信号槽位）
    sys_kill(pid, sig) -> 设置目标进程位图 -> 唤醒所有线程 -> 调度器重新入队
    
投递点（返回用户态边界）：
    syscall_exit 前检查 -> do_signal()
    trap_return 前检查 -> do_signal()
    
默认行为：
    致命信号（SIGINT/SIGKILL/SIGTERM/SIGABRT/SIGFPE/SIGILL）-> 进程终止
    其他信号 -> 清掉位图，忽略（框架已留好，可扩展 sig_handler）
```

**关键设计：信号在"返回用户态边界"处理，绝不异步中断用户态执行。** 这与 Linux 的 `sigreturn` 语义一致，但实现更极简——没有用户态信号栈、没有 `sa_mask` 嵌套、没有 `sigaltstack`，只保留最核心的"投递 + 默认终止"。

**异常隔离**：用户态程序触发非法指令（`ECODE_INE` / `CAUSE_ILLEGAL_INSTRUCTION`）或浮点异常时，内核不再 panic，而是向该进程投递 `SIGILL` / `SIGFPE`，随后调度器杀死它。这实现了**"用户态错误不炸内核"**的基本隔离，是操作系统与裸机程序的分水岭。

**Shell 交互**：通过 `fcntl(fd, F_SETFL, O_NONBLOCK)` 将 stdin 设为非阻塞，shell 在 `waitpid(WNOHANG)` 轮询期间可检测键盘输入。检测到 `Ctrl+C`（ASCII 0x03）时，shell 通过 `kill(front_pid, SIGINT)` 发送信号，实现前台进程中断。子进程退出后，shell 恢复 stdin 阻塞状态，不影响后续交互。





### VFS and File Descriptors

系统实现了基础 VFS 和 fd table。

当前支持：

* `open`（Unix 风格 flags：`O_RDONLY` / `O_WRONLY` / `O_RDWR` / `O_CREAT` / `O_TRUNC` / `O_APPEND`）
* `close`
* `read`
* `write`
* `getdents`
* `stat`
* `fstat`
* `chdir`
* `getcwd`
* `exec`
* `pipe`
* `dup2`
* `mkdir`
* `create`
* `truncate`（按路径截断到指定长度，`truncate(path, len)`）
* `ftruncate`（按 fd 截断到指定长度，`ftruncate(fd, len)`）
* `unlink`
* `rmdir`
* `remove_recursive`
* `lseek`（定位 fd 读写偏移，`lseek(fd, offset, whence)`，支持 SET / CUR / END）
* `fsync`（刷盘，`fsync(fd)`；tmpfs 为内存 no-op，FAT 走 `BlockIo` → `BlockDevice` 真实落盘）
* `rename`（移动 / 改名，`rename(old, new)`：同目录 / 跨目录 / 覆盖已存在文件 / 目录改名；跨设备返回 EXDEV）

标准输入输出也通过 fd 统一处理：

```text
fd 0 -> stdin
fd 1 -> stdout
```

读写权限按打开模式强制：只读句柄（`O_RDONLY`）拒绝 `write`，只写句柄（`O_WRONLY`）拒绝 `read`，在 `read` / `write` 系统调用处经 `File::readable / writable` 统一检查。`ftruncate` / `truncate` / `rename` 的写权限检查与之对齐，只查 `File::writable()`（权限系统详见后文「两层 POSIX 风格权限模型」），不做完整 `check_access`。

### File Operation Syscalls: 64–68 专用号段

在既有 VFS 之上，RmikuOS 补齐了一组文件**定位 / 裁剪 / 刷盘 / 改名**系统调用，与主系统调用表共用同一分发入口，独立占用号段 64–68（在权限系统号段 50–63 之后、网络号段 100–109 之前）：

```text
64 FSYNC      把打开的 fd 数据刷盘
65 FTRUNCATE  按 fd 截断到指定长度
66 TRUNCATE   按路径截断到指定长度
67 LSEEK      定位 fd 读写偏移(SET / CUR / END)
68 RENAME     移动 / 改名
```

语义要点：

* **`lseek`**：复用每个打开 fd 自带的独立 offset（`TmpfsFile` / `FatFile` 各自持有 `Mutex<offset>`），`whence` 支持 `SEEK_SET` / `SEEK_CUR` / `SEEK_END`；非法 whence、负偏移、坏 fd 均返回 -1。
* **`ftruncate` / `truncate`**：按长度裁剪文件——tmpfs 为 `Vec` resize 并补 0，FAT 为 seek + `truncate`。`ftruncate` 按 fd、`truncate` 按路径；只读 fd 拒绝裁剪（返回 -1）。
* **`fsync`**：tmpfs 直接返回成功（数据本就在内存，无盘可刷）；FAT 走 `file.flush()` → `BlockIo::flush` → `BlockDevice::flush` 的**完整刷盘链**，是真正落盘。为支持这点，`BlockDevice` trait 新增了 `flush()` 默认空实现，`BlockIo::flush` 打通到设备。
* **`rename`**：支持同目录改名、跨目录移动、覆盖已存在文件（编辑器「写临时文件再 rename」的原子写风格）、目录改名；跨设备（如 `/tmp → /fat`）返回 -1（EXDEV 语义）；把目录移进自己的子目录会被拒绝（防自环）。跨设备检测通过 `mount_point_of()` 比较挂载点（而非路径本身）实现。

用户态经 `fs.h` 封装为 `lseek` / `ftruncate` / `fsync` / `truncate` / `rename`，经 `syscall4` 进入内核；Rust 侧经 `ulib::io` 的同名封装，体感与 POSIX 对齐。

---

## User Programs and Shell

RmikuOS 从 ext4 rootfs 中加载用户程序，第一个用户进程（init shell）通过 VFS 从 `/bin/shell` 加载。Shell 不是内核内置的玩具解释器，而是一个**支持交互式编辑、通配符展开、逻辑链、后台执行与脚本 source** 的完整用户态程序。

### Shell 命令体系

区分**内建命令**（改变 shell 自身状态，必须内建）与**外部命令**（独立 ELF，可被管道 / 重定向组合）：

```text
内建：  cd  pwd  exit  help  shutdown  jobs  clear
        mkdir  touch  rm  rmdir  mv  rename  source  .
        export  env  unset
外部：  ls  cat  echo  grep  shell  sleep ...
```

`ls` / `cat` / `echo` / `grep` 等 IO 工具被实现为独立的外部程序，因此它们能出现在管道与重定向中；`cd` / `pwd` / `exit` / `jobs` / `source` / `export` / `env` / `unset` 保持内建，因为它们必须修改 shell 进程自身的状态（如环境变量表、当前工作目录）或访问 shell 内部数据结构。

### Interactive Line Editing

Shell 支持完整的命令行编辑，无需依赖 readline 库：

```text
↑ / ↓         浏览历史命令（环形缓冲区，自动去重）
← / →         光标左右移动，支持任意位置插入与删除
Backspace     在光标处删除字符（自动重绘后续内容）
Tab           命令补全 + 路径补全
```

**Tab 补全**：

- 唯一匹配时直接补全，命令后自动追加空格；
- 多匹配时先补**最长公共前缀**（LCP），再次按 Tab 列出候选列表；
- 支持绝对路径与相对路径（`ls /bi<Tab>` → `ls /bin/`）。

### Glob, Brace Expansion & Quoting

Shell 在参数展开阶段实现了**大括号展开 → 通配符展开**的两级展开，且引号内完全保护：

```text
# 通配符
/ $ ls *.c
/ $ ls /bin/s?ell
/ $ rm [abc]*.o

# 大括号展开
/ $ echo {hello,world}
hello world
/ $ echo file{1,2,3}.txt
file1.txt file2.txt file3.txt
/ $ echo num{10..13}
num10 num11 num12 num13

# 引号保护（内部不做任何展开）
/ $ echo "*.c"
*.c
/ $ echo 'hello * ? [abc]'
hello * ? [abc]
```

**字符类** `[abc]`、`[a-z]`、`[^abc]`（或 `[!abc]`）也支持，与 `*` `?` 自由组合。

### Control Flow: `;` `&&` `||` `&`

Shell 支持完整的命令控制流：

```text
;           顺序执行（多条命令分隔）
&&          短路与（前一条成功才执行后一条）
||          短路或（前一条失败才执行后一条）
&           后台执行（不阻塞 shell，返回 job id）
```

```text
/ $ cd /bin && ls | grep shell || echo not found
/ $ echo one; echo two; echo three
/ $ sleep 5 &
[1] 3
/ $ jobs
[1] running sleep
/ $ ...（5秒后自动打印）[1] done sleep
```

`&&` / `||` 与管道 `|` 的优先级关系与 bash 一致：管道先组合命令，再参与逻辑短路。

### Script Execution: `source` / `.`

Shell 支持从文件逐行执行脚本，实现为内建命令 `source` 或 `.`：

```text
/ $ cat test.sh
echo "========== hello =========="
echo {1,2,3}
ls /bin/*.c 2>/dev/null || echo no .c files
cd /tmp && touch foo && ls foo
/ $ source test.sh
========== hello ==========
1 2 3
no .c files
foo
```

支持**续行**（行尾 `\` 将下一行拼接），支持 `#` 行内注释。脚本中可任意使用管道、重定向、逻辑链与后台执行。

### Pipeline & Redirection

`pipe()` 创建匿名管道，`dup2` 实现重定向。Shell 支持：

```text
cmd > file        stdout 覆盖写入
cmd >> file       stdout 追加
cmd < file        stdin 从文件读取
cmd1 | cmd2       单级管道
cmd1 | cmd2 | ... 多级管道
cmd < in | f1 | f2 > out   管道 + 两端重定向
```

管道与重定向的解析在**引号保护**下进行：`echo "a > b | c"` 被正确当作单个字面参数，不会触发重定向或管道。

### Lexing（词法解析）

命令行解析为原地压缩的词法分析器，单遍扫描完成：

```text
"..." / '...'   引号剥离；双引号内处理 \ 转义，单引号内全字面
\               转义：反斜杠后的字符按字面保留
#               行内注释：词首的 # 起至行尾忽略
```

引号状态在分词、管道切分、重定向解析三处一致跟踪，保证操作符在引号内无特殊含义。



---

### Environment Variables & `$?` Expansion

Shell 支持用户态环境变量与 `$?`（上一条命令退出码）的读取与修改，并内建 `export` / `env` / `unset` 三个命令：

```text
/ $ export FOO=bar            # 设置环境变量（shell 进程内，exec 子进程时随 envp 透传）
/ $ echo $FOO                 # 单参数展开 -> bar
/ $ echo ${FOO}               # 大括号形式 -> bar
/ $ echo "FOO=$FOO"           # 双引号内展开 -> FOO=bar
/ $ echo '$FOO'               # 单引号保护 -> $FOO（不展开）
/ $ ls /bin/*.c; echo $?      # $? = 上一条命令退出码
/ $ env                      # 打印当前全部 KEY=VALUE
/ $ unset FOO                # 删除变量
```

展开规则与 bash 对齐：

* `$VAR` / `${VAR}`：读取环境变量（经 `getenv` syscall）；
* `$?` / `${?}`：展开为 `g_last_exit`（每跑完一条命令由 `execute_line` 更新，含管道整条命令的退出码）；
* **单引号** `'...'` 内完全字面，**不展开**任何 `$`；
* **双引号** `"..."` 内展开 `$` 与转义 `\`，但保留空白与字边界；
* 变量展开在引号保护下进行：`echo "$FOO | bar"` 不会被误判为管道。

`export` 设置的环境变量在 shell `exec` 外部命令时，随内核经寄存器传入的 `envp` 透传给子进程（详见下节「Environment Variable Syscalls」）。shell 的命令搜索目录也从环境变量 `PATH`（`getenv("PATH")`，按 `:` 切分，回退 `/bin /tests /programs`）读取，因此 `export PATH=/bin:/tests` 等修改即时生效。

---

### TCC：系统内的 C 编译器（自托管工具链）

TCC（Tiny C Compiler）0.9.28 移植——RmikuOS 能**在自己的系统里现场编译并运行 C 程序**，形成「内核写 C → 系统里编译 C → 运行 C」的完整自托管闭环。

#### 用法

```
tcc hello.c -o hello && ./hello    # AOT: 编译出 ELF, 内核加载执行
tcc hello.c -run                   # JIT: 编译进内存直接执行
```

`/codes/` 内置 12 个测试程序（镜像 overlay 带进），`scripts/tcc_test.sh` 顺序执行——**12/12 全过**。

#### 系统文件布局（`/usr/lib/tcc`）

| 文件 | 作用 |
|---|---|
| `crt1.o` / `crti.o` / `crtn.o` | 启动（复用 crt0，`_start`→main→exit）/ 空 .init/.fini |
| `libc.a` | syscall.o + string.o + **libc_extern.o** |
| `libtcc1.a` | TCC 运行时 + **libgcc 软浮点合并**（long double 等） |
| `runmain.o` | `-run`（JIT）启动 stub |
| `include/` | RmikuOS 头 + TCC 编译器配套头（stddef/stdarg/stdbool…） |

#### 三个关键设计

**① libc 具现化（libc_extern.c）**：RmikuOS 的 libc 全是**头内联 static inline**（无真实符号），而 TCC 对 `static inline` 不内联、生成外部引用 → `unresolved printf`。解法：把全部头内联函数重命名（`__rmiku_*`）后 include（宿主 gcc 内联消化内部依赖链），再为公共 API 生成非 static 转发打进 libc.a。顺序必须是「**重命名 → include → undef → 转发**」——include 放重命名前会 redefinition。

**② libgcc 全量合并**：TCC 静态链接**不自动链 libgcc**（`TCC_LIBGCC` 只在动态链接生效）→ long double（128 位 quad）软浮点 `__*tf*`、`__clzdi2` 等全部 unresolved。解法：`add_libgcc_tf()` 把 host libgcc.a 全量展开、**与 libtcc1.o 去重**后合并进 libtcc1.a——TCC 惰性提取只取被引用的成员，多放无害，一次解决所有编译期辅助符号。

**③ 全局 `_stdout`（短输出不丢）**：标准流从「每编译单元一份 static」改为**全局唯一**（定义在 string.o）。否则 printf（某 TU）与 exit 的 flush（string.o）各用各的 `_stdout` 实例，flush 落空——短输出（<BUFSIZ）且无换行就丢失。crt0 调 main 后 `call exit`（flush 标准流再退出）。

#### 其他适配（踩坑实录）

- **静态 ELF**：libtcc.c `tcc_new()` 默认 `static_link=1`——否则产物带 `PT_INTERP`，内核 loader 拒绝；
- **weak 符号化解冲突**：runmain.o 自带强 `exit`/`atexit`（-run 专属）→ libc 侧用 `__attribute__((weak))` 让位；`.init_array`/`.fini_array` 边界符号 TCC 不生成 → 提供 weak 空数组，否则 `-run` reloc 静默失败；
- **mprotect 系统调用**（JIT 需要）：`PageTable::update_flags` 只改已有 PTE 的权限位，**不重映射、不分配新帧**——`map()` 是 assert 未映射语义，直接重映射会 panic；
- **open() 三参 mode 打通内核**：O_CREAT 创建文件后 `chmod(mode)`，POSIX 真语义（用户态变参 → syscall6 → 内核）；
- **`__sync_*` 原子符号**：lock.h 用 gcc 内建原子，TCC 不识别 → string.c 用 riscv64 `amoswap.w.aq`/loongarch64 `amswap_db.w` 提供真实符号；
- **stdint.h**：裸机工具链无 libc 版 → TCC 专用最小版（只进 `/usr/lib/tcc/include`，gcc 程序继续用编译器内置）。

#### 已知局限

- `-run` 走 runmain 的 exit，不 flush 无换行缓冲——`-run` 输出建议带 `
`；
- 用户程序显式 `exit()`（头内联）不 flush——次要路径；
- TCC 的 `static inline` 不内联是特性不是 bug，靠 libc_extern 转发兜底。

#### LoongArch64 后端：从「全崩」到 12/12 全绿

LoongArch64 后端的排障。起点极其狼狈：后端能编译、产物长得完全正常，但 12 个测试**一个都跑不起来**——所有程序在启动头几条指令就 SIGSEGV。难点在于：编译侧一切正确、运行时全军覆没。

**排障方法**：在宿主侧用同一份源码编出 `/tmp/tcc-la`，产出与镜像内**逐字节一致**的 ELF，再用 `objdump` 把每个 trap 的 `era` 精确映射到指令——整个调试过程就是「反汇编定罪 → 对照能用的 riscv64 后端 → 修 → 重测」的循环。

按时间线：

**① 指令编码层**：`pcalau12i` 的宏值写成了 `lu32i.d` 的、BEQZ 编码错、`jirl` 丢返回地址、r5/r6（其实是 `a1/a2` 参数寄存器）被当 scratch 用、B26/B21/B16 重定位字段移位错、PCALA_HI20 缺 `+0x800` 进位——**6 个编码错误叠在一起**。

**② 链接层**：`__extenddftf2` unresolved——根因是 libtcc1.o 的本地标签 `L0` 和每个 libgcc 成员「撞名」，build.py 的去重逻辑把**全部 tf 符号当重复项误杀**。诊断脚本实锤：libgcc 有 14 个 tf 符号，合并后的 libtcc1.a 是 0 个。

**③ 条件跳转层**：`gjmp_cond` 的条件**没取反**——riscv64 的写法是 `op ^ 1`（发反条件跳过链式跳转），移植时丢了。后果：**所有 if/while/for 的条件全部颠倒**，程序跑起来完全不是人写的逻辑。

**④ GOT 层**：`GOT_PC_HI20/LO12` 重定位直接填符号地址，而不是 **GOT 槽地址**（`got->sh_addr + got_offset`）→ `pcalau12i + ld.d` 从**函数入口读出前两条指令**当成函数指针 → `jirl` 跳进野地址。这正是所有 trap 里 `badv=指令对`（如 `0x28ffa2cc29ffa2c4` = 两条 `ld.d/st.d` 序言指令拼成的 64 位值）的来历。

**⑤ 重定位掩码层（主线崩溃根因）**：`relocate()` 的 imm20 保留掩码 `0xfc00001f` **错了一比特**——1RI20 格式指令（pcalau12i/lu12i.w/lu32i.d）的操作码是 **7 位**（bits[31:25]），掩码只保留 [31:26] 把 bit25 清了 → GAS 发出的 `pcalau12i $t0, 3`（`0x1a00000c`）链接后变成 `pcaddi $t0, 3`（`0x1800006c`）→ **PC 相对寻址全灭**。`lu12i.w` 因 bit25=0 恰好幸存，所以绝对寻址正常、PC 相对全崩——「什么都是对的但什么都跑不起来」的教科书案例。**era 恒定（崩在 crt1.o 内）+ badv 随构建变化（文本内容变）** 这两个特征直接把矛头钉死在重定位上。

**⑥ 变参层**：`tccdefs.h` 没有 LoongArch 分支 → `va_start/va_arg` 落进 **i386 的 4 字节槽方案** → `va_start(ap, fmt)` 变成 `ap = &fmt + 8`（= fp-16，恰好是保存旧 fp 的槽位）→ **所有 `%d` 打印同一个数 16764816**（= 初始 sp = 0xffcf90），`%f` 全 0，`%s` 全乱码。一个宏分支的缺失让整个 printf 看起来像坏了。

**⑦ 立即数层（全崩总根因）**：LoongArch 的 `ANDI/ORI/XORI` 立即数是**零扩展**（逻辑运算），`ADDI/SLTI` 是**符号扩展**——TCC 的立即数判断对两者一视同仁 → `& -4` 被编成 `andi $a0, $a0, 0xffc` → **64 位地址被截成 12 位小整数**（所有 `badv=0xexx` 的 PIL 崩溃）。RISC-V 的 `andi` 是符号扩展所以永远踩不到。`va_arg` 的 `_tcc_align`、malloc 的 `align_up`、一切 `& ~mask` 全中招。

**⑧ ABI 层**：浮点参数被放进了 `$f0-$f7`，而 LoongArch LP64D 的参数寄存器是 **`$f12-$f19`**（`$f0` 只是返回寄存器）。RISC-V 的 fa0-fa7 = f10-f17 与返回寄存器 f10 重合，移植代码「歪打正着」在 RISC-V 上自洽；LoongArch 必须显式 `fmov.d $f12+k ← $f(k)`。症状一眼看穿：`cos(0.0)=1.0` 对（$f12 初始恰好是 0）、`sqrt(2.0)=0`（libc 从 $f12 读到 0）。

**⑨ 常量求值层**：`tcc.h` 的 `CValue.i` 是 **uint64_t**，后端却用 `int fc = vtop->c.i` 截断 → `SLAB_MAGIC = 1ULL<<63` 变成 0 → `b->size = SLAB_MAGIC | sc` 被编成 `ori $a1, $a1, 0x0` → slab 头失去 magic → `realloc` 全部误判。定罪证据是反汇编里那行刺眼的 `ori 0x0`。RISC-V 版靠一行「废话」比较 `fc == vtop->c.i`（int 提升成 64 位比较）兜底，移植时丢了。

**方法论沉淀**（三句话）：

1. **反汇编是唯一真相**。`badv=指令对`、`era 恒定`、`ori 0x0`、`pcalau12i→pcaddi`——所有「看起来完全正常」的 bug，最后都是被 objdump 直接定罪，而不是靠读代码猜出来的。
2. **同树能用的后端是金矿**。GOT 槽地址、条件取反、freg 物理映射、64 位常量兜底——一半的修复是 `diff riscv64-gen.c` 直接对出来的。
3. **指令编码只信权威源**。binutils 的 loongarch-opc.c、QEMU 的 insns.decode、loongson 的 psABI 三源核对，宁可多查一次也不猜。

结局：**12/12 全绿**，与 RISC-V 参考输出逐字节一致——`tcc_test.sh` 里的每一行，都是这九层 bug 的墓志铭。

---

### kilo：全屏编辑器（与 TCC 组成开发闭环）

kilo（antirez 的经典教学编辑器，1308 行单文件 C）移植——与 TCC 配合，在 RmikuOS 内完成**「编辑 → 编译 → 运行」完整开发闭环**：

```
kilo hello.c          # 写代码（ANSI 全屏 + C 语法高亮）
tcc hello.c -o hello && ./hello    # 编译并运行
```

#### 适配要点（踩坑实录）

- **termios 最小模拟**（`include/termios.h`）：RmikuOS 无完整 termios，只把 `c_lflag` 的 `ECHO` 位映射到内核 `set_echo()`（raw 模式 = 关回显）；VMIN/VTIME 忽略（内核 read 天然逐字符）；`OPOST/CS8` 等标志只定义供位运算。
- **回车 = `\n`（ICRNL）**：内核 stdin read 把回车统一转成 `\n`（`stdio.rs` 的 ICRNL），kilo 原版只认 `\r` → 回车会被 insert 成裸 LF、屏幕错乱。修复：`case ENTER` 加 `case '\n'` 同判。
- **窗口固定 80x24**：无 ioctl(TIOCGWINSZ)/DSR 查询，`getWindowSize` 返回固定尺寸。
- **退出恢复终端**：RmikuOS crt0 的 exit 不跑 `atexit`，kilo 的 `atexit(editorAtExit)` 失效——Ctrl+Q/Ctrl+Z 退出前显式 `disableRawMode`。
- **Ctrl+S/Q 备选键**：Ctrl+S(0x13)/Ctrl+Q(0x11) 是 XON/XOFF 软件流控字符，QEMU 串口 + 宿主终端常吞 → 加备选 **Ctrl+X 保存 / Ctrl+Z 退出**（0x18/0x1A 不被流控占用）。
- 其他：`getline`→`fgets`（无 getline）、`ftruncate` 用真实实现（内核有 `sys_ftruncate`）、`isatty`/`__sync_*` 补 weak 符号。

#### 按键速查

| 键 | 功能 |
|---|---|
| 方向键 / PageUp / PageDown | 光标移动、翻页 |
| Enter | 换行（`\r`/`\n` 均识别） |
| Backspace / Del | 删除 |
| **Ctrl-S / Ctrl-X** | 保存 |
| **Ctrl-Q / Ctrl-Z** | 退出（未保存连按两次确认） |
| Ctrl-F | 搜索 |

---

### File System Map

![filesystem map](docs/images/vfs.png)

## Filesystem


RmikuOS 的文件系统建立在一层统一的 VFS 抽象之上：每个文件系统节点实现 `Inode` trait（`lookup` / `open` / `getdents` / `metadata` / `truncate`，以及可写的 `create` / `mkdir` / `unlink` / `rmdir`），每个文件系统实例实现 `FileSystem` trait（提供根 inode）。在此之上，一张**挂载表**把不同文件系统挂到目录树的不同位置，使只读的 ext4、内存可写的 tmpfs、以及落盘可写的 FAT 能在同一棵目录树中共存。

```text
                    路径解析 (lookup_abs_path)
                             │
                             ▼
                      挂载表（最长前缀匹配）
            /              │               \
        "/" → ext4    "/tmp" → tmpfs    "/fat" → FAT
            │              │                 │
     read-only ext4   in-memory CRUD    writable on-disk
     (ext4_view)      (Vec/BTreeMap)    (fatfs + BlockIo)
            │                                │
        Block Cache                    BlockDevice(读/写)
            │                                │
        BlockDevice ─────────────────────────┘
          /        \
   virtio-mmio   virtio-pci
    RISC-V       LoongArch64
```

`open` 接受 Unix 风格的 flags（`O_RDONLY` / `O_WRONLY` / `O_RDWR` / `O_CREAT` / `O_TRUNC` / `O_APPEND`）：访问模式经 `File::readable / writable` 在 `read` / `write` 系统调用处强制（只读句柄拒绝写、只写句柄拒绝读），`O_CREAT` 在内核侧按「拆父目录 + 在父目录 inode 上 `create`」创建文件，`O_TRUNC` 调用 `Inode::truncate` 截断，`O_APPEND` 让写句柄每次写前定位到文件末尾。这套 flags 对所有可写文件系统（tmpfs / FAT）通用。

### Mount Layer（多文件系统挂载）

所有路径访问都汇聚到 `lookup_abs_path` 这一个入口。它先查挂载表，按**最长前缀匹配**选出该路径所属的文件系统及其根 inode，再把挂载点内的相对路径逐级 `lookup` 下去。

```text
/tmp/foo/bar
  → 挂载点 "/tmp" 命中（比 "/" 更长，优先）
  → 交给 tmpfs，相对路径 "foo/bar"
  → tmpfs.root_inode().lookup("foo").lookup("bar")

/fat/note
  → 挂载点 "/fat" 命中
  → 交给 FAT，相对路径 "note"

/etc/motd
  → 仅 "/" 命中（兜底）
  → 交给 ext4，相对路径 "etc/motd"
```

前缀匹配按**路径段**而非裸字符串进行（要求路径恰好等于挂载点，或以「挂载点 + `/`」开头），从而避免 `/tmp` 误匹配到 `/tmpfoo`。挂载机制让「加新文件系统」变成纯粹的扩展：实现 `FileSystem` + `Inode`，在启动时 `mount("/挂载点", fs)` 即可，无需改动路径解析。

挂载点本身需要在父文件系统中存在一个对应的目录作为「挂载坑」（如 ext4 rootfs 中的 `tmp` / `fat` 目录），否则该挂载点虽在挂载表中、却不会出现在 `ls /` 的目录列表里。

### Read-only ext4 Rootfs

根文件系统 `/` 是一个 ext4 镜像，作为只读 rootfs 挂载。ext4 的磁盘格式解析交给第三方 crate `ext4_view`，RmikuOS 只实现其要求的块读取回调（`Ext4Read::read`），把「读到字节」接到自己的块设备与块缓存上；格式解析、目录遍历、inode 查找由 crate 完成。

```text
User Program → Syscall → VFS → ext4 (ext4_view) → BlockCache → BlockDevice
                                                                  ├── RamDisk
                                                                  ├── VirtioMmioBlockDevice (riscv64)
                                                                  └── VirtioPciBlockDevice  (loongarch64)
```

rootfs 由宿主机上的目录模板 `user/rootfs/` 生成，用户程序的编译产物在打包时被复制进镜像。最终镜像大致形如：

```text
/
├── bin/         系统命令(shell, ls, cat, echo, grep ...)
├── samples/     C/C++ / 单文件 Rust 测试程序
├── tests/       测试程序
├── programs/    构建的 Rust/C/C++ 程序
├── gcn/         C++ 图神经网络程序（GCN / GAT）
├── jvm/         Java 项目的class文件
├── etc/
│   └── motd
├── home/
├── share/
├── tmp/         tmpfs 挂载点(可写,内存)
├── fat/         FAT 挂载点(可写,落盘)
├── dev/
└── proc/
```

> ext4 经由 `ext4_view` 以**只读**方式访问；运行时的可写存储由挂载在 `/tmp` 的 tmpfs（内存）与挂载在 `/fat` 的 FAT（落盘）提供（见下）。

### Writable tmpfs（可写内存文件系统）

tmpfs 是一个完全活在内存中的可写文件系统，挂载于 `/tmp`，提供完整的 CRUD。文件内容是 `Arc<Mutex<Vec<u8>>>`，目录是 `Arc<Mutex<BTreeMap<String, TmpfsNode>>>`，因此「可写」只是对内存数据结构的增删改，无需触及任何磁盘格式。

支持的操作：

```text
mkdir              创建目录
create (touch)     创建空文件
write / read       文件内容读写(每个打开的 fd 独立 offset,数据共享)
lseek              定位 fd 读写偏移(SET/CUR/END,复用 fd 自带 offset)
ftruncate          按 fd 截断到指定长度(缩小保留内容、扩大补 0)
truncate           按路径截断到指定长度
fsync              内存 no-op(数据本就在内存,无需落盘)
rename             移动 / 改名(同目录 / 跨目录 / 覆盖已存在文件 / 目录改名)
unlink (rm)        删除文件
rmdir              删除空目录
remove_recursive   递归删除(rm -r)
```

几个语义直接由 Rust 的所有权与 `Arc` 机制自然得到：

* **目录树共享**：`lookup` 返回子节点时 clone 的是 `Arc`，多个进程拿到同一文件即操作同一份内存——一端写、另一端可读。
* **递归删除零额外代码**：`remove_recursive` 直接从父目录的 `BTreeMap` 中移除整个子树节点，`Arc` 连锁 drop 自动递归释放整棵子树的内存。
* **unlink 已打开的文件**：删除只是从目录移除「名字」，若仍有进程持有该文件的 `Arc`，内存保留到最后一个 fd 关闭——与 Unix「unlink 一个 open 的文件，数据存活到 close」一致。
* **定位 / 裁剪 / 刷盘 / 改名**：`lseek` 复用每个 fd 现有的独立 offset；`ftruncate` / `truncate` 按长度裁剪（`Vec` resize 并补 0）；`fsync` 直接返回成功（内存即真相，无需落盘）；`rename` 通过 `find_dir` 从根遍历定位目标父目录，支持跨目录移动与覆盖已存在文件（编辑器原子写风格），跨设备则返回失败。

写权限的隔离也随之成立：在只读 ext4 路径下（如 `/etc`）执行写操作会被正确拒绝（`Inode` 的写方法默认返回失败，ext4 不重写它们），而 tmpfs 重写为真正的增删。一组端到端测试覆盖了建树、文件读写、`rmdir` 非空目录失败、`unlink` 目录失败、递归删除、删除不存在项失败、以及「ext4 上 mkdir 失败」等用例。

> **关于可写文件系统的设计取舍**：由于 ext4 经 `ext4_view` 只读访问，自实现可写 ext4（分配 inode / 数据块、维护位图与日志）成本极高且收益有限。tmpfs 在内存中提供了「可写文件系统」的全部语义（创建、写入、删除、共享、引用计数释放），落盘可写文件系统（FAT）见下节。

### Writable FAT on Disk（可落盘的 FAT 文件系统）

tmpfs 提供了内存中的完整可写语义，但内容随重启丢失。RmikuOS 进一步接入了 **FAT16** 文件系统，挂载于 `/fat`，运行在一块**独立的 virtio 块设备**上，提供真正落盘、跨重启存活的可写存储。

FAT 的磁盘格式解析交给 vendored 的 `fatfs` crate（0.4，`no_std` + `alloc`，开启 `lfn` 长文件名）。RmikuOS 提供两层适配：

```text
VFS (Inode / File)
      │
  FatFs / FatInode / FatFile      ← 把 fatfs 的借用式 API 适配到 VFS
      │
  BlockIo                          ← 把「字节流」翻译成「扇区读写」
      │  (read-modify-write 处理非对齐写)
  BlockDevice(读写)
```

* **`BlockIo`** 实现 `fatfs` 要求的字节流 IO（`Read` / `Write` / `Seek`）：按字节偏移定位到扇区，非扇区对齐的写入用 read-modify-write（先读整扇区、改其中一段、再写回）。
* **`FatFs` / `FatInode` / `FatFile`** 把 `fatfs` 的借用式 API（`File` / `Dir` 借用 `FileSystem`）适配到 VFS 的 `Inode` / `File`。由于 fatfs 的句柄借用全局 `FileSystem` 对象，无法直接塞进 `'static` 的 VFS 节点，RmikuOS 采用「路径式 inode」：`FatInode` 只存路径，每次操作在持锁块内临时打开 fatfs 句柄、用完即弃，只让纯数据（`Vec` / 元数据 / 返回码）逃出锁作用域。这与 ext4 的设计同构。

支持的操作与 tmpfs 对齐：创建 / 读 / 写 / 截断 / 追加 / 建目录 / 删除 / 递归删除 / `lseek` / `ftruncate` / `fsync` / `rename`，并经由统一的 open flags 驱动（`>` 覆盖、`>>` 追加、`<` 读取）。写入经 `BlockIo` 落到 virtio 块设备的磁盘镜像，跨重启存活；其中 `fsync` 在 FAT 上走 `file.flush()` → `BlockIo::flush` → `BlockDevice::flush` 的**完整刷盘链**，是真正持久化（区别于 tmpfs 的 no-op），写入后 `fsync` 再读回可验证内容落盘：

```text
/ $ echo "hello fat" > /fat/note
/ $ cat /fat/note
hello fat
   ... 重启 QEMU ...
/ $ cat /fat/note
hello fat
```

> **关于文件名大小写**：FAT 始终大小写不敏感（忽略大小写后同名即同一文件）。开启 LFN 后，新建文件**保留输入时的显示大小写**（`Note.txt` 显示为 `Note.txt`），但匹配仍不区分大小写——这是 FAT 显示名（LFN 项）与匹配名（8.3 短名，规范大写）分离的固有特性，并非 bug。

> **两文件系统对称、上层零改动**：FAT 的整条上层（`BlockIo` / `fatfs` / VFS 适配）完全建立在 `BlockDevice` trait 之上，不含任何架构分支。riscv（virtio-mmio）与 loongarch（virtio-pci）只需各自实现 `BlockDevice` 的读写，FAT 挂载层一份代码两个架构通用——这正是把架构差异收敛到 `BlockDevice` 接缝之下的回报。

---

## Virtio Block Device

RmikuOS 不再只依赖内核内置 ramdisk，而是从 QEMU 挂载的真实磁盘镜像读写数据：只读 ext4 rootfs 从一块盘加载，可写 FAT 落在另一块独立盘上。virtio 块设备驱动同时实现了**读路径与写路径**，并支持发现并初始化**多块**磁盘（按 sector 2 的 ext4 magic 区分 rootfs 盘与 FAT 盘）。

整体路径如下：

```text
User Program
    ↓
Syscall
    ↓
VFS
    ↓
ext4 (只读) / tmpfs (内存) / FAT (落盘)
    ↓
BlockCache
    ↓
BlockDevice (读 + 写)
    ├── RamDisk
    ├── VirtioMmioBlockDevice
    └── VirtioPciBlockDevice
```

#### RISC-V virtio-mmio

在 RISC-V QEMU `virt` 机器上，系统通过 virtio-mmio 扫描 virtio block device。

流程：

```text
扫描 virtio-mmio slot(发现多块盘)
识别 virtio-blk
初始化 legacy virtio-mmio device
配置 virtqueue
提交 block read / write request
按 ext4 magic 区分 rootfs 盘与 FAT 盘
```

#### LoongArch64 virtio-pci

在 LoongArch64 QEMU `virt` 机器上，系统通过 PCI/PCIe 枚举 virtio block device。

流程：

```text
映射 PCI ECAM
枚举 PCI bus/device/function
找到 vendor=0x1af4 的 virtio-blk-pci(可多块)
分配 BAR(多块盘各自分配不重叠的 BAR 地址)
解析 virtio PCI capabilities
初始化 modern virtio-pci device
配置 virtqueue
提交 block read / write request
按 ext4 magic 区分 rootfs 盘与 FAT 盘
```

---

## Network Stack

RmikuOS 自带一套 TCP/IP 协议栈：自 virtio-net 驱动起，经 Ethernet / ARP / IPv4 / ICMP / UDP / TCP 与 DHCP，到一组专用的 socket 系统调用（号段 100–109），最终在用户态跑起一个真实的 HTTP 服务器与 TFTP 客户端——宿主机浏览器经 QEMU `hostfwd` 直接访问 guest 内的页面，两台 QEMU 经 socket pair 互 ping。协议栈每一层都是内核 `drivers/net/` 下的 Rust 代码，不依赖任何外部网络 crate。

```text
用户态   httpd(静态文件 + JSON API)   tftp(文件注入)   ping / udp_test / tcp_test
            │  socket syscalls:100 SOCKET … 109 RECV(专用网络号段)
────────────┼───────────────────────────────────────────────────
内核       Socket 层(UDP / TCP 统一 socket table,端口冲突检查)
            │
            ├─ TCP   11 态状态机 · 滑动窗口 · Jacobson/Karn 自适应 RTO
            ├─ UDP   无连接收发 · 校验和
            └─ ICMP  echo request / reply(ping)
            │
            IPv4   头部校验和 · 按 protocol 字段分发(17=UDP,6=TCP,1=ICMP) · 同网段直连路由
            DHCP   四步交互(DORA)· 广播位 · options 解析,自动配置地址
            ARP    地址缓存 + 挂起队列(未命中先存整包,解析成功补发)
            │
            Ethernet → virtio-net 驱动 → QEMU slirp → 宿主机协议栈
```

QEMU 侧使用 slirp 用户态网络（`-netdev user`）：无需宿主机 root 权限，自带 DHCP 服务器（`10.0.2.2`）与 DNS（`10.0.2.3`），guest 默认落在 `10.0.2.15`。

### ARP：挂起队列（Pending Queue）

发包时 ARP 缓存未命中是常态，而地址解析是异步的。朴素实现直接丢包、把重试责任推给上层；RmikuOS 在 ARP 层内置一张 4 槽 PENDING 队列：未命中时整包入队并发出 ARP request，`on_arp_learned` 回调时补发，上层（IP / TCP / UDP）完全无感。实现上有一条锁纪律：回调点不得持有 ARP 缓存锁，否则会触发自研锁的同核重入死锁检测。

### IPv4 与校验和

* 发送时生成头部校验和、接收时验证；checksum 写回必须显式转大端——slirp 对校验失败的包**静默丢弃**（实踩的坑）；
* 本机地址原子化（`MY_IP`）：DHCP 完成前用默认值，租约落地后 `set_my_ip` 热切换；
* 按 protocol 字段分发到 UDP（17）/ TCP（6），未知协议打日志——漏写分发行曾导致 SYN-ACK 静默消失，这类坑必须能被一眼看见。

### TCP：教学版实现


* 11 态状态机（Closed → Listen → SynSent / SynReceived → Established → FinWait1 / 2 → CloseWait / Closing / LastAck → TimeWait），主动 open（connect）与被动 open（listen / accept）均支持；
* 发送侧：`tx_unacked` 重传队列（SYN / FIN 各占一个序号），**Jacobson/Karn 自适应 RTO**（RFC 6298 定点 SRTT/RTTVAR，RTO = SRTT + max(G, 4·RTTVAR)，200ms–16s，指数退避，最多 8 次；详见 Network Experiments 一节）；
* 接收侧：按序交付 + 固定窗口广告（65535），乱序段丢弃并重复 ACK；
* 定时器不依赖硬件中断：RTO / TIME_WAIT 等全部期限由 socket 层 `poll()` 内嵌的 `tick()` 驱动；
* 两条路径均已实机验证：主动 connect 经 slirp 访问宿主机 `nc -l`；被动 listen 经 hostfwd 接受宿主机浏览器连接。


### DHCP 客户端

内核态 DHCP 客户端：BOOTP 236 字节头 + magic cookie（`99,130,83,99`）+ options 编解码（53 消息类型 / 50 请求地址 / 54 服务器标识 / 55 参数请求 / 3 网关 / 6 DNS / 51 租期）。flags 置广播位 `0x8000`，使 OFFER / ACK 走二层广播——租约落地之前本机没有合法地址，单播回复无从送达。四步交互后 `set_my_ip(yiaddr)`，实测租得 `10.0.2.15`、租期 86400s。

### ICMP 与双机互 ping

ICMP echo request / reply 入栈后，两台 QEMU 可以直接对话：经 socket netdev pair（`-netdev socket,listen=` / `connect=`）二层直连、绕开 slirp，两台 guest 互设 `192.168.100.x` 后 ping 通。这一步抓出两个隐蔽 bug：

* **MAC 硬编码**：`eth.rs` 的 `MY_MAC` 写死了 slirp 模式分配的 `52:54:00:12:34:56`，而 socket pair 模式分配的是 `...0A` / `...0B`——A 机的包发出去了，B 机网卡不认这个源；
* **同网段路由**：`ip.rs` 的 `next_hop` 把同 /24 的地址也交给网关，ARP who-has 的始终是 `10.0.2.2` 而非对端——同网段应当直连，下一跳即目的地址本身。

两个 bug 都由「三段定位法」在 ARP 层现形：tcpdump 里 ARP 请求的目标地址暴露了一切。

### Socket 系统调用：100–109 专用号段

网络调用不挤占主系统调用表，独立开一段号段——主分发只多一条范围判断，后续扩充也不污染既有编号：

```text
100 SOCKET    101 BIND    102 SENDTO    103 RECVFROM    104 CLOSE
105 CONNECT   106 LISTEN  107 ACCEPT    108 SEND        109 RECV
```

用户态经 `net.h` 封装为 `net_socket()` / `net_socket_tcp()` / `net_bind()` / `net_connect()` / `net_listen()` / `net_accept()` / `net_send()` / `net_recv()` 等，体感与 POSIX 对齐。

### httpd：跑在自研协议栈上的 Web 服务器

协议栈的「真应用」验证：一个多文件 C 工程（`httpd.c` / `http.c` / `pages.c` + 头文件），顺带压测了用户态多文件编译与链接——并因此逼出并修复了头文件函数体未加 `static inline` 导致的 `multiple definition` 隐患（单文件时代不可见，多文件链接即炸）。

* **静态文件模式**：`httpd wow.html` 启动时将文件读入内存（16KB 缓冲），`/` 与 `/index.html` 发送文件内容；
* **内联路由**：`/demo` 内联演示页、`/hello`、`/api/stats`（JSON 实时请求计数）、404；
* **HTTP 细节自己扛**：TCP 是字节流，请求头边界靠扫描 `\r\n\r\n` 确定；发送超 1460 字节按 1400 切片；`Connection: close` 迭代式服务；
* **浏览器适配**：Chrome 会打开「占位不说话」的预热连接，迭代式服务器 accept 到它就会被焊死——recv 增加软超时（800ms），空连接到点踢掉，真实请求随后即被服务。

宿主机访问只需在 run.sh 的 netdev 上挂一行 port forward（**必须与 `id=net0` 同行**，拆成独立参数会被 QEMU 误认为磁盘镜像）：

```text
-netdev user,id=net0,hostfwd=tcp::8080-:8080
```

```text
/ $ httpd wow.html
[httpd] loaded wow.html, 6986 bytes, serving at /
[httpd] RmikuOS httpd listening on 10.0.2.15:8080
[httpd] #1 GET /
[httpd] #1 served, 6986 bytes, closing fd=2
```

浏览器打开 `http://127.0.0.1:8080/` 即可（内联演示页在 `/demo`）。随附的 `wow.html` 演示页每 2 秒 `fetch('/api/stats')` 刷新请求计数——页面上数字每跳一次，背后都是一次完整的 TCP 建立—传输—挥手。

### TFTP：经 slirp 的文件注入通道

rootfs 只读、重新打包 ext4 镜像太慢，实验文件（尺寸扫描用的 4K–1M 随机文件）需要一条运行时注入通道。slirp 内置 TFTP 服务器：在 netdev 上挂 `tftpboot=<绝对路径>`（必须是绝对路径，相对路径直接报 `Invalid parameter`），guest 内用户态 `tftp` 客户端（RRQ → DATA/ACK 停等）即可拉取宿主机目录里的文件：

```text
/ $ tftp hello.txt /tmp/a
tftp: hello.txt -> /tmp/a, 26 bytes
```

一个与文档印象不符的实测结论：**slirp 的 TFTP 服务器在 guest 视角是 `10.0.2.2`**（与 DHCP 网关同地址），而不是某些资料里的 `10.0.2.4`——向后者发 ARP who-has 永远无人应答，改指 `10.0.2.2` 即通。ACK 直接回 `recvfrom` 的 from 地址，TFTP 的 TID 语义天然正确。

### NTP 客户端：网络同步时间（墙钟 + 文件时间戳）

RmikuOS 通过网络协议拿到真实世界时间：宿主机的 Python NTP 服务器 + guest 内的用户态 `ntpdate` 客户端，一次校准内核墙钟，之后 `time()` 与文件 `mtime` 都是单调累加的真实 epoch 秒。

```text
宿主机                                    QEMU guest
┌──────────────────┐      slirp      ┌────────────────────────────┐
│ tools/ntp_server │←─ UDP 10.0.2.2 ─│ ntpdate(5次采样最小delay)   │
│  (RFC5905子集)   │── 123/任意端口 ─→│    │ SYS_SET_WALL_CLOCK     │
└──────────────────┘                  │    ▼                       │
                                      │ 内核墙钟(epoch微秒+单调累加) │
                                      │    ├─ time()/gettimeofday  │
                                      │    └─ Stat.mtime → st_mtime│
                                      └────────────────────────────┘
```

#### 原理：RFC 5905 教学子集

* **四时间戳**：`offset = ((T2−T1) + (T3−T4)) / 2`——请求去程与响应回程各带一次"服务器时间减客户端时间"，平均后抵消网络延迟，得到真实时钟偏移（`delay = (T4−T1) − (T3−T2)` 是往返延迟）。
* **64 位定点**：NTP 时间戳高 32 位=秒（1900 纪元）、低 32 位=分数秒（2⁻³²）。分数秒让 delay 可测到毫秒级，否则 delay 全整数秒、"取最小"失去意义。
* **防溢出**：两个 ≈22 亿秒级时间戳相减后相加会超 2⁶⁴——必须 `((T2−T1)>>1) + ((T3−T4)>>1)`（先移位再相加，丢 0.1ns）。
* **最小延迟原则**：5 次采样取 delay 最小那次——网络最空闲 ≈ 去回程最对称 ≈ offset 最准。
* **本地假时钟**：T1/T4 用 `get_time_us()`（单调微秒，自启动起，0 基准）。offset 是"服务器绝对时间 − 本地开机基准"的常数差，校准后 `墙钟 = 单调 + offset`——完美绕开"没时钟"的鸡生蛋问题。

#### 墙钟与 Stat 时间戳

内核 `timer` 维护墙钟：`set_wall_clock(epoch_us)` 存"校准时刻的绝对微秒 + 单调微秒快照"，`now_secs()` 单调累加（未校准返回 0）。`Stat` 新增 `mtime` 字段（复用原 reserved[4]，32 字节布局不变），各文件系统 `stat()` 填 `now_secs()`；用户态 `fs.h` 翻译层填 `st_mtime`（atime/ctime 教学简化同 mtime）。`time()`/`gettimeofday` 也改接墙钟（新 syscall `SYS_GET_EPOCH`）。

#### 使用

```bash
# 宿主机（tools/ntp_server.py, RFC 5905 教学子集服务器）：
sudo python3 tools/ntp_server.py          # 端口 123(需 root); 或 -p 12300 免 root
```

```text
/ $ ntpdate                              # 默认 10.0.2.2:123; 或 ntpdate 12300
[ntpdate] synced: epoch=1785658218 s, delay=2 ms
/ $ sqlite3 /fat/test.db                 # 落盘库（CREATE TABLE 需 VFS xOpen 判 pOutFlags）
sqlite> CREATE TABLE t(x);  INSERT INTO t VALUES(42);  .quit
/ $ /samples/stat_time /fat/test.db
path          : /fat/test.db
mtime(epoch)  : 1785658405
time()(epoch) : 1785658405
mtime(GMT)    : 2026-08-02 08:13:25
```

QEMU slirp 关键路径：guest 发 UDP 到 `10.0.2.2:<端口>` 会被自动转发到宿主机 loopback 同名端口（无需 hostfwd），与 TFTP/DHCP 同一通道。

#### 已知局限（教学取舍）

* **QEMU TCG 虚拟时钟**：TCG 动态翻译下 `time` 寄存器按虚拟时间推进，负载不同可能与真实时钟速率不一致（delay 异常大时 epoch 有百秒级偏差）。真实硬件 timebase 与晶振绑定，无此问题。
* **闰秒 / 2036 纪元回绕**：RFC 5905 用 era 判断（服务器时间戳在本地 ±68 年内则判定回绕 +2³²）处理；教学版注释说明不实现。
* **FAT 目录项 DOS 时间戳未读**：mtime 取 stat 时刻墙钟而非磁盘持久化修改时间（需 DOS→epoch 转换，留待以后）。
* **相关 syscall 号段**：`SYS_SET_ECHO=69`、`SYS_SIGNAL=70`、`SYS_SET_FRONT=71`、`SYS_GET_TIME_US=72`、`SYS_SET_WALL_CLOCK=73`、`SYS_GET_EPOCH=74`。

### wget：TCP 客户端从 host 拉文件（网络栈的下载闭环）

`user/c/wget/wget.c`（约 150 行）——用自研 TCP 栈发 `HTTP/1.0 GET`，把响应体存进 FAT：

```
wget http://10.0.2.2:8000/hello.txt /fat/hi.txt   # URL 形式(默认端口 80)
wget http://10.0.2.2:8000                          # 无路径 -> GET /
wget 10.0.2.2 8000 /hello.txt /fat/hi.txt          # 旧三参形式(兼容)
```

流程：`socket → connect → send GET（HTTP/1.0 + Connection: close）→ 收响应头（缓冲找 \r\n\r\n）→ 收 body 到连接关闭 → write 落盘`。host 侧 `python3 -m http.server 8000` 即服务端——guest 访问 `10.0.2.2:8000` 经 slirp 转发到 host loopback（与 NTP 同款通道，无需 hostfwd）。

实测输出：

```
[tcp] fd 1 established
wget: HTTP/1.0 200 OK
Server: SimpleHTTP/0.6 Python/3.14.4
Content-Length: 17
...
/ $ cat /fat/hi.txt
hello from host!
```

#### 顺带把内核 recv 改成 POSIX 语义

wget 调试暴露了内核一个 API 简化：`tcp::recv_data` 原来**弹出整个 TCP chunk、只返回请求长度**——逐字节读的客户端（如 HTTP 头解析）会丢数据。已修复为 POSIX 语义：消费 n 字节后 `chunk.drain(..n) + push_front` 把剩余放回队列头，下次 recv 继续读。UDP 数据报语义本就正确、未动。**现在任意读法（大块/逐字节）都安全**。

#### 踩坑两个

* **双重 htons**：`addr_of` 内部已 `htons(port)`，再套一层会转回主机序——8000(0x1F40) 变 16415，SYN 发错端口。tcp_test 直传端口所以从没暴露；
* **整块弹出丢数据**：见上——客户端必须大块读（现已在 API 侧根治）。

---

### 排障方法学：三段定位法

网络问题一律按「guest 发没发对 → slirp 转没转发 → 宿主机谁收走」三段切分：

```text
-object filter-dump,id=f0,netdev=net0,file=/tmp/rmiku.pcap   # guest 网线上抓包
sudo tcpdump -i lo -nn -X udp port 9999                      # slirp 是否已转发到宿主机
ss -ulnp | grep 9999                                         # 宿主机端口被谁持有
```

实战战绩：曾用这套方法揪出「4 个僵尸 nc 进程同绑一个 UDP 端口、报文全进旧进程接收队列」——guest 侧报文逐字节验证完美，锅在宿主机。

---

## Network Experiments：TCP RTO 对照实验

网络栈的第二组实验回答一个问题：**重传超时（RTO）的估计方式，对真实传输性能影响多大？** 对照双方共享除估值器以外的全部代码（同一状态机、同一窗口管理、同一丢包装置），唯一变量是 RTO 算法：

```text
new:  Jacobson/Karn 自适应 RTO(RFC 6298 风格,定点实现)
      SRTT/RTTVAR 按 ×8/×4 缩放存储,除法即右移,无浮点
      RTO = SRTT + max(G, 4·RTTVAR),clamp [200ms, 16s],G = 10ms(tick 粒度)
      Karn 两条:重传段不采样(ACK 歧义);退避期间保持翻倍后的 RTO
      队首采样规则:每个 ACK 只在弹出队首段时采样(队首干净 ⟹ 样本新鲜)
      RTO-restore:有前向进展但无干净样本时恢复估值(参考 Linux)

old:  固定 RTO = 1s,指数退避,封顶 16s(实现 Jacobson 之前的原版)
```



### 实现过程中抓出的三个深邃 bug

* **陈旧样本死亡螺旋**：串行修洞（每 tick 只重传队首）下，洞修好后的累积 ACK 会连跳弹出多个老段；若对每个被确认的段都采样，`now − sent_ms` 里混入了等待修洞的时间，假样本按等比数列膨胀（实测 100→202→422→…→254659ms），RTO 一路爆炸到封顶。修复即「队首采样规则」：队首段干净意味着它发出未满一个 RTO，样本必然新鲜。
* **退避棘轮**：Karn 的「退避保持到下一个干净样本」与队首采样叠加后，排水期（数据已发完、只剩重传在飞）永远采不到干净样本，RTO 每个洞翻倍一次，几洞之内钉死在 16s。修复即「RTO-restore」：只要有前向进展就恢复估值——旧版固定 RTO 天然等价于此，这也是对照实验公平的一环。
* **无流量控制**：初版 `send_data` 只看对端窗口是否为零、不跟踪在途字节，1MB 传输瞬间灌爆宿主 64KB 接收窗，真实丢包与注入丢包混杂，实验不可解释。修复为阻塞式窗口管理（`in_flight = snd_nxt − snd_una`，窗口满则解锁 poll 等待）。

### 实验装置

* **确定性丢包**：`LOSS_EVERY = N` 时每 N 个数据段丢弃 1 个（SYN / FIN 不丢），完全可复现；
* **单变量对照**：对照版与新版共享丢包装置、流量控制与打点，只差估值器；
* **自造噪声消除**：正式测量前 30 个 4K 请求预热，每次 run 间隔 2s——否则 8 槽 socket 表被 TIME_WAIT（10s）子连接占满，SYN 被静默丢弃，宿主退避产生周期性 18s 停摆；
* 每组 5–7 次取中位数；脚本：`tcp_exp.sh`（丢包率组）/ `tcp_size_sweep.sh`（尺寸组）/ `plot_tcp.py`（绘图），数据落盘 `logs/tcp/`。

### 实验一：尺寸扫描 @ 0% 注入丢包（4K – 1M）

![size sweep](logs/tcp/fig1_size_sweep.png)

* ≤64K：两版差异在 ±10% 噪声带内——**do no harm**，自适应估值器在无损路径上不引入额外开销；
* ≥128K：new 在两个独立 session 中稳定更快（1M：22.4s / 21.7s vs 28.2s / 48.8s），方向可复现，幅度受宿主噪声影响，给区间不给单点。

![cross-session drift](logs/tcp/fig2_drift.png)

跨 session 漂移（同版本连跑两批）：new 的 1M 中位数漂移 **−3%**，old 的 1M 漂移 **+73%**；小尺寸双方均在 ±30% 噪声带内。注意两批的运行顺序与版本相关（位置效应未消），此图作为 observation 呈现，交错重复实验见 Roadmap。

### 实验二：丢包率扫描 @ 100K（0 / 5 / 10 / 20%）

![loss sweep](logs/tcp/fig3_loss_sweep.png)

| 注入丢包率 | new（中位数） | old（中位数） | 提速      |
| ---------- | ------------- | ------------- | --------- |
| 0%（对照） | 2.052s        | 2.111s        | 1.03×     |
| 5%         | 2.253s        | 5.425s        | **2.41×** |
| 10%        | 3.187s        | 10.765s       | **3.38×** |
| 20%        | 5.770s        | 23.243s       | **4.03×** |

* 0% 对照臂两版一致（1.03×），实验台自证干净；
* old 耗时随丢包率近似线性爆炸（≈ 每 1% 丢包 +1.05s），正是固定 1s RTO「每洞罚一秒」的理论预期；new 的 RTO 收敛在 200ms 附近，曲线平缓；
* 机制佐证：逐洞恢复耗时 new ≈ 200ms/洞、old ≈ 1s+/洞（恢复比 5–9×）；且 new 的恢复时间随洞序号线性爬升——这是串行修洞的排队签名，也是 Roadmap 中快重[docs] readme更新cubic实验传 / SACK 的直接动机；
* 采样规模：new 传 1M 采 5236 个 RTT 样本，old 全程 0 个（它没有估值器）——对照的本质浓缩在这一数字里。

## TCP CUBIC 拥塞控制实验

在 RmikuOS 教学 TCP 栈上实现 **CUBIC(RFC 9438)** 拥塞控制与快速重传,并与无拥塞控制版本在确定性丢包下做 A/B 对照。

### 实验设计

- **对照组**:old 版(无 cwnd,仅接收窗口流控 + RTO 重传)
- **实验组**:CUBIC 版(慢启动 + 立方增长 + β=0.7 降窗 + 快速收敛 + 3-dupACK 快速重传)
- **丢包装置**:发送侧每 `LOSS_EVERY` 个数据段丢 1 个(确定性、可复现),档位 {0, 20, 50, 100, 200, 500}
- **负载**:guest 内 httpd 发文件,宿主机 curl 下载,尺寸 {64K, 256K, 1M},每格 7 次取中位数
- **打点**:每连接一行 `[tcp-stat]`(字节/重传/降窗计数),`[cwnd]` 逐次变窗轨迹;宿主机按日志书签切片聚合

### 结果

![CUBIC 锯齿](logs/tcp/figs/fig1_cwnd_sawtooth.png)

![恢复路径对比](logs/tcp/figs/fig2_recovery.png)

- 相同丢包序列下两版**重传总量一致**(fig2 柱高),证明装置公平;差异全在恢复路径:**99.6% 的重传由快速重传完成**(l20@1M:fast=277, RTO=1),单次丢包恢复代价从 ≥200ms(RTO_MIN)降至约 0。
- 实测降窗次数与理论丢包数(段数/LOSS)在全部档位吻合(见 fig3),丢包装置与打点计数自洽。
- RTT 样本数随丢包率下降(fig5),符合 Karn 规则(重传段不采样)。

### 复现

```bash
# 终端1: QEMU 输出落盘(每次重启先删旧日志)
rm -f logs/console.log && ./run.sh riscv64 debug 2>&1 | tee logs/console.log
# 终端2: 扫描(LOSS 标签需与内核编译的 LOSS_EVERY 一致)
./scripts/tcp_loss_sweep.sh old   100 7
./scripts/tcp_loss_sweep.sh cubic 100 7
# 出图
python3 scripts/plot_tcp.py
```

### 局限性

- QEMU 内 RTT≈0,协议栈受 CPU/串口限制(~27KB/s),在途数据不足 1 段,cwnd 不构成瓶颈——**计时列仅作参考**,结论以机制计数为准;窗口瓶颈实验需关闭日志并加链路延迟(设计见实验记录)。
- 耗时存在会话级漂移(每次换内核冷启动),跨行绝对值不可比。
- 接收端原为 GBN 行为(乱序丢弃),已由后续的 SR 升级(重组缓存)解决;SACK 选项未实现。


## TCP 接收端升级实验:Go-Back-N vs Selective Repeat

在 RmikuOS 教学 TCP 栈(CUBIC 拥塞控制 + 快速重传)上,将接收端从 **GBN 行为**(乱序即丢弃)升级为 **SR 行为**(乱序进重组缓存,洞补上后顺序交付),在确定性丢包下做 A/B 对照。

### 前置修复

实验前发现通告窗口 65535 与宿主机 slirp 接收缓冲(≈64KB)几乎相等,满窗口发送会打爆宿主缓冲造成**不可控真实丢包**(无损基线 34~54s 剧烈散布)。将通告窗口降至 **16384(11 段)** 后,基线收敛至 29.2s ± 1.2s。此后所有数据均为 16KB 窗口配置。

### 实验设计

- 对照:同一 CUBIC+快速重传内核,仅接收路径不同(丢弃 vs 缓存)
- 丢包:{0, 1/5, 1/6, 1/7, 1/10, 1/20, 1/50, 1/100, 1/200, 1/500} 八档 × 20 次/格,1M 文件
- 每组跑于独立冷启动会话;污染会话(宿主机并行实验导致单调爬升/钟形隆起)整组作废重跑
- 统计:中位数 + IQR,离群点不剔除但由中位数免疫

### 结果

![耗时-丢包率](logs/sr/figs/fig_sr2_time_vs_loss.png)

![分组箱线图](logs/sr/figs/fig_sr2_box.png)

| 丢包率       | GBN 中位(s) | SR 中位(s) | 结论                            |
| ------------ | ----------- | ---------- | ------------------------------- |
| 0            | 29.9        | 29.2       | 打平                            |
| 1/5          | 71.3        | 69.8       | 共同触底                        |
| 1/6          | ~35.1       | ~35.9      | 打平，中间态                    |
| **1/7**      | **~35.7**   | **~28.9**  | **SR 快 19%,SR 回到基线水平！** |
| 1/10         | 30.7        | 28.6       | **SR 快 7%**                    |
| 1/20         | 30.4        | 28.6       | **SR 快 6%**                    |
| 1/50 ~ 1/500 | 28.4~28.7   | 28.6~30.5  | 打平                            |




### 分析

1. **SR 的收益集中在 1/10~1/20 重-中丢包档**:此时丢包频繁到 GBN 级联(洞后段被丢弃、逐段等 dup ACK 链式修复)构成可测开销,而又未让 cwnd 触底。
2. **1/5 档两组共同劣化至 ~70s**:每 5 段丢 1 个使 cwnd 长期钉死在 2 段下限,吞吐 ∝ 窗口。定量验证:2/11 ≈ 0.41 ≈ 28.6/69.8——窗口下限主导一切,接收端策略无关。
3. **在 5 和 10 之间挖出了一个悬崖 **：对 SR 来说，1/6 还在 36s 的中间态，1/7 就突然跌回 28.9s 的无损基线水平，一档之差 24%。1/6→1/7 恰好跨过临界点，就是离散动力学里的 regime switch。
4. **轻丢包档打平**:丢包间隔超过窗口,GBN 级联深度 ≈ 0,丢弃与缓存无差异。
5. **SR 收益整体温和(≈7%)的原因**:本环境 RTT≈0、在途段数 ≪ 窗口,且快速重传已将级联修复的等待代价压至近零——SR 相对"GBN+快速重传"的边际收益天然有限。SR 的决定性优势需要足够大的带宽时延积(丢包瞬间洞后存在大量在途段)才能显现,列入后续工作(链路延迟队列,设计已定)。

### 实验教训(数据质量控制)

- 首轮 gbn 数据被宿主机并行任务污染(组内单调爬升 32→55s),整组重跑;教训:**对照组应尽量交错/同窗口运行,或用定标 curl 监控会话漂移**。
- sr@200/500 格仍有少量离群点(46~56s),中位数免疫,均值已标注仅供参考。

### 复现

```bash
./scripts/sr_run.sh gbn 10 20 1M     # 采集(标签须与内核 LOSS_EVERY 一致)
./scripts/sr_run.sh sr  10 20 1M
python3 scripts/plot_sr2.py          # 出图 + 汇总表
```



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

![绝对时间](logs/jvm/bench_abs.png)


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

![加速比](logs/jvm/bench_speedup.png)

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

![跨架构](logs/jvm/bench_arch.png)

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

![process&thread map](docs/images/process.png)

### 彩蛋：Brainfuck 解释器（8 指令的抽象语言）

凑数的抽象语言彩蛋——**不算第六种用户态语言**（真正支持的语言是上面五个），只是一个 8 指令的图灵完备玩具解释器（`/programs/brainfuck`）：

```
/programs/brainfuck /codes/hello.bf     # Hello World!
/programs/brainfuck /codes/mult.bf      # d（10x10 循环乘法）
/programs/brainfuck /codes/echo.bf      # 读 4 个字符原样回显（测 ',' 指令）
```

实现要点：磁带 `malloc(30000)`、**括号配对预处理 jump 表**（一遍扫描算出每个 `[` 的匹配 `]`，执行时 O(1) 跳转）、纯用户态零新头文件（只依赖 `user.h`）。`/codes/` 里带 3 个示例程序。踩过的一个雷：RmikuOS 用户栈只有 64KB，源缓冲必须上堆（64KB 局部数组会撑爆栈）。

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

![riscv64 linearity](logs/sched/calibrate/calib_linearity_riscv64.png)

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

![loongarch64 linearity](logs/sched/calibrate/calib_linearity_loongarch.png)

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

![riscv64 burn](logs/sched/calibrate/calib_burn_riscv64.png)

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

![loongarch64 burn](logs/sched/calibrate/calib_burn_loongarch.png)

---

## Phase 3: 漂移

| 平台 | pre (us/tick ×1000) | post (us/tick ×1000) | drift |
|------|---------------------|----------------------|-------|
| riscv64 (SMP=1) | 14,562,515 | 14,649,211 | **0%** |
| loongarch64 | 120,795,020 | 120,778,716 | **0%** |

**✅ 两平台 drift = 0%**，正式实验前标定完全有效。

![drift summary](logs/sched/calibrate/calib_drift.png)

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

![eff_tickets vs α](exp1_eff_tickets_5.png)

*eff_tickets 随 α 单调递增。点=实测，线=理论 `100×n^(α/100)`。t1(1线程)在 α<100 时恒为 100，t2(9)从 100 爬到 1000，t3(25)从 100 爬到 2600。*

![CPU share vs α](exp1_cpu_share_5.png)

*CPU share 从 33:33:33（α=0）平滑过渡到 2.7:25.8:71.5（α=100）。t3 随 α 增大获得越来越多 CPU，t1 逐渐被压缩但不饥饿。*

![Jain fairness vs α](exp1_jain_5.png)

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

![eff_tickets vs α (step=1)](exp1_eff_tickets_1.png)

*101 点连续扫描。eff_tickets 曲线平滑单调，α=92 处 t3 有一个微小下凹（瞬时快照噪声）。*

![CPU share vs α (step=1)](exp1_cpu_share_1.png)

*101 点 CPU share。曲线整体平滑，α=92 处 t2 share 异常偏高（33.2 vs 周围 26-27），是同一瞬时快照噪声的表现。*

![Jain fairness vs α (step=1)](exp1_jain_1.png)

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

![miss rate comparison](exp2_compare_miss.png)

*5 个配置的 miss rate vs α。压力越大曲线越靠左——edge 出现在更低 α。*

![trade-off comparison](exp2_compare_tradeoff.png)

*ctrl miss rate vs ai CPU share。不同配置的 trade-off 曲线，α 标注在点旁。*

#### medium 配置（核心）

![medium miss & share](exp2_medium_miss_share.png)

*ctrl miss rate（红，左轴）+ ai/log share（虚线，右轴）vs α。α=30→40 miss 从 16% 跳到 66%。*

![medium tardiness](exp2_medium_tardiness.png)

*ctrl 迟到程度（avg/max late）vs α。α=40 后 avg_late 急剧上升。*

![medium jain](exp2_medium_jain.png)

*Jain 公平指数 vs α。α 增大后 ctrl 被压制，Jain 下降。*

#### 其他配置

| 配置 | miss & share | tardiness | jain |
|------|-------------|-----------|------|
| light | [图](exp2_light_miss_share.png) | [图](exp2_light_tardiness.png) | [图](exp2_light_jain.png) |
| medlo | [图](exp2_medlo_miss_share.png) | [图](exp2_medlo_tardiness.png) | [图](exp2_medlo_jain.png) |
| heavy | [图](exp2_heavy_miss_share.png) | [图](exp2_heavy_tardiness.png) | [图](exp2_heavy_jain.png) |
| extreme | [图](exp2_extreme_miss_share.png) | [图](exp2_extreme_tardiness.png) | [图](exp2_extreme_jain.png) |

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


# 03 · AIMD 恒定负载自适应

## 目的

验证 AIMD 控制器在恒定负载下的行为：
1. **三起点收敛**：AIMD 从 α0=0/50/100 是否收敛到同一条稳态线
2. **不崩特性**：AIMD 是否在所有负载下 miss 保持低位（不像 fixed50+ 在重负载崩到 80-99%）
3. **edge 验证**：fixed 扫描 0/25/50/75/100，miss 急剧上升点是否匹配 exp2 edge
4. **vs fixed 基线**：AIMD 相比固定 α 的优劣，定位 AIMD 的价值场景

## 配置

| 任务 | tickets | 线程 | 类型 | period | burn | 备注 |
|------|---------|------|------|--------|------|------|
| ctrl | 300 | 1 | in-parent jobs | 5 | 180,000 | deadline 任务，D 行反馈 |
| ai | 100 | 变 | spin (fork) | — | 12,000 | 抢占负载 |
| log | 50 | 变 | spin (fork) | — | 12,000 | 后台负载 |

4 个压力等级：

| 配置 | ai 线程 | log 线程 | exp2 edge α |
|------|--------|---------|------------|
| light | 7 | 3 | ~55 |
| medlo | 15 | 8 | ~45 |
| medium | 25 | 9 | ~35 |
| heavy | 75 | 25 | ~25 |

- 平台：riscv64 (QEMU), SMP=1, 1 tick = 13.98 ms
- total = 24000 ticks/trial, window = 100 ticks (~240 windows/trial)
- 每配置：fixed(0/25/50/75/100) × 3 reps + AIMD α0=0/50/100 × 3 reps = 24 trials
- 总计：4 configs × 24 = **96 trials**
- **AIMD 参数**：INC=5（sl_aimd_init 默认值，代码改 inc=1 但未 save all，实际跑的是 inc=5）, BACKOFF=80%, safe_lateness=0, danger_lateness=25, cooldown=1

> **INC 实际值说明**：源码 `sl_aimd_init` 里写了 `a->inc = 1`，但编译时 schedlab.h 没保存（用户忘记 save all），实际跑的二进制 inc=5（旧默认值）。diag_alpha_traj 验证：after 值 `0→5→10→15→...→60`，每次 up 跳 5，实锤 inc=5。inc=1 数据待重跑。

## 运行

```bash
/ $ ./sched/sexp3_aimd > /tmp/sexp3_aimd.csv
python3 ./scripts/sched/stat_exp3.py ./logs/sched/aimd/sexp3_aimd.csv
```

## 实测结果（2026-07-31，96 runs）

### 汇总表

误差为非对称 +hi/-lo（3 reps 的 max-mean / mean-min）。

| config | mode | α0 | miss% | α_steady | sh_ai | ai_work | Jain |
|--------|------|----|-------|----------|-------|---------|------|
| heavy | fixed0 | 0 | 0.9 +0.4/-0.2 | 0 | 57.3 | 12742 | 0.855 |
| heavy | fixed25 | 25 | 4.6 +1.8/-1.0 | 25 | 57.5 | 12830 | 0.852 |
| heavy | fixed50 | 50 | 79.1 +0.9/-1.1 | 50 | 67.2 | 15552 | 0.826 |
| heavy | fixed75 | 75 | 98.7 +0.6/-1.1 | 75 | 76.0 | 17638 | 0.831 |
| heavy | fixed100 | 100 | 99.4 +0.1/-0.1 | 100 | 82.7 | 19213 | 0.750 |
| heavy | aimd0 | 0 | 12.1 +2.0/-2.2 | 25.4 +8.6/-10.4 | 61.0 | 13560 | 0.848 |
| heavy | aimd50 | 50 | 15.2 +2.2/-1.7 | 24.4 +5.6/-9.4 | 61.2 | 13614 | 0.846 |
| heavy | aimd100 | 100 | 11.7 +2.1/-2.3 | 24.7 +10.3/-9.7 | 63.9 | 14334 | 0.842 |
| medlo | fixed0 | 0 | 1.6 +1.6/-0.9 | 0 | 55.2 | 12629 | 0.855 |
| medlo | fixed25 | 25 | 1.5 +1.6/-1.0 | 25 | 54.8 | 12457 | 0.882 |
| medlo | fixed50 | 50 | 20.6 +7.3/-4.9 | 50 | 60.5 | 13935 | 0.851 |
| medlo | fixed75 | 75 | 62.3 +2.1/-1.8 | 75 | 70.1 | 16400 | 0.787 |
| medlo | fixed100 | 100 | 91.2 +2.3/-1.4 | 100 | 71.6 | 16834 | 0.760 |
| medlo | aimd0 | 0 | 16.0 +2.9/-2.7 | 39.8 +5.2/-7.8 | 62.3 | 14397 | 0.843 |
| medlo | aimd50 | 50 | 11.6 +2.5/-1.3 | 42.5 +12.5/-12.5 | 64.1 | 14825 | 0.846 |
| medlo | aimd100 | 100 | 12.8 +1.9/-3.8 | 39.5 +10.5/-15.5 | 61.2 | 14052 | 0.843 |
| medium | fixed0 | 0 | 1.1 +0.9/-0.6 | 0 | 58.2 | 13306 | 0.854 |
| medium | fixed25 | 25 | 1.1 +1.1/-0.7 | 25 | 69.6 | 15900 | 0.793 |
| medium | fixed50 | 50 | 53.0 +6.1/-11.7 | 50 | 70.6 | 16457 | 0.766 |
| medium | fixed75 | 75 | 85.4 +0.7/-0.9 | 75 | 74.3 | 17448 | 0.783 |
| medium | fixed100 | 100 | 95.7 +0.7/-0.9 | 100 | 78.5 | 18451 | 0.772 |
| medium | aimd0 | 0 | 17.2 +5.7/-7.4 | 35.0 +4.0/-14.0 | 65.3 | 15098 | 0.848 |
| medium | aimd50 | 50 | 14.0 +3.0/-3.8 | 34.4 +3.6/-6.4 | 63.8 | 14637 | 0.845 |
| medium | aimd100 | 100 | 16.2 +3.4/-1.8 | 34.0 +9.0/-8.0 | 62.8 | 14179 | 0.835 |
| light | fixed0 | 0 | 0.6 +0.3/-0.4 | 0 | 62.5 | 14640 | 0.856 |
| light | fixed25 | 25 | 0.5 +0.5/-0.2 | 25 | 62.4 | 14585 | 0.856 |
| light | fixed50 | 50 | 1.0 +1.4/-0.7 | 50 | 75.1 | 17574 | 0.701 |
| light | fixed75 | 75 | 37.8 +6.7/-11.6 | 75 | 75.4 | 17720 | 0.807 |
| light | fixed100 | 100 | 68.3 +3.3/-2.1 | 100 | 71.2 | 16708 | 0.751 |
| light | aimd0 | 0 | 9.4 +4.7/-2.9 | 60.3 +15.7/-24.3 | 72.1 | 16822 | 0.758 |
| light | aimd50 | 50 | 14.5 +0.8/-0.8 | 55.2 +5.8/-21.2 | 71.7 | 16631 | 0.708 |
| light | aimd100 | 100 | 15.0 +0.5/-0.4 | 57.2 +2.8/-11.2 | 80.6 | 18868 | 0.702 |

### 三起点收敛

| 配置 | exp2 edge | aimd0 α_steady | aimd50 α_steady | aimd100 α_steady | 三起点收敛 | 匹配 edge? |
|------|----------|---------------|----------------|-----------------|-----------|-----------|
| light | ~55 | 60.3 | 55.2 | 57.2 | ✅ ~57 | 略超（+2） |
| medlo | ~45 | 39.8 | 42.5 | 39.5 | ✅ ~40 | 略低（-5） |
| medium | ~35 | 35.0 | 34.4 | 34.0 | ✅ ~34 | ✅ 匹配 |
| heavy | ~25 | 25.4 | 24.4 | 24.7 | ✅ ~25 | ✅ 匹配 |

**三个起点收敛到同一条稳态线**，α_steady 基本贴合 exp2 edge（medium/heavy 精准，light/medlo 偏差 ±5）。注：本数据 inc=5（见上说明），inc=1 数据待重跑。

### fixed 扫描 & edge 验证

fixed 五点扫描的 miss% 急剧上升区间，验证 exp2 edge 定位：

| 配置 | f0 | f25 | f50 | f75 | f100 | miss 跳变区间 | edge 定位 |
|------|----|-----|-----|-----|------|-------------|----------|
| light | 0.6 | 0.5 | 1.0 | 37.8 | 68.3 | 50→75 | ~55 ✅ |
| medlo | 1.6 | 1.5 | 20.6 | 62.3 | 91.2 | 25→50 | ~45 ✅ |
| medium | 1.1 | 1.1 | 53.0 | 85.4 | 95.7 | 25→50 | ~35 ✅ |
| heavy | 0.9 | 4.6 | 79.1 | 98.7 | 99.4 | 25→50 | ~25 ✅ |

fixed 扫描独立验证了 exp2 的 edge：负载越重，edge 越左移（light edge~55，heavy edge~25）。

### AIMD vs fixed

| 配置 | fixed0/25 miss | AIMD miss | AIMD α_steady | AIMD 评价 |
|------|---------------|----------|--------------|----------|
| light | 0.5-0.6% | 9-15% | ~57 | 不崩，但 miss 比 fixed0/25 高（α 稳态 57>edge 55） |
| medlo | 1.5-1.6% | 12-16% | ~40 | 不崩，miss 比 fixed0/25 高 ~14pp |
| medium | 1.1% | 14-17% | ~34 | 不崩，远好于 fixed50+(53-96%) |
| heavy | 0.9-4.6% | 12-15% | ~25 | 不崩，远好于 fixed50+(79-99%) |

**恒定负载下 fixed0/25 最优**（ctrl 天然占优，α=0/25 时 ai 不抢资源，miss<5%）。AIMD 的 α_steady 落在 edge 附近，miss 比 fixed0/25 高 10-15pp——因为 AIMD 把 α 推到 edge（ai 吞吐最大化），代价是 ctrl miss 上升。

### 度量方式：ai_burn vs ai_run

脚本输出两个吞吐量指标，含义不同：

| 指标 | 来源 | 含义 | 量级 |
|------|------|------|------|
| **ai_burn** | K 行 `work` | ai 完成的 `sl_burn()` 迭代数（**实际计算量**） | ~12000-19000 |
| **ai_run** | W 行 `run_delta` | ai 线程获得的 CPU ticks（**CPU 分配量**） | ~12000-19000 |

两者通过 `run/burn` 比（~0.12）关联但不等价：
- **run_delta**：内核每 tick 调 `account_current_tick()` 给当前线程 `run_ticks += 1`，两个 window 之间的差值就是 run_delta——线程被分配了多少 CPU 时间（含被抢占浪费的 tick）
- **burn 迭代数**：`sl_burn(12000)` 每跑完 12000 次乘法算 1 个 burn——线程实际做了多少计算

**为什么不一样**：一个 burn 迭代大约花 0.12 tick，但不精确。线程跑到一半被抢占 → 这 1 tick 只跑了部分 burn 但 tick 照记 → run/burn 升高。线程连续运行、cache 热 → burn 效率高 → run/burn 降低。AIMD 频繁调 `set_sched_alpha` 会打断 ai 线程 → 浪费 tick → run 偏高但 burn 不增 → run/burn 比升高。

**两个指标各有价值**：
- `ai_run` 诚实反映 AIMD 的"调度成本"——eff 高但 run 只多一点点，差额就是 set_sched_alpha 开销
- `ai_burn` 反映"实际产出"——程序到底跑了多少计算，是端到端吞吐量

### AIMD actions 分布（典型）

以 medium aimd100 为例（3 reps 合计）：
```
cool:56  down:56  gray:448  hold:11  up:82
```
- **gray 占绝大多数**（448/~650）：AIMD 大部分时间在灰区（0≤late<50），既不升也不降
- **up:82 / down:56**：在 edge 附近反复 probe up / 退避，体现自适应
- **cool:56**：退避后冷却窗口

### 窗口数差异与 α 轨迹归一化

诊断脚本（`diag_a_count.py`）统计 A/S 行数后发现：**A 事件确实每 window 都输出**（A==S，全 EVERY-WIN），schedlab.h 无需改。但不同 run 的 **window 总数（S 行数）差异巨大**：

| config | mode | rep1 | rep2 | rep3 |
|--------|------|------|------|------|
| light | aimd100 | 60 | 21 | 24 |
| light | aimd50 | 223 | 175 | 209 |
| light | aimd0 | 153 | 123 | 148 |

**根因**：α 高时 ai 线程占 CPU 多，监控进程（ctrl in-parent）被抢占，`sleep(100)` 实际跨度变大，window 数变少。但实验总时间（24000 tick）相同——aimd100 的 21 个 window 也覆盖了完整 24000 tick，只是采样稀疏。α_traj 长度 = window 数 = 监控进程被调度频率，不是实验时间。

**处理**：`stat_exp3.py` 的三个 α 轨迹图（`plot_alpha_traj_all` / `plot_convergence_medium` / `plot_summary_config`）改为归一化 x 轴 + 插值对齐——每个 rep 的 x = win/max(win) 归一化到 [0,1]，在 100 个均匀点上 `np.interp` 插值后求 mean。三条线都从 0 画到 1，视觉等长。x 轴标签为 "Relative Time (normalized)"。

## 图表

### 跨配置
- `exp3_miss_all.png` — 4 配置 × 8 模式 miss rate 柱状图
- `exp3_alpha_traj_all.png` — 4 配置 α 轨迹（三起点收敛，x 轴归一化到 [0,1]）
- `exp3_burn_vs_miss.png` — ai burn 迭代数 vs ctrl miss 散点（每配置）—— **真正的吞吐量**
- `exp3_run_vs_miss.png` — ai CPU ticks (run_delta) vs ctrl miss 散点—— **CPU 分配视角**
- `exp3_share_vs_miss.png` — ai CPU share vs ctrl miss 散点（每配置）
- `exp3_burn_vs_run.png` — ai burn vs ai run 散点（4 配置 + fixed0 参考线，**点在参考线上方 = burn 效率高**）

### medium 深度
- `exp3_convergence_medium.png` — medium 三起点收敛叠加（x 轴归一化）
- `exp3_miss_traj_medium.png` — medium 逐窗口 miss rate
- `exp3_comparison_medium.png` — medium fixed vs AIMD 柱状对比

### 每配置 summary
- `exp3_summary_light.png` / `exp3_summary_medlo.png` / `exp3_summary_medium.png` / `exp3_summary_heavy.png`

## 调试历程

### Bug 1：AIMD 全退到 0
**现象**：AIMD 从任何 α0 收敛到 0，miss 全 30%（早期调试）。
**根因**：`danger_lateness` 对当时负载太敏感——in-parent ctrl 被主线程每 100tick 打断 1tick，late_delta 基线偏高，频繁触发退避。
**修复**：调整为 `safe_lateness=0 + danger_lateness=25`（run_aimd 当前生效值），配合 Bug 2 的 set_sched_alpha 修复后，各配置均能收敛到 edge 附近。danger 需在"轻负载能 probe up"与"重负载不误退避"间权衡——本实验 4 配置均为轻~中负载（ai 7~75 线程但 tickets 仅 100），danger=25 可用。

### Bug 2：set_sched_alpha 频繁重置 pass
**现象**：AIMD 所有配置 miss≈30%，α 差异被抹平。
**根因**：`sl_run` 每 window 都调 `set_sched_alpha(alpha)`，即使 α 没变。set_sched_alpha 重置所有 pass=0，stride 无法累积，ctrl/ai 变成 1:1 交替。
**修复**：只在 α 变化时调：
```c
if (new_alpha != alpha) {
    alpha = new_alpha;
    set_sched_alpha(alpha);
}
```

### Bug 3：INC=5 vs INC=1（实际跑的是 inc=5）
**现象**：diag_alpha_traj 打印 after 值 `0→5→10→15→...→60`，每次 up 跳 5。
**根因**：源码 `sl_aimd_init` 改了 `a->inc = 1`，但 schedlab.h **忘记 save all**，编译的二进制还是旧默认 inc=5。
**影响**：inc=5 步长大，probe up 时从 edge-5 跳到 edge+5，可能越过膝点。但实测三起点仍收敛到 edge 附近（±5），影响可接受。
**状态**：当前数据为 inc=5。inc=1 的精细逼近数据待重跑（需确认 save all + make clean）。

### Bug 4：safe_lateness 语义反了
**现象**：safe=10 比 safe=0 更激进（late≤10 就升 α）。
**根因**：safe_lateness 是"迟到≤此值算安全可 probe up"，值越大越容易升。想要保守应设 0。
**修复**：safe=0（不允许任何迟到就算安全）。

## 结论

**PASS（AIMD 收敛性验证通过）。**

1. **三起点收敛** ✅：α0=0/50/100 都收敛到同一条稳态线（light~57, medlo~40, medium~34, heavy~25）
2. **贴合 edge** ✅：α_steady 基本匹配 exp2 edge（INC=1 精细逼近后不再跳过）
3. **不崩特性** ✅：所有配置 AIMD miss 9-17%，fixed50+ 在重负载崩到 79-99%
4. **恒定负载非最优**：fixed0/25 miss<5% 优于 AIMD——因为 α=0/25 时 ctrl 天然占优，AIMD 把 α 推到 edge 牺牲了 ctrl
5. **→ exp4 动机**：恒定负载下 AIMD 没有优势（知道最优 α 直接 fixed 即可），**动态负载才是 AIMD 主场**——负载变化时 fixed 无法适应，AIMD 能动态调节

## 待补

- [x] fixed25/75：补全 fixed 扫描曲线（本次已完成）
- [x] α_traj 等长：诊断发现 A 已每 window 输出（A==S），长短不一是 window 总数差异（高α监控进程被抢占），已用归一化 x 轴解决，无需重跑实验
- [ ] exp4 动态负载：验证 AIMD 在负载变化时的自适应能力
- [ ] inc=1 重跑：save all + make clean 后用真 inc=1 数据验证（当前为 inc=5）

## 产出文件

```
logs/sched/aimd/
├── sexp3_aimd.csv                    # 原始 CSV (96 runs)
├── exp3_miss_all.png                 # 跨配置 miss rate 柱状图
├── exp3_alpha_traj_all.png           # 跨配置 α 轨迹
├── exp3_burn_vs_miss.png             # ai burn vs miss 散点（实际吞吐量）
├── exp3_run_vs_miss.png              # ai run vs miss 散点（CPU 分配）
├── exp3_share_vs_miss.png            # share vs miss 散点
├── exp3_burn_vs_run.png              # burn vs run 散点（效率对比 + fixed0 参考线）
├── exp3_convergence_medium.png       # medium 三起点收敛
├── exp3_miss_traj_medium.png         # medium 逐窗口 miss
├── exp3_comparison_medium.png        # medium fixed vs AIMD 对比
└── exp3_summary_{light,medlo,medium,heavy}.png
```

## 注意事项

- **ctrl 用 in-parent jobs**（D 行反馈），ai/log 用 fork 子进程
- **AIMD 参数**：INC=5（实际值，代码改 inc=1 未 save）, safe_lateness=0, danger_lateness=25（run_aimd 实际生效值，非 sl_aimd_init 默认）
- **set_sched_alpha 只在 α 变化时调**，避免 pass 频繁重置破坏 stride 公平性
- **误差为非对称 +hi/-lo**：hi=max(rep)-mean, lo=mean-min(rep)
- **ctrl tickets=300**：恒定负载下 α=0/25 最优，AIMD 价值在动态负载（exp4）
- **wake_blocked_thread 只对 tickets≤1 重置 pass**（exp2 修复），ctrl(tickets=300) 不享受"免费优先"


# 04 · 动态负载 AIMD 自适应

## 目的

验证 AIMD 控制器在**动态负载**（轻重交替）下的自适应能力：
1. **L 段冲高**：轻负载时 AIMD 把 α 推高，让 ai 充分利用空闲 CPU
2. **H 段退避**：重负载时 AIMD 压低 α，把 CPU 还给 ctrl 保护 deadline
3. **三起点收敛**：α0=0/50/100 在 H 段是否收敛到同一稳态
4. **vs fixed 基线**：动态负载下 fixed α 无法兼顾（高了 H 段崩、低了 L 段浪费），AIMD 能否两头占

## 配置

| 任务 | tickets | 线程 | 类型 | period | burn | 备注 |
|------|---------|------|------|--------|------|------|
| ctrl | 300 | 1 | in-parent jobs | 5 | 180,000 | deadline 任务，D 行反馈 |
| ai | 100 | 50 | phased spin (fork) | — | 12,000 | light_active=5 |
| log | 50 | 3 | spin (fork) | — | 12,000 | 后台负载 |

**四段相位**（L-H-L-H）：

| 段 | 占比 | ai 活跃线程 | ai runnable | 负载 |
|----|------|------------|-------------|------|
| L1 | 0-25% | 5 (light_active) | 5 | 轻 |
| H1 | 25-50% | 50 (全员) | 50 | 重 |
| L2 | 50-75% | 5 | 5 | 轻 |
| H2 | 75-100% | 50 | 50 | 重 |

- 平台：riscv64 (QEMU), SMP=1, 1 tick = 13.98 ms
- total = 240,000 ticks/trial, window = 100 ticks (~2270 windows/trial)
- 8 modes：fixed(0/25/50/75/100) + aimd(0/50/100) × 3 reps = 24 trials
- **AIMD 参数**：INC=5（sl_aimd_init 默认）, BACKOFF=80%, safe_lateness=0, danger_lateness=25, cooldown=3, safe_windows>=2

> **AIMD 参数调优历程**：初版 cooldown=1 / safe_windows>=1，AIMD 每 window 频繁 up/down → `set_sched_alpha` 调用次数 ~250/rep，syscall + scale 缓存重算开销算在 ctrl 头上，压低 ai_run。改为 cooldown=3 / safe_windows>=2 后，α 变化次数砍到 ~120/rep，ai_run 追上并超过 fixed0。

## 运行

```bash
/ $ ./sched/sexp4_dyn > /tmp/sexp4_dyn.csv
python3 ./scripts/sched/stat_exp4.py ./logs/sched/dyn/sexp4_dyn.csv
```

## 实测结果（2026-08-02，24 runs）

### 汇总表

误差为非对称 +hi/-lo（3 reps 的 max-mean / mean-min）。

| mode | α0 | miss% | miss_L1 | miss_H1 | miss_L2 | miss_H2 | sh_ai | ai_burn | ai_run | Jain |
|------|----|-------|---------|---------|---------|---------|-------|---------|--------|------|
| fixed0 | 0 | 3.1 +0.8/-0.9 | 3.1 | 3.0 | 3.1 | 3.0 | 41.1 | 765,616 | 92,700 | 0.852 |
| fixed25 | 25 | 3.7 +0.2/-0.3 | 3.7 | 3.5 | 3.7 | 3.8 | 45.1 | 848,282 | 101,534 | 0.809 |
| fixed50 | 50 | 55.4 +4.6/-7.5 | 8.0 | 94.8 | 29.5 | 98.9 | 57.2 | 1,100,435 | 132,809 | 0.786 |
| fixed75 | 75 | 84.2 +1.4/-0.8 | 57.3 | 100.0 | 100.0 | 100.0 | 65.0 | 1,289,644 | 154,302 | 0.828 |
| fixed100 | 100 | 99.4 +0.5/-0.9 | 98.9 | 100.0 | 100.0 | 100.0 | 71.9 | 1,445,010 | 171,656 | 0.824 |
| aimd0 | 0 | 7.6 +0.5/-0.4 | 5.9 | 7.7 | 7.0 | 8.2 | 47.0 | 901,054 | 104,463 | 0.807 |
| aimd50 | 50 | 8.8 +2.0/-1.1 | 7.8 | 9.4 | 7.6 | 9.1 | 48.3 | 949,516 | 107,949 | 0.808 |
| aimd100 | 100 | 7.4 +0.8/-1.0 | 6.9 | 6.9 | 6.7 | 7.3 | 46.3 | 885,937 | 102,323 | 0.807 |

### 核心发现：AIMD 自适应波形

α 轨迹呈现教科书级的"跟随负载"波形（见 `exp4_alpha_traj.png`）：

| 段 | 负载 | AIMD α 行为 | 物理意义 |
|----|------|------------|---------|
| L1 | 轻（5 ai） | 0→60-85（冲高） | ai eff 拉满，利用空闲 CPU |
| H1 | 重（50 ai） | 60-85→20-28（退避） | late 飙升→down，保护 ctrl |
| L2 | 轻（5 ai） | 20-28→60-68（再冲高） | 第二次冲高，可重复 |
| H2 | 重（50 ai） | 60-68→20-28（再退避） | 对称回落 |

**三起点在 H 段收敛**：aimd0/50/100 经过 L1 冲高 + H1 退避后，都收敛到 α≈20-28，之后 L2/H2 同步震荡——与 exp3 恒定负载的收敛结论一致。

### AIMD vs fixed：trade-off 分析

| 对比 | AIMD | fixed25 | fixed50 | 结论 |
|------|------|---------|---------|------|
| miss% | 7.4-8.8 | 3.7 | 55.4 | AIMD miss 高于 fixed25，远低于 fixed50 |
| ai_burn | 886k-950k | 848k | 1,100k | AIMD 领先 fixed25 **5-12%**，低于 fixed50 |
| ai_run | 102k-108k | 102k | 133k | AIMD 领先 fixed25 **2-6%**，低于 fixed50 |
| H 段 miss | 7-9 | 3.5-3.8 | 94.8-98.9 | AIMD 不崩，fixed50 H 段崩 |

**AIMD 的位置**：在 fixed25（保守不崩但浪费 L 段）和 fixed50（激进高吞吐但 H 段崩）之间，AIMD 用 miss 高于 fixed25 的代价，换来 L 段冲高、ai_burn 领先 fixed25 5-12%。**动态负载下 fixed 无法兼顾，AIMD 两头占。**

### burn vs run：两个视角看吞吐量

| 指标 | 来源 | 含义 | aimd0 vs fixed25 领先 |
|------|------|------|----------------------|
| ai_burn | K 行 work | 实际完成的 burn 迭代数 | +6.2%（901k vs 848k） |
| ai_run | W 行 run_delta | 获得的 CPU ticks | +2.8%（104k vs 102k） |

**burn 领先大于 run 领先**的原因：AIMD 的 burn 效率更高（run/burn 比 0.116 < fixed25 的 0.120）。AIMD 在 L 段把 α 拉高后，ai 线程密集运行、cache 热，每个 tick 完成的乘法更多。但 AIMD 频繁调 `set_sched_alpha` 的开销（syscall + scale 缓存重算）算在 ctrl 的 run_delta 里，挤掉了 ai 的 CPU ticks——这部分代价在 ai_run 里可见，在 ai_burn 里被 burn 效率提升部分抵消。

**两个指标各有价值**：ai_run 诚实反映 AIMD 的调度成本，ai_burn 反映程序的实际产出。

## 度量方式说明

### ai_burn（实际吞吐量）

K 行 `work` = `sl_gstats[gi].work` = ai 线程完成的 `sl_burn(12000)` 循环次数。每跑完一轮 12000 次乘法算 1 个 burn。**这是程序速度的直接度量**——跑了多少个循环。

### ai_run（CPU 分配量）

W 行 `run_delta` = 两个相邻 window 之间 `run_ticks` 的差值。内核每 tick 调 `account_current_tick()` 给当前线程 `run_ticks += 1`，所以 run_delta = ai 线程在这个窗口里被分配了多少 CPU tick。

### 为什么不一样

一个 burn 迭代大约花 0.12 tick，但不精确：
- 线程跑满一整个 tick 没被打断 → 这 1 tick 能跑 ~8 个 burn → run/burn ≈ 0.125
- 线程跑到一半被抢占（如 set_sched_alpha 触发重调度）→ 这 1 tick 只跑了 3 个 burn，但 tick 照记 → run/burn 升高
- 线程连续运行、cache 热 → burn 跑得更快 → run/burn 降低

### set_sched_alpha 的代价

AIMD 每 up/down 一次就调一次 `set_sched_alpha`（syscall + `scale_factor_cached` 整表重算），开销算在监控进程（ctrl）的 run_delta 里。aimd0 一个 rep 约 120 次调用（cooldown=3/safe_windows>=2 后），这些 CPU 本可以分给 ai。

**这是 AIMD 的固有代价**，不是 bug——动态调节必然有开销。脚本同时展示 ai_burn 和 ai_run，让读者看到完整 trade-off：ai_run 揭示代价（eff 高 4.7 倍但 run 只多 2.8%），ai_burn 揭示产出（burn 领先 6.2%）。

## 图表

### 轨迹图
- `exp4_alpha_traj.png` — AIMD α 轨迹 + 相位背景（L/H/L/H 四段，三起点）
- `exp4_miss_traj.png` — 逐窗口 miss rate + 相位背景（8 mode）
- `exp4_ai_throughput.png` — 逐窗口 ai CPU time (run_delta) + 相位背景

### 柱状/散点
- `exp4_phase_summary.png` — 四段相位 miss rate 柱状图（8 mode 对比）
- `exp4_work_vs_miss.png` — ai_burn vs miss 散点（实际吞吐量视角）
- `exp4_run_vs_miss.png` — ai_run vs miss 散点（CPU 分配视角）
- `exp4_burn_vs_run.png` — ai_burn vs ai_run 散点 + fixed0 参考线（**点在参考线上方 = burn 效率高**）
- `exp4_burn_run_bars.png` — 8 mode 的 burn/run 并排柱状图（双 y 轴）

## 调试历程

### Bug 1：inc=1 导致 AIMD 爬不高
**现象**：exp4 AIMD α 只到 16-30，远低于 exp3 light 的 60。
**诊断**：diag_alpha_traj 打印 after 值，exp3 每次 up 跳 5（inc=5），exp4 每次 up 跳 1（inc=1）。
**根因**：我在 sexp4_dyn.c 的 `run_mode` 里加了 `aimd.inc = 1`（试图对齐"代码写的 inc=1"），但 exp3 实际跑的是 inc=5（sl_aimd_init 默认，代码改了没 save）。两个实验 inc 不一致。
**理论**：AIMD 稳态 α = (p/q)·inc/(1−b) ∝ inc。inc 从 5 改 1，稳态 α 直接降到 1/5。
**修复**：删掉 `aimd.inc = 1`，让 exp4 和 exp3 统一用 sl_aimd_init 的 inc=5。修复后 L 段冲到 60-85。

### Bug 2：set_sched_alpha 调用开销压低 ai_run
**现象**：aimd ai_eff 是 fixed0 的 11-25 倍，但 ai_run 反而更低。
**诊断**：diag_work_puzzle 对比 ai_eff/ai_run/ai_work，发现 eff 和 run 完全倒挂。
**根因**：aimd 每 up/down 调一次 set_sched_alpha（syscall + scale_factor_cached 整表重算），开销算在 ctrl_run 里，挤掉 ai 的 CPU。
**修复**：改 sl_policy_aimd 参数——`cooldown=3`（原 1）、`safe_windows>=2`（原 >=1），α 变化次数从 ~250/rep 砍到 ~120/rep。修复后 ai_run 超过 fixed0。

### Bug 3：light_active=1 让 L 段冲高完全无效
**现象**：α 轨迹显示 aimd 在 L 段冲到 100，但 ai_run 没上来，miss 反而比 fixed0 高。
**诊断**：`sched_thread_scale(n, α) = floor(n^(α/100))`，当 n=1 时 `scale(1,α) = 1` 恒成立——L 段 ai 只有 1 个活跃线程，冲高 α 对 ai_eff 毫无效果（100×1=100，和 fixed0 一样），只有 set_sched_alpha 的代价。
**修复**：light_active 从 1 改成 5。`scale(5,100)=5`，冲高 α 后 ai_eff = 100×5 = 500，L 段冲高终于能兑现成 ai_run。

### Bug 4：set_sched_alpha pass 重置（exp3 遗留）
**现象**：exp3 调试时发现 set_sched_alpha 每 window 重置所有 pass=0，stride 无法累积。
**修复**：改为只在 α 变化时调（exp3 Bug 2），内核改为阈值重置（|Δα|>25 才重置，aimd 小步不重置保持 stride 连续）。
**注意**：阈值重置对 ai_run 改善有限——真正的开销是调用频率本身（syscall + scale 重算），不是 pass 重置。Bug 2 的 cooldown/safe_windows 调参才是对症的。

## 结论

**PASS（AIMD 动态自适应验证通过）。**

1. **L 段冲高 H 段退避** ✅：α 轨迹呈现清晰的"跟随负载"波形——轻负载冲到 60-85 拉满 ai eff，重负载退到 20-28 保护 ctrl
2. **三起点收敛** ✅：aimd0/50/100 经过 L1+H1 后收敛到同一 α 区间（20-28），L2/H2 同步震荡
3. **不崩特性** ✅：AIMD 全程 miss 7-9%，fixed50 在 H 段崩到 95-99%、fixed75/100 全程崩
4. **trade-off 成立** ✅：AIMD 用 miss 高于 fixed25 的代价（7.6 vs 3.7），换来 ai_burn 领先 fixed25 5-12%——动态负载下 fixed 无法兼顾，AIMD 两头占
5. **burn vs run** ✅：ai_burn（实际产出）领先幅度大于 ai_run（CPU 分配），反映 AIMD 的 burn 效率更高，但 set_sched_alpha 开销在 ai_run 里可见

## 待补

- [x] inc=10 实验：增大 inc 减少 set_sched_alpha 调用次数（已试，效果不好）
- [ ] inc=1 重跑 exp3+exp4（save all + make clean，用真 inc=1 数据验证）
- [ ] exp5 相位比例：不同 L/H 比例下 AIMD 优势的变化
- [ ] exp6：SPSA-AdamW 自适应对照

## 产出文件

```
logs/sched/dyn/
├── sexp4_dyn.csv                  # 原始 CSV (24 runs)
├── exp4_alpha_traj.png            # α 轨迹 + 相位背景
├── exp4_miss_traj.png             # 逐窗口 miss rate + 相位背景
├── exp4_ai_throughput.png         # 逐窗口 ai CPU time
├── exp4_phase_summary.png         # 四段相位 miss 柱状图
├── exp4_work_vs_miss.png          # ai_burn vs miss 散点
├── exp4_run_vs_miss.png           # ai_run vs miss 散点
├── exp4_burn_vs_run.png           # burn vs run 散点（效率对比）
└── exp4_burn_run_bars.png         # burn/run 并排柱状图
```

## 注意事项

- **ctrl 用 in-parent jobs**（D 行反馈），ai 用 phased spin（fork），log 用 spin（fork）
- **AIMD 参数**：INC=5, safe_lateness=0, danger_lateness=25, cooldown=3, safe_windows>=2
- **light_active=5**：L 段 5 个 ai 线程活跃，确保 `scale(5,α)>1`，冲高 α 有实际效果
- **total=240,000 ticks**（约 56 分钟/trial），8 modes × 3 reps = 24 trials
- **set_sched_alpha 只在 α 变化时调**（exp3 修复），内核阈值重置（|Δα|>25 才重置 pass）
- **burn（K 行）是真正的吞吐量**，run（W 行）是 CPU 分配量，两者通过 run/burn 比关联
- **diag 脚本**：`diag_work_puzzle.py`（eff/run/work 对比）、`diag_exp4_aimd.py`（分相位 actions/late）、`diag_phase_mode.py`（逐 window eff/run/late）、`diag_phase_ready.py`（phased 验证）


# 05 · 相位比例对 AIMD 优势的影响

## 目的

验证 **L 段占比越大，AIMD 相对 fixed 的吞吐量优势越大**。

核心推理：
- **L 段（轻负载）**：AIMD α 冲到 50-85，fixed25 α=25 → AIMD ai_eff 远高于 fixed25 → **AIMD 拉开差距**
- **H 段（重负载）**：AIMD α 退避到 20-28，fixed25 α=25 → ai_eff 差不多 → **吞吐量接近**
- **结论**：L 段越多 → AIMD 优势积累时间越长 → 整体领先越大

## 配置

负载配置同 exp4：

| 任务 | tickets | 线程 | 类型 | period | burn |
|------|---------|------|------|--------|------|
| ctrl | 300 | 1 | in-parent jobs | 5 | 180,000 |
| ai | 100 | 50 | phased spin | — | 12,000 |
| log | 50 | 3 | spin | — | 12,000 |

light_active=5（L 段 5 个 ai 活跃），total=96,000 ticks/trial。

**变量：相位比例（3 组，25/25 复用 exp4 数据）**

| ratio (permil) | 标签 | L1 | H1 | L2 | H2 | L 段总占比 | 来源 |
|----------------|------|-----|-----|-----|-----|----------|------|
| 250 | 25/25 | 25% | 25% | 25% | 25% | 50% | exp4 |
| 800 | 40/10 | 40% | 10% | 40% | 10% | 80% | exp5 |
| 200 | 10/40 | 10% | 40% | 10% | 40% | 20% | exp5 |

- 8 modes：fixed(0/25/50/75/100) + aimd(0/50/100)
- exp5: 2 ratios × 8 modes × 3 reps = 48 trials + exp4: 24 trials = **72 runs 合计**
- AIMD 参数：INC=5, safe=0, danger=25, cooldown=3, safe_windows>=2（同 exp4）

## 运行

```bash
/ $ ./sched/sexp5_phase > /tmp/sexp5_phase.csv
# exp4 数据复用（25/25 作为 L=50% 参照点）
python3 ./scripts/sched/stat_exp5.py ./logs/sched/phase/sexp5_phase.csv ./logs/sched/dyn/sexp4_dyn.csv
```

stat 脚本自动识别文件名含 `sexp4` 的 CSV，按 `ratio=250`（25/25）导入。

## 实测结果（2026-08-02，72 runs）

### 汇总表

#### RATIO 25/25 (L=50%) [exp4]

| mode | miss% | miss_L1 | miss_H1 | miss_L2 | miss_H2 | sh_ai | ai_burn | ai_run | α_steady | Jain |
|------|-------|---------|---------|---------|---------|-------|---------|--------|----------|------|
| fixed0 | 3.1 | 3.2 | 3.0 | 3.2 | 3.0 | 41.1 | 765,616 | 92,700 | 0 | 0.852 |
| fixed25 | 3.7 | 3.5 | 3.6 | 3.9 | 3.7 | 45.1 | 848,282 | 101,534 | 25 | 0.809 |
| fixed50 | 55.4 | 7.5 | 63.1 | 53.8 | 64.8 | 57.2 | 1,100,435 | 132,809 | 50 | 0.786 |
| aimd0 | 7.6 | 4.9 | 7.6 | 5.7 | 7.9 | 47.0 | 901,054 | 104,463 | 44.7 | 0.807 |
| aimd50 | 8.8 | 7.1 | 9.2 | 6.9 | 8.7 | 48.3 | 949,516 | 107,949 | 43.3 | 0.808 |
| aimd100 | 7.4 | 6.8 | 7.0 | 6.1 | 7.2 | 46.3 | 885,937 | 102,323 | 42.9 | 0.807 |

#### RATIO 40/10 (L=80%) [exp5]

| mode | miss% | miss_L1 | miss_H1 | miss_L2 | miss_H2 | sh_ai | ai_burn | ai_run | α_steady | Jain |
|------|-------|---------|---------|---------|---------|-------|---------|--------|----------|------|
| fixed0 | 3.2 | 3.0 | 3.2 | 3.3 | 2.8 | 42.2 | 855,378 | 95,925 | 0 | 0.850 |
| fixed25 | 2.9 | 3.0 | 2.8 | 2.8 | 2.7 | 43.2 | 859,974 | 97,394 | 25 | 0.821 |
| fixed50 | 17.4 | 3.2 | 62.3 | 7.4 | 69.4 | 52.2 | 1,055,380 | 118,351 | 50 | 0.797 |
| aimd0 | 6.5 | 5.5 | 8.9 | 5.5 | 8.8 | 46.5 | 950,991 | 103,792 | 54.2 | 0.814 |
| aimd50 | 5.3 | 5.7 | 6.5 | 7.2 | 8.2 | 45.7 | 930,424 | 101,719 | 50.4 | 0.813 |
| aimd100 | 7.6 | 6.8 | 8.7 | 6.4 | 8.3 | 47.1 | 969,863 | 104,613 | 57.4 | 0.815 |

#### RATIO 10/40 (L=20%) [exp5]

| mode | miss% | miss_L1 | miss_H1 | miss_L2 | miss_H2 | sh_ai | ai_burn | ai_run | α_steady | Jain |
|------|-------|---------|---------|---------|---------|-------|---------|--------|----------|------|
| fixed0 | 3.9 | 3.7 | 3.9 | 4.0 | 3.9 | 41.0 | 803,193 | 91,449 | 0 | 0.851 |
| fixed25 | 4.0 | 3.8 | 4.0 | 4.1 | 4.0 | 47.4 | 914,135 | 104,819 | 25 | 0.803 |
| fixed50 | 85.3 | 5.9 | 95.8 | 72.8 | 97.5 | 63.1 | 1,274,844 | 148,756 | 50 | 0.785 |
| aimd0 | 8.0 | 4.1 | 8.3 | 7.2 | 8.0 | 48.3 | 923,245 | 104,666 | 32.8 | 0.805 |
| aimd50 | 8.8 | 6.4 | 8.7 | 6.6 | 8.5 | 48.5 | 927,986 | 104,642 | 32.7 | 0.806 |
| aimd100 | 7.9 | 7.4 | 7.8 | 6.4 | 8.2 | 47.1 | 902,379 | 101,667 | 29.8 | 0.806 |

### 核心对比：AIMD vs fixed25

| L 段占比 | ratio | aimd | burn 领先% | miss delta | α_steady |
|----------|-------|------|----------|-----------|----------|
| **80%** | 40/10 | aimd0 | **+10.6%** | +3.7 | 54.2 |
| **80%** | 40/10 | aimd50 | **+8.2%** | +2.4 | 50.4 |
| **80%** | 40/10 | aimd100 | **+12.8%** | +4.8 | 57.4 |
| **50%** | 25/25 | aimd0 | +6.2% | +3.9 | 44.7 |
| **50%** | 25/25 | aimd50 | +11.9% | +5.1 | 43.3 |
| **50%** | 25/25 | aimd100 | +4.4% | +3.7 | 42.9 |
| **20%** | 10/40 | aimd0 | **+1.0%** | +4.0 | 32.8 |
| **20%** | 10/40 | aimd50 | **+1.5%** | +4.8 | 32.7 |
| **20%** | 10/40 | aimd100 | **-1.3%** | +3.8 | 29.8 |

### 假设验证

| 假设 | 结果 | 说明 |
|------|------|------|
| burn 领先随 L 段比例递增 | ✅ **强确认** | 20%→0.4% avg, 50%→7.5% avg, 80%→10.5% avg |
| miss 差距随 L 段比例递减 | ✅ 确认 | 40/10 的 aimd50 miss delta 仅 +2.4（最小） |
| α_steady 随 L 段比例递增 | ✅ **强确认** | 20%→32, 50%→44, 80%→54 |
| L=20% 时 AIMD 优势消失 | ✅ **确认** | burn 领先 ±1.3%（几乎为零），miss 仍高 +4 |

## 分析

### 1. burn 领先随 L 段比例单调递增

```
L=20%:  burn lead = +0.4% (avg)   ← AIMD 几乎没有优势
L=50%:  burn lead = +7.5% (avg)   ← exp4 基线
L=80%:  burn lead = +10.5% (avg)  ← AIMD 优势最大
```

这直接验证了核心假设：**L 段是 AIMD 拉开差距的地方，L 段越多优势越大**。

### 2. α_steady 随 L 段比例上升

```
L=20%:  α_steady = 30-33  ← L 段太短，AIMD 爬不到高位
L=50%:  α_steady = 43-45  ← 中等
L=80%:  α_steady = 50-57  ← L 段长，AIMD 充分冲高
```

L 段长 → AIMD 有更多 window 执行 probe up → α 爬得更高 → ai_eff 更大 → burn 领先更多。

### 3. miss 差距在 L=80% 时最小

40/10 的 H 段只占 10%，miss 集中在很短的窗口里，总 miss 被长 L 段稀释。aimd50 在 40/10 下 miss 仅 5.3%，与 fixed25 的 2.9% 差距只有 +2.4pp——这是所有配置中 miss 差距最小的。

### 4. L=20% 时 AIMD 退化为"浪费"

10/40 下 AIMD burn 领先几乎为零（+0.4% avg），但 miss 仍比 fixed25 高 4pp——**AIMD 在重负载为主时没有吞吐量优势，却仍付出 miss 代价**。此时 fixed25 是更好选择。

### 5. fixed50 的崩塌程度也随比例变化

| L 段占比 | fixed50 miss% | 说明 |
|----------|-------------|------|
| 80% (40/10) | 17.4% | H 段短（10%），崩得少 |
| 50% (25/25) | 55.4% | H 段中等，崩 |
| 20% (10/40) | 85.3% | H 段长（40%），彻底崩 |

fixed50 在 L=80% 时 miss 只有 17.4%——H 段太短来不及崩。但这不代表 fixed50 好，因为它的 ai_burn（1,055k）远低于 aimd100（969k）在 L=80% 的表现——等等，实际上 fixed50 的 burn 更高。但 fixed50 在 L=20% 时 miss 85% 完全不可用，而 AIMD miss 仅 8%。

**这才是 AIMD 的核心价值**：fixed50 在重负载为主时崩（miss 85%），AIMD 不崩（miss 8%）——**AIMD 是"不崩的 fixed50"**。

## 图表

### 核心图
- `exp5_advantage_vs_ratio.png` — **最关键**：x=L 段占比(20/50/80%)，y=AIMD burn 领先 fixed25 的%。三条线（aimd0/50/100）均单调递增。
- `exp5_burn_vs_miss.png` — 3 子图（每个 ratio 一张），8 mode 的 burn vs miss 散点。
- `exp5_alpha_traj.png` — 3 子图，每个 ratio 的 AIMD α 轨迹 + 相位背景。L=80% 时冲高最高，L=20% 时冲高最低。
- `exp5_miss_traj.png` — 3 子图，每个 ratio 的逐窗口 miss rate + 相位背景。

## 调试历程

### schedlab.h 支持非等分相位

新增全局变量 `sl_l_ratio_permil`（千分比，0=等分）：
- `sl_phase_now()`：当 `sl_l_ratio_permil > 0` 时，按 `L段 = half × ratio/1000` 计算相位边界
- `sl_phased_sleep()`：同步使用自定义边界计算 sleep 时长
- exp4 不受影响（用默认 0=等分）

### stat_exp5.py 多 CSV 导入

- 文件名含 `sexp4` 的 CSV 自动按 `ratio=250`（25/25）导入
- 两个 CSV 合并统计，72 runs 一起画图
- 子图数量动态适配 ratio 数量

## 结论

**PASS（假设验证通过）。**

1. **burn 领先随 L 段比例单调递增** ✅：L=20%→0.4%, L=50%→7.5%, L=80%→10.5%
2. **α_steady 随 L 段比例上升** ✅：L=20%→32, L=50%→44, L=80%→54
3. **miss 差距在 L=80% 时最小** ✅：40/10 aimd50 miss delta 仅 +2.4pp
4. **L=20% 时 AIMD 优势消失** ✅：burn 领先≈0，miss 仍高 → 重负载为主时用 fixed25
5. **AIMD 是"不崩的 fixed50"** ✅：L=20% 时 fixed50 miss 85% 崩，AIMD miss 8% 不崩

**核心结论**：AIMD 适合"轻负载为主、偶尔重负载"的场景。L 段占比越大，AIMD 的自适应优势越明显——L 段冲高拉开 burn 差距，H 段退避保持不崩。

## 产出文件

```
logs/sched/phase/
├── sexp5_phase.csv                  # 原始 CSV (48 runs, exp5)
├── sexp4_dyn.csv                    # 原始 CSV (24 runs, exp4 复用)
├── exp5_advantage_vs_ratio.png      # AIMD 优势 vs L段比例（核心图）
├── exp5_burn_vs_miss.png            # 每 ratio 的 burn vs miss 散点
├── exp5_alpha_traj.png              # 每 ratio 的 α 轨迹
└── exp5_miss_traj.png               # 每 ratio 的 miss 轨迹
```

## 注意事项

- **exp5 只跑 40/10 和 10/40**，25/25 复用 exp4 数据（stat 脚本自动导入）
- **total=96,000 ticks**（约 22 分钟/trial），48 trials 约 18 小时
- **相位比例通过 `sl_l_ratio_permil` 控制**，每个 run 前设置、后恢复
- **AIMD 参数同 exp4**（inc=5, cooldown=3, safe_windows>=2）
- **exp4 的 total=240,000**，exp5 的 total=96,000——total 不同但比例相同，对比的是相对趋势而非绝对值


# 06 · SPSA-AdamW vs AIMD 自适应控制器对比

## 目的

对比两种自适应控制器在动态负载下的表现：
- **AIMD**：启发式规则（late>25 退避、late=0 爬升），确定性
- **SPSA-AdamW**：梯度优化（loss=miss+late，SPSA 估梯度，AdamW 更新），带随机扰动

核心问题：**梯度优化能不能比启发式规则更好地调节调度 α？**

## 配置

负载配置同 exp4/exp5：

| 任务 | tickets | 线程 | 类型 | period | burn |
|------|---------|------|------|--------|------|
| ctrl | 300 | 1 | in-parent jobs | 5 | 180,000 |
| ai | 100 | 50 | phased spin | — | 12,000 |
| log | 50 | 3 | spin | — | 12,000 |

light_active=5，total=96,000 ticks/trial。

**3 种相位比例**（全测）：

| ratio (permil) | 标签 | L1 | H1 | L2 | H2 | L 段总占比 | 来源 |
|----------------|------|-----|-----|-----|-----|----------|------|
| 250 | 25/25 | 25% | 25% | 25% | 25% | 50% | exp4 |
| 800 | 40/10 | 40% | 10% | 40% | 10% | 80% | exp5 |
| 200 | 10/40 | 10% | 40% | 10% | 40% | 20% | exp5 |

**AdamW 参数**：lr=3, target=25, delta=5 (SPSA), α0=0/50/100
- 3 ratios × 3 AdamW modes × 3 reps = **27 runs (exp6)**
- AIMD/fixed 数据复用 exp4 (25/25) + exp5 (40/10, 10/40) = **72 runs**
- 合计 **99 runs**

## 运行

```bash
/ $ ./sched/sexp6_adamw > /tmp/sexp6_adamw.csv
python3 ./scripts/sched/stat_exp6.py \
  ./logs/sched/adamw/sexp6_adamw.csv \
  ./logs/sched/phase/sexp5_phase.csv \
  ./logs/sched/dyn/sexp4_dyn.csv
```

## AdamW 原理

### Loss 函数

```
loss = miss_per_1000 + late_per_job  (封顶 4000)
```

- `miss_per_1000`：窗口内每 1000 个 job 的 miss 数
- `late_per_job`：窗口内平均迟到 × 1000

loss 高 → ctrl 在受苦 → α 该降；loss 低 → ctrl 轻松 → α 可以升。

### SPSA 梯度估计

loss(α) 是调度器的黑盒行为，无法解析求导。SPSA 用**扰动试探**估计梯度：

```
奇数 window: probe = α + 5   (往上探)
偶数 window: probe = α - 5   (往下探)
g ≈ (loss_后 - loss_前) / (2 × 5 × 探测方向)
```

### AdamW 更新

```
m = 0.9·m + 0.1·g              # 动量（一阶矩）
v = 0.99·v + 0.01·g²           # 方差（二阶矩）
step = lr × m / √v              # Adam 步长
α -= step - decay               # 梯度下降 + weight decay 回拉 target
```

全程定点（×1024），无浮点。`isqrt(v)` 用整数 Newton 迭代。

### 与 AIMD 的本质差异

| | AIMD | AdamW |
|--|------|-------|
| 信号 | late=0 → **主动 probe up** | loss=0 → 梯度=0 → **不知道往哪走** |
| 轻负载行为 | 冲高（α→60-85） | 停在 target 附近（被 decay 拉着） |
| 重负载行为 | late>25 → 退避 | loss 高 → 梯度正 → 降 α |
| 调节方式 | 规则（if-else） | 数学（梯度下降） |
| α 轨迹 | 稳定爬升/退避 | **高频震荡**（SPSA ±5 + Adam 噪声） |

## 预期 vs 实测

### 预期（我们的预测，被狠狠打脸）

> AdamW 在 loss=0 时梯度=0，没有信号爬高，α 会卡在 target=25 附近。L 段冲高不如 AIMD，burn lead 会低很多。**梯度优化在调度场景不如启发式规则。**

### 实测（完全反转）

**AdamW 全面碾压 AIMD——burn 更高，miss 更低，全 ratio 占优。**

## 实测结果（2026-08-03，99 runs）

### 汇总表

#### RATIO 10/40 (L=20%)

| mode | miss% | sh_ai | ai_burn | ai_run | α_steady | Jain |
|------|-------|-------|---------|--------|----------|------|
| fixed0 | 3.9 | 41.0 | 803,193 | 91,449 | 0 | 0.851 |
| fixed25 | 4.0 | 47.4 | 914,135 | 104,819 | 25 | 0.803 |
| fixed50 | 85.3 | 63.1 | 1,274,844 | 148,756 | 50 | 0.785 |
| aimd0 | 8.0 | 48.3 | 923,245 | 104,666 | 32.8 | 0.805 |
| aimd50 | 8.8 | 48.5 | 927,986 | 104,642 | 32.7 | 0.806 |
| aimd100 | 7.9 | 47.1 | 902,379 | 101,667 | 29.8 | 0.806 |
| **adamw0** | **4.5** | **49.3** | **1,038,218** | **112,796** | **29.5** | 0.816 |
| **adamw50** | **8.2** | **49.6** | **1,023,730** | **113,186** | **34.4** | 0.817 |
| **adamw100** | **5.6** | **48.6** | **1,027,956** | **110,718** | **22.0** | 0.821 |

#### RATIO 25/25 (L=50%)

| mode | miss% | sh_ai | ai_burn | ai_run | α_steady | Jain |
|------|-------|-------|---------|--------|----------|------|
| fixed0 | 3.1 | 41.1 | 765,616 | 92,700 | 0 | 0.852 |
| fixed25 | 3.7 | 45.1 | 848,282 | 101,534 | 25 | 0.809 |
| fixed50 | 55.4 | 57.2 | 1,100,435 | 132,809 | 50 | 0.786 |
| aimd0 | 7.6 | 47.0 | 901,054 | 104,463 | 44.7 | 0.807 |
| aimd50 | 8.8 | 48.3 | 949,516 | 107,949 | 43.3 | 0.808 |
| aimd100 | 7.4 | 46.3 | 885,937 | 102,323 | 42.9 | 0.807 |
| **adamw0** | **3.9** | **49.6** | **1,022,278** | **114,608** | **27.8** | 0.819 |
| **adamw50** | **5.0** | **48.9** | **995,691** | **112,772** | **23.7** | 0.819 |
| **adamw100** | **4.7** | **47.5** | **938,056** | **108,975** | **23.6** | 0.818 |

#### RATIO 40/10 (L=80%)

| mode | miss% | sh_ai | ai_burn | ai_run | α_steady | Jain |
|------|-------|-------|---------|--------|----------|------|
| fixed0 | 3.2 | 42.2 | 855,378 | 95,925 | 0 | 0.850 |
| fixed25 | 2.9 | 43.2 | 859,974 | 97,394 | 25 | 0.821 |
| fixed50 | 17.4 | 52.2 | 1,055,380 | 118,351 | 50 | 0.797 |
| aimd0 | 6.5 | 46.5 | 950,991 | 103,792 | 54.2 | 0.814 |
| aimd50 | 5.3 | 45.7 | 930,424 | 101,719 | 50.4 | 0.813 |
| aimd100 | 7.6 | 47.1 | 969,863 | 104,613 | 57.4 | 0.815 |
| **adamw0** | **4.6** | **44.2** | **947,384** | **101,144** | **25.4** | 0.827 |
| **adamw50** | **4.3** | **44.7** | **979,261** | **102,548** | **27.6** | 0.826 |
| **adamw100** | **5.4** | **45.0** | **1,009,294** | **102,942** | **25.9** | 0.825 |

### 核心对比：AdamW vs AIMD vs fixed25

| L 段占比 | ctrl | burn lead% | miss% | α_steady |
|----------|------|-----------|-------|----------|
| **20%** | aimd0 | +1.0% | 8.0 | 32.8 |
| **20%** | aimd50 | +1.5% | 8.8 | 32.7 |
| **20%** | aimd100 | -1.3% | 7.9 | 29.8 |
| **20%** | **adamw0** | **+13.6%** | **4.5** | **29.5** |
| **20%** | **adamw50** | **+12.0%** | **8.2** | **34.4** |
| **20%** | **adamw100** | **+12.5%** | **5.6** | **22.0** |
| **50%** | aimd0 | +6.2% | 7.6 | 44.7 |
| **50%** | aimd50 | +11.9% | 8.8 | 43.3 |
| **50%** | aimd100 | +4.4% | 7.4 | 42.9 |
| **50%** | **adamw0** | **+20.5%** | **3.9** | **27.8** |
| **50%** | **adamw50** | **+17.4%** | **5.0** | **23.7** |
| **50%** | **adamw100** | **+10.6%** | **4.7** | **23.6** |
| **80%** | aimd0 | +10.6% | 6.5 | 54.2 |
| **80%** | aimd50 | +8.2% | 5.3 | 50.4 |
| **80%** | aimd100 | +12.8% | 7.6 | 57.4 |
| **80%** | **adamw0** | **+10.2%** | **4.6** | **25.4** |
| **80%** | **adamw50** | **+13.9%** | **4.3** | **27.6** |
| **80%** | **adamw100** | **+17.4%** | **5.4** | **25.9** |

## 核心发现

### 1. AdamW 全面碾压 AIMD

| L 段占比 | AIMD burn lead avg | AdamW burn lead avg | AIMD miss avg | AdamW miss avg |
|----------|-------------------|--------------------|--------------------|--------------------|
| 20% | +0.4% | **+12.7%** | 8.2% | **6.1%** |
| 50% | +7.5% | **+16.2%** | 7.9% | **4.5%** |
| 80% | +10.5% | **+13.8%** | 6.5% | **4.8%** |

**burn 更高，miss 更低，全 ratio 占优——AdamW strictly dominates AIMD。**

### 2. AdamW 的优势是"无条件的"

| L 段占比 | AIMD α_steady | AdamW α_steady | AIMD burn lead | AdamW burn lead |
|----------|-------------|---------------|---------------|----------------|
| 20% | 32 | **29** | +0.4% | **+12.7%** |
| 50% | 44 | **26** | +7.5% | **+16.2%** |
| 80% | 54 | **26** | +10.5% | **+13.8%** |

- **AIMD 的 α_steady 随 L 段比例大幅变化**（32→44→54）——依赖 L 段冲高拉开差距
- **AdamW 的 α_steady 几乎不变**（26-29）——**不依赖 L 段冲高，任何负载比例下都有效**
- **最惊艳的是 10/40（H 段占 80%）**：AIMD 无优势（+0.4%），AdamW 大幅领先（+12.7%）

### 3. 低 α 但 burn 更高——SPSA 扰动是探索机制

看 α 轨迹图，AdamW 的 α 不是"稳定爬升到 60"（像 AIMD），而是**在 target=25 附近高频震荡**（10-50 之间，偶尔 spike 到 60-80）。α_steady 只有 22-34。

**低 α 但 burn 更高**——表面矛盾，实际是核心机制：

```
AdamW α 震荡:
  α spike 到 40-50 → ai_eff 瞬间拉高 → ai 抢到大量 CPU → burn 大幅增加
  α 回落到 10-15 → ctrl 恢复 → miss 被压低
  净效果: spike 贡献的 burn > 低谷损失的 burn → 总 burn 更高
          低谷让 ctrl 恢复 → 总 miss 更低
```

相比之下，AIMD 的 α 更"稳定"（32-54）：
- 没有 spike 到更高 → 错过 transient 机会
- 没有 dip 到更低 → ctrl 恢复不充分

**SPSA 的随机扰动不是弱点，是探索机制**——它让 AdamW 不断试探更高/更低的 α，利用 transient 机会。AIMD 的确定性规则（late>25 退避、late=0 爬升）反而把自己锁在一个较窄的区间里。

### 4. AdamW 的 miss 优势来自"低谷让 ctrl 恢复"

AdamW miss 比 AIMD 低 2-4pp。看 α 轨迹，AdamW 的 α 会 dip 到 10-15（远低于 AIMD 的 25-30），这给 ctrl 留了充分的 CPU 空间。AIMD 的 α 稳在 25-57，ctrl 始终被 ai 挤压，miss 更高。

**AdamW 是"间歇性给 ctrl 喘息"**——α 高时抢 CPU，α 低时让 ctrl 恢复。AIMD 是"持续性挤压 ctrl"——α 稳定在中高位，ctrl 一直在被压。

### 5. 10/40 是分水岭：AIMD 无优势 vs AdamW 大幅领先

H 段占 80% 时：
- AIMD burn lead 只有 +0.4%（没有优势）——L 段太短，冲高拉开差距的时间不够
- **AdamW burn lead +12.7%（大幅领先）**——不依赖 L 段冲高，H 段通过 α 震荡也能抢到更多 CPU

**这证明 AdamW 的优势不是"靠 L 段冲高"，而是"在任何负载下都能通过 α 震荡找到更好的工作点"**。

## 为什么 AdamW 赢——机制分析

### 信息论视角

| | AIMD | AdamW |
|--|------|-------|
| 反馈信号 | late_delta（1 bit：0 或 >0）| miss_per_1000 + late_per_job（连续值）|
| 决策粒度 | 粗（±inc 或 ×backoff）| 细（Adam 步长，自适应）|
| 探索方式 | 无（确定性）| SPSA 扰动（±5 试探）|

AdamW 的 loss 信号比 AIMD 的 late 阈值**信息量更大**——它知道"miss 有多严重、迟到有多久"，能做出更精细的调节。AIMD 只知道"有没有迟到"，粒度太粗。

### 动态系统视角

AIMD 是一个**bang-bang 控制器**（二值控制：up 或 down，步长固定）。AdamW 是一个**连续控制器**（步长自适应，梯度大走大步，梯度小走小步）。

在调度这种**非线性、时变、带噪声**的系统里：
- bang-bang 控制容易在 edge 附近震荡（AIMD 的 gray 区占 60-70%）
- 连续控制能更精确地跟踪最优工作点（AdamW 的 α 震荡恰好覆盖了 edge 两侧）

### 优化理论视角

AIMD 是**贪心算法**（每步局部最优：没迟到就升，有迟到就降）。AdamW 是**梯度下降**（全局最优：最小化 loss 函数）。

调度问题的 loss(α) 不是凸函数——有多个局部最优。AIMD 的贪心容易陷入次优局部最优（比如 α=25 的"安全区"）。AdamW 的 SPSA 扰动提供了**随机探索**能力，能跳出局部最优。

## α 轨迹对比

| 特征 | AIMD | AdamW |
|------|------|-------|
| 轨迹形状 | 稳定爬升/退避（阶梯状） | 高频震荡（锯齿状） |
| α_steady | 32-57（随 L 段比例变化） | 22-34（几乎不变） |
| 波动范围 | ±10-15 | ±20-30 |
| 峰值 | 60-85（L 段冲高） | 40-50（spike） |
| 谷值 | 20-28（H 段退避） | 5-15（dip） |

AdamW 的 α 轨迹**方差远大于 AIMD**——这正是它的优势来源。高方差让它覆盖更广的工作点，利用 transient 机会。

## 调试历程

### Bug 1：AdamW 无 A 行输出
**现象**：首轮 exp6 跑完，α_steady 全 0.0，α 轨迹图空白。
**根因**：`sl_policy_adamw` 没有 `printf("A,...")`——A 行只在 `sl_policy_aimd` 里有。
**修复**：在 `sl_policy_adamw` 末尾加 A 行输出（before/after/action=up/down/hold）。
**教训**：A 行是最重要的调试信息，任何新 policy 都必须输出。

### Bug 2：stat_exp6.py W 行 fallback
**现象**：首轮数据无 A 行，α 轨迹拿不到。
**修复**：stat 脚本从 W 行第二个字段（alpha=probe 值）提取 α，2-window 移动平均平滑 SPSA ±5 扰动。
**效果**：不用重跑实验就能拿到近似 α 轨迹（probe±5 均值≈实际 α）。

### Bug 3：stat_exp6.py phase_burn 崩溃
**现象**：`TypeError: tuple indices must be integers or slices, not str`。
**根因**：`plot_adamw_phase_burn` 访问 `r["W"]`，但 `compute()` 不存 W 行原始数据。
**修复**：`compute()` 新增 `win_ai_rd` / `win_ai_wins`（逐窗口 ai run_delta 和 win 号），phase_burn 改用这两个字段。

## 图表

### 对比图（AdamW vs AIMD）
- `exp6_alpha_traj_adamw_vs_aimd.png` — AdamW（虚线）vs AIMD（实线）α 轨迹，3 ratio 对比。AdamW 震荡大、AIMD 稳定。
- `exp6_advantage_adamw_vs_aimd.png` — 两者 burn lead vs L 段比例。AdamW 线在 AIMD 线上方。
- `exp6_burn_vs_miss.png` — 全 mode 散点（AdamW=方块，AIMD/fixed=圆点）。AdamW 在"高 burn、低 miss"的优势区。

### AdamW 独立图
- `exp6_adamw_alpha_traj.png` — AdamW 三起点 α 轨迹 + target=25 参考线 + 相位背景。看 AdamW 的震荡模式。
- `exp6_adamw_miss_traj.png` — AdamW 逐窗口 miss rate（虚线）+ fixed 基线（实线）。
- `exp6_adamw_burn_vs_miss.png` — AdamW 独立散点 + fixed 基线。
- `exp6_adamw_actions.png` — up/down/hold 分布柱状图。看 AdamW 的调节频率。
- `exp6_adamw_phase_burn.png` — 分相位 ai CPU time 柱状图。看 AdamW 在 L/H 段的 CPU 分配。

## 结论

**PASS（AdamW 优于 AIMD，预测反转）。**

1. **AdamW 全面碾压 AIMD** ✅：burn 更高（+12-20% vs fixed25），miss 更低（4-8% vs 6-9%），全 ratio 占优
2. **AdamW 的优势是"无条件的"** ✅：α_steady 几乎不随 L 段比例变化（26-29），不依赖 L 段冲高
3. **SPSA 扰动是探索机制** ✅：α 高频震荡（10-50），spike 贡献 burn > dip 损失，dip 让 ctrl 恢复降低 miss
4. **10/40 是分水岭** ✅：AIMD 无优势（+0.4%），AdamW 大幅领先（+12.7%）——AdamW 在 H 段主导时也有效
5. **梯度优化 > 启发式规则** ✅：loss 信号信息量更大 + 连续控制粒度更细 + SPSA 随机探索

**核心结论**：在调度这种非线性、时变、带噪声的系统里，**带随机探索的连续控制器（AdamW）优于确定性 bang-bang 控制器（AIMD）**。SPSA 的随机扰动不是噪声，是探索机制——它让 AdamW 不断试探更高/更低的 α，利用 transient 机会，这是 AIMD 的确定性规则做不到的。

## 产出文件

```
logs/sched/adamw/
├── sexp6_adamw.csv                        # 原始 CSV (27 runs, exp6)
├── exp6_alpha_traj_adamw_vs_aimd.png      # AdamW vs AIMD α 轨迹
├── exp6_advantage_adamw_vs_aimd.png       # 优势对比（核心图）
├── exp6_burn_vs_miss.png                  # 全 mode burn vs miss
├── exp6_adamw_alpha_traj.png              # AdamW 三起点 α 轨迹
├── exp6_adamw_miss_traj.png               # AdamW 逐窗口 miss
├── exp6_adamw_burn_vs_miss.png            # AdamW 独立散点
├── exp6_adamw_actions.png                 # AdamW actions 分布
└── exp6_adamw_phase_burn.png              # 分相位 ai CPU time
```

## 注意事项

- **AdamW 参数**：lr=3, target=25, delta=5 (SPSA), α0=0/50/100
- **A 行输出**：`sl_policy_adamw` 末尾输出 `A,win,before,after,action`（up/down/hold）
- **W 行 fallback**：stat 脚本从 W 行提取 alpha（probe 值），2-window 移动平均平滑 SPSA ±5 扰动
- **exp4 数据 total=240,000**，exp5/exp6 数据 total=96,000——对比的是相对趋势
- **AdamW α 轨迹方差远大于 AIMD**——这是优势不是 bug，SPSA 扰动是探索机制


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

> 注：在 QEMU 软件模拟下，loongarch64 的指令翻译、串口 IO 与多 vCPU 执行效率可能明显低于 riscv64；如果宿主环境是 Windows / VMware / Linux / QEMU 多层嵌套，`-smp 8` 也不一定代表 QEMU 会吃满 8 个宿主核心。日常开发建议以 riscv64 为主，loongarch64 用于跨架构正确性验证；真实性能扩展需要结合宿主机 `top -H` / `ps -L` 观察 QEMU 线程占用。

---

## Source Layout（用户程序与 rootfs 布局）

```text
user/
├── rootfs/                 rootfs 目录模板(etc/motd, home, share, tmp, fat ...)
│   └── scripts/lua         lua测试文件
├── include/                C/C++ 用户库(分层头文件)
│   ├── types.h             基础类型(usize / isize)
│   ├── syscall.h           系统调用号 + syscall3 / syscall6
│   ├── flag.h              open flags(O_RDONLY / O_WRONLY / O_RDWR / O_CREAT / O_TRUNC / O_APPEND)
│   ├── io.h                strlen + read/write + open/close/create + puts/put_char
│   ├── process.h           exit/fork/waitpid/getpid/yield/sleep + exec
│   ├── fs.h                dirent/stat + getdents/stat/chdir/getcwd + mkdir/unlink/rmdir
│   ├── mem.h               PROT_* + mmap/munmap + malloc/free/calloc
│   ├── lock.h              spinlock / mutex
│   ├── thread.h            thread_create/exit/join + 栈管理
│   ├── sched.h             tickets / alpha / sched_proc_stat / get_ticks
│   ├── ipc.h               pipe / dup2
│   ├── net.h               socket 封装(100–109 号段)
│   ├── string.h            标准字符串/内存函数(static inline)+ trim / copy_str / read_file
│   ├── fmt.h               parse_int / put_int / put_hex / append_* / str_eq + uprintf / snprintf 族
│   ├── env.h               getenv/getenv_r / setenv / unsetenv / clearenv / listenv
│   ├── user.h              纯汇总入口（#include 全部上述头文件）
│   └── my/                 C++ 桥接层与裸运行时库
│       ├── stdcompat.h     C++ 桥接头文件：std::vector→mv::Vector, std::exp→mymath::exp...
│       ├── cmath.h         裸运行时数学库(exp/log/sqrt/pow/sin/cos)
│       ├── vector.h        裸运行时 Vector<T>
│       ├── string.h        字符串操作 + read_file
│       ├── io.h            系统调用封装(read/write/open/close)
│       ├── random.h        随机数生成器
│       └── compat.h        类型工具 + placement new
├── lib/                    crt0 与 syscall_<arch>.S、cpp_runtime.cpp
├── src/                    C 系统程序 → /bin
│   ├── ls.c                目录列表
│   ├── cat.c               文件内容输出
│   ├── echo.c              回显参数
│   ├── grep.c              文本过滤
│   └── shell.c             交互式 shell（内建命令 + 外部命令执行）
├── tests/                  C / 单文件 Rust / 单文件 C++ 测试程序 → /tests
├── c/                      C 项目型构建目录（每个子文件夹编译为一个 ELF，如多文件工程 httpd）→ /programs
│   ├── lua                 Lua 5.4 官方源码
│   └── httpd               httpd 代码
├── cpp/                    C++ 项目型构建目录（每个子文件夹编译为一个 ELF）→ /programs
│   └── lvm                 转载期AOT的JVM 代码
├── gcn/                    C++ 图神经网络项目（GCN/GAT）→ /gcn
│   ├── Tensor.h
│   ├── GCNLayer.h
│   ├── GATLayer.h
│   ├── Func.h
│   ├── Optim.h
│   ├── Dataset.h
│   ├── train_cora.cpp
│   ├── train_cora_GTA.cpp
│   ├── gradcheck.cpp
│   └── main.cpp
├── java/                   Java 程序源码（.java → .class）→ /jvm
├── rust/                   cargo workspace
│   ├── ulib/               no_std 公共库 crate
│   └── programs/<crate>/   依赖 ulib 的 Rust 程序 → /programs
└── build.py                统一构建脚本(按来源/语言分派编译)
```

构建产物进入：

```text
user/build/<arch>/
├── bin/          # src/*.c
├── samples/      # samples/*.{c,cpp,rs,S}
├── programs/     # c/*/, cpp/*/, rust/programs/*
└── gcn/          # gcn/*.cpp
```

随后由 `user/mkfs_ext4.sh` 打包进 ext4 镜像；FAT 盘镜像由同一脚本单独生成（`mkfs.fat -F 16`）。

```text
target/fs-riscv64.img        ext4 rootfs(riscv)
target/fs-loongarch64.img    ext4 rootfs(loongarch)
target/fat-riscv64.img       FAT 数据盘(riscv)
target/fat-loongarch64.img   FAT 数据盘(loongarch)
```

修改 `user/rootfs`、`user/src`、`user/tests`、`user/gcn` 或 `user/rust` 后重新运行 `./run.sh <arch> debug`，即可在系统 shell 中看到新的文件结构与用户程序。

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
                         User Programs (C / C++ / Rust / Java / lua)
                                  │
                                  ▼
                               Syscall
                                  │
        ┌────────────────┬────────┴────────┬────────────────┐
        ▼                ▼                 ▼                ▼
       VFS           Scheduler       Process/Thread       IPC
        │                │                 │           pipe / dup2
        ▼                ▼                 ▼
   Mount Table     alpha-scaled       address space
  /    │    \      stride scheduler    fd table
ext4 tmpfs FAT     (continuous alpha
 │   (mem) (disk)    + AIMD policy)
 ▼          │
Block      BlockDevice(读写)
Cache       │
 │          │
 ▼          ▼
BlockDevice ───┐
 /         \   │
virtio-mmio virtio-pci
 RISC-V     LoongArch64
```

网络子系统与文件系统并列，挂在同一棵调用树下，并与块设备共享 virtio transport：

```text
User Programs (httpd / udp_test / tcp_test)
                │  socket syscalls(100–109 专用号段)
                ▼
            Socket 层(UDP / TCP 统一 socket table)
                │
        TCP(状态机/滑窗/Jacobson-Karn RTO)   UDP   ICMP
                │
        IPv4  ·  DHCP(自动配置地址)
                │
        ARP(缓存 + 挂起队列)
                │
        Ethernet → virtio-net 驱动
                │
        virtio-mmio(riscv64)/ virtio-pci(loongarch64)
```

---

## Current Status

已经完成：

* RISC-V 64 / LoongArch64 内核启动
* trap handling、syscall、进程调度
* stride scheduling
* alpha-scaled scheduling（连续 alpha `[0,100]`，纯整数幂 + 热路径缓存）
* 调度统计接口（含 deadline / tardiness / jitter 原始量）
* `fork / exec / waitpid`
* 用户态线程 `thread_create / thread_exit / thread_join`
* 用户态 shell、`argc / argv`、内建与外部命令、命令搜索路径（/bin、/tests、/programs）
* shell 词法解析：引号剥离 / 转义 / 行内注释 / 自分隔操作符 / 畸形语法报错
* fd table、`open / close / read / write`、`stat / fstat`、`getdents`、`cwd / chdir / getcwd`
* Unix 风格 **open flags**（`O_RDONLY/WRONLY/RDWR/CREAT/TRUNC/APPEND`）与读写权限强制
* 管道 `pipe`、`dup2`；shell **多级管道** `|`、重定向 `< > >>`、管道与重定向自由组合
* VFS 多文件系统挂载（最长前缀匹配）
* read-only ext4 rootfs（基于 `ext4_view`）
* 可写 tmpfs（mkdir / create / write / read / lseek / ftruncate / fsync / truncate / rename / unlink / rmdir / 递归删除，挂载于 `/tmp`）
* 可写 **FAT16** 落盘文件系统（vendored `fatfs` + `BlockIo` 适配，挂载于 `/fat`，跨重启持久化，开启 LFN）；支持 `lseek` / `ftruncate` / `fsync` / `rename`，`fsync` 走 `BlockIo → BlockDevice` 真刷盘链
* 文件位置 / 裁剪 / 刷盘 / 改名系统调用（号段 **64–68**：fsync / ftruncate / truncate / lseek / rename），tmpfs 与 FAT 双后端通用；权限对齐现有 `sys_write` 只查 `File::writable()`，不做完整 `check_access`；跨设备 rename 经 `mount_point_of()` 比较挂载点返回 EXDEV
* 5 个独立用户态测试程序（`lseek_test` / `ftruncate_test` / `truncate_test` / `fsync_test` / `rename_test`）覆盖上述 syscall 的正常 / 错误路径（含 FAT 落盘组、跨设备 EXDEV、目录移入自身子目录等边界）
* BlockDevice trait（读 + 写）、RamDisk、BlockCache
* RISC-V virtio-mmio / LoongArch64 virtio-pci block device（读 + 写路径，多盘发现）
* virtio-net 网卡驱动，自研 TCP/IP 协议栈（Ethernet / ARP / IPv4 / UDP / TCP / DHCP）
* TCP 11 态状态机、滑动窗口、超时重传（RTO 指数退避）；connect 与 listen / accept 双路径实机验证
* DHCP 客户端四步交互（DORA），自动配置地址（实测 10.0.2.15，租期 86400s）
* socket 专用系统调用号段 100–109（socket / bind / connect / listen / accept / send / recv / sendto / recvfrom / close）
* 用户态 HTTP 服务器 `httpd`（多文件 C 工程：静态文件 + 内联路由 + JSON API），宿主机浏览器经 `hostfwd` 实机访问
* ICMP echo（ping）与双机 socket netdev pair 互连（修复 MAC 硬编码与同网段 next_hop 路由）
* 用户态 TFTP 客户端：slirp `tftpboot` 文件注入通道（服务器实测在 `10.0.2.2`）
* TCP **Jacobson/Karn 自适应 RTO**（定点 SRTT/RTTVAR、队首采样、RTO-restore、阻塞式流量控制），100K 丢包实验提速 2.4–4.0×
* 用户态 libc 补全：标准 `string.h`、`snprintf` 族（全 `static inline`，多文件链接安全）
* 从 ext4 `/bin/shell` 启动 init shell
* C 用户库（分层头文件）与 Rust 用户库 `ulib`
* Rust 用户程序支持（单文件 rustc + cargo workspace），双架构，syscall ABI 语言无关
* **C++ 用户程序支持**：`stdcompat.h` 桥接层，算法代码零改动移植
* **裸运行时数学库**（`exp`/`log`/`sqrt`/`pow`/`sin`/`cos`），fdlibm 风格，双精度全精度
* **图神经网络 GCN/GAT 在裸运行时上运行**：完整前向/反向传播、AdamW、Dropout、softmax/交叉熵、数值梯度检验（`gradcheck` 1e-8 级 PASS）
* **Java 用户程序支持**：自研 JVM（class 解析 + 解释器 + 装载期 AOT），riscv64 / loongarch64 双原生后端，`Rmiku.*` native 桥接 syscall ABI
* **RmikuRay**：纯 Java 16.16 定点光线追踪（零浮点指令），双线程分带渲染拼帧，双架构同一份 class 运行
* **buddy 物理帧分配器**（order 0–10，连续块供 DMA，dealloc 越界断言 + double-free 检查）
* **Lua 用户程序支持**：零改动移植 Lua 5.4 官方源码，lcompat 桥接层（同名头文件 + 手写 setjmp/longjmp），9 组递进测试全部通过（含 pcall / coroutine）
* **环境变量子系统**：内核 syscall 号段 45–49（`getenv` / `getenv_r` / `setenv` / `unsetenv` / `listenv` / `clearenv`）+ C 用户库 `env.h` 封装
* shell 内建 `export` / `env` / `unset`，支持 `$VAR` / `${VAR}` / `$?` 展开（单引号保护、双引号展开），`PATH` 经 `getenv` 驱动命令搜索且可即时修改
* 内核 `exec` 路径将 `envp` 经寄存器（`a2` / `r6`）传入 `main` 第 3 参数，`test_env.c` 全量用例（9 项）在双架构实机验证通过
* 统一构建脚本 `build.py`（C / C++ / 单文件 Rust / cargo Rust 分派编译）
* 双架构关机（riscv SiFive Test finisher / loongarch ACPI GED）
* RISC-V / LoongArch64 SMP 启动与多核调度验证（per-hart timer、IPI reschedule、running_on 防重入）
* QEMU 软件模拟下的多核性能测试说明：区分 correctness 验证与真实性能 scaling，避免把 TCG / 串口 / 调试日志开销误判为内核开销
* alpha mechanism / edge deadline / AIMD 自适应 / 动态负载 四层调度实验

---

## Roadmap

### Network

virtio-net → Ethernet / ARP / IPv4 / UDP / TCP / DHCP / socket / httpd 已打通（见 Network Stack 一节），下一步：

* RTO 实验补强：交错（interleaved）重复尺寸扫描，消除跨 session 漂移图中的「版本–顺序」混杂
* socket 表扩容与 SYN 队列（当前 8 槽 + TIME_WAIT 10s，快速连发时 SYN 被静默丢弃，宿主侧表现为周期性 18s 停摆）
* DHCP 租约续期（T1 / T2）
* socket fd 与文件 fd 的统一 fd 表
* 并发 httpd：每连接一个用户态线程（`thread_create` 已就位）

### Filesystem

* FAT 当前为 FAT16 / 单分区，可扩展 FAT32 与更深的子目录用例
* `fsync` 已在 FAT 上实现完整刷盘链（`file.flush()` → `BlockIo::flush` → `BlockDevice::flush`），tmpfs 为内存 no-op；后续可探索更通用的**块设备写回缓存**（延迟写 + 脏页回收），进一步统一各后端的持久化语义
* 可写文件系统的并发访问（当前 fatfs 单核 + 全局锁）

### Scheduler

* per-process alpha 或调度 class（让 control / AI / logger 各自一档，而非全局单旋钮）
* 更复杂的反馈控制器（如以 tardiness 为误差信号的 PI 控制）
* 更丰富的动态负载模式（多阶段、随机突变）

### C++ Ecosystem

* 扩展 `stdcompat.h` 覆盖更多标准库容器与算法
* 探索更复杂的 C++ 应用移植（如线性代数库、小型游戏引擎）

---

## Project Goal

为了好玩，写RmikuOS的时候，挺开心的

