# RmikuOS

[![CI/CD](https://github.com/amieon/RmikuOS/actions/workflows/ci.yml/badge.svg)](https://github.com/amieon/RmikuOS/actions/workflows/ci.yml) [![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)

RmikuOS 是一个从零实现的教学型操作系统内核,支持 **RISC-V 64** 与 **LoongArch 64** 双架构。它可以在 QEMU 上启动用户态 shell,从真实 virtio 块设备加载 ext4 rootfs,并运行 **C / C++ / Rust / Java / Lua / Scheme** 六种语言的用户程序,内置 **TCC（Tiny C Compiler）** 可在系统内现场编译并运行 C 程序（AOT + JIT 双模式）,配备 **kilo 全屏编辑器**（ANSI 终端、语法高亮）——编辑、编译、运行完整闭环,还内置 **SQLite 3.50 交互式数据库**（自定义 VFS 落盘，数据可持久化到 FAT 磁盘）,并通过自研 TCP/IP 协议栈（含 DHCP 自动配置与租约续期、DNS 域名解析）向宿主机浏览器提供真实的 HTTP 服务。

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

![RmikuOS shell](docs/images/rmikuos_shell.png)

*Boot and Shell*

---

## 功能总览

| 子系统       | 能力                                                         | 备注                                                         |
| ------------ | ------------------------------------------------------------ | ------------------------------------------------------------ |
| 双架构       | RISC-V 64 + LoongArch 64,SMP 多核                            | virtio-mmio / virtio-pci                                     |
| 进程与线程   | `fork` / `exec` / `waitpid`、`thread_create` / `thread_exit` / `thread_join` | 进程级 fd table,线程共享地址空间                             |
| 信号         | 通用 `sig_pending` 位图 + 延迟投递                           | 用户态 SIGILL/SIGFPE 不炸内核,shell Ctrl+C                   |
| 虚拟内存     | buddy 帧分配器、多级页表、ELF 加载、mmap                     |                                                              |
| 文件系统     | VFS 多挂载:ext4 rootfs(只读) / 可写 ext4(rsext4 + JBD2 日志) / tmpfs / FAT32 | `lseek` / `ftruncate` / `fsync` / `rename`;数据盘 LABEL 身份识别 + 周期 writeback |
| 用户与组     | `useradd` / `groupadd` / `usermod` / `passwd` / `su` / `id`(身份类仅 root) | 账户库 defaults + overrides:可写 `/var/etc/{passwd,group}` 覆盖只读出厂 `/etc/{passwd,group}` |
| 调度器       | stride + alpha-scaled + AIMD / SPSA-AdamW 自适应             | 内置调度实验框架(exp00–exp06,见 docs)                        |
| 网络         | 自研 TCP/IP:Ethernet / ARP / IPv4 / UDP / TCP / DHCP / DNS / ICMP | TCP 11 态 + Jacobson/Karn RTO + 用户态 httpd + 域名解析(TTL 缓存) |
| 用户程序     | C / C++ / Rust / Java(JVM + 装载期 AOT)/ Lua 5.4 / Scheme    | syscall ABI 语言无关                                         |
| 系统内工具链 | TCC 0.9.28(AOT + JIT)、SQLite 3.50(自定义 VFS 落盘)、kilo 编辑器 | 编辑-编译-运行闭环                                           |
| 应用验证     | VeryEasyGCN(78.3% 准确率)、RmikuRay(定点光线追踪)、GCN/GAT   |                                                              |

---

## 文档索引

主 README 只保留门面与索引,深度内容按主题拆到 `docs/`:

| 文档                                           | 内容                                                         |
| ---------------------------------------------- | ------------------------------------------------------------ |
| [docs/shell.md](docs/shell.md)                 | Shell 词法 / 管道 / 重定向 / 环境变量 / `$?` 展开,TCC 自托管工具链,kilo 编辑器 |
| [docs/filesystem.md](docs/filesystem.md)       | VFS 与 fd table,ext4 / tmpfs / FAT32,文件系统调用,virtio 块设备 |
| [docs/network.md](docs/network.md)             | 自研协议栈(ARP / IPv4 / TCP / UDP / DHCP / DNS / ICMP / NTP),socket 100–117 + `poll()` ,UDP connect,DHCP T1/T2,listen backlog,httpd,wget;TCP RTO / CUBIC / Go-Back-N 三组网络实验 |
| [docs/user-programs.md](docs/user-programs.md) | C 分层库 / C++ `stdcompat.h` 桥接 / Rust `ulib` / 自研 JVM / Lua 5.4 / Scheme,堆分配器与裸运行时数学库 |
| [docs/scheduler.md](docs/scheduler.md)         | stride 与 alpha-scaled 调度机制,调度统计接口,SMP 与计时注意事项 |
| [docs/experiments/](docs/experiments/)         | 调度实验框架(schedlab)+ 7 篇完整实验报告:EDF 基线 / α 机制 / Edge Deadline / AIMD / 动态负载 / 相位 / SPSA-AdamW |
| [docs/report.md](docs/report.md)               | α 缩放调度旋钮的自适应控制：五类方法家族的受控对照研究 |

---

## CI/CD 持续集成

每次提交后,GitHub Actions 自动验证双架构(手动触发,可选架构):

```text
双架构交叉编译 -> rootfs 制作 -> QEMU 启动 -> 自动登录 -> 36 项回归测试 -> 检查汇总 -> shutdown 关机
                                            \-> 可写 ext4 崩溃一致性测试(kill -9 + journal 回放 + e2fsck)
```

* 流水线文件:`.github/workflows/ci.yml`;冒烟脚本 `scripts/smoke_test.sh`,崩溃一致性脚本 `scripts/crash_consistency_test.sh`
* 触发方式:仓库 **Actions** 页 → 左侧 **CI/CD** → 右侧 **Run workflow**,选择 `riscv64` / `loongarch64` / `both`
* 构建产物(内核 ELF + rootfs + FAT 镜像)自动缓存,二次运行大幅提速
* 回归测试:进系统后 `run_all` 一键执行 36 个测试(断言库 `user/include/test.h`,覆盖进程/线程/内存/文件系统/管道/syscall/SQLite 落盘/数学库/printf/setjmp/C++ 容器/语言运行时),任一失败即流水线红
* 崩溃一致性测试:`bigwrite` 写大文件到一半时 `kill -9` QEMU 模拟掉电,再用同一块数据盘重启,断言 journal 自动回放、`/home` 重新绑定成功、已 sync 数据仍在,最后用宿主机 `e2fsck -fn` 校验磁盘结构干净——把"JBD2 真的在工作"从一次性手工实验变成每次提交都验证的事实(测试用 `/tmp` 下的临时数据盘,不碰 `target/data-*.img`)
* 打 `v` 开头的标签(如 `v1.0`)时,自动构建双架构 release 产物并上传 GitHub Release

---

## 环境搭建

### Docker(推荐)

```bash
docker build -t rmikuos-dev .
docker run -it --rm -v $(pwd):/work -p 8080:8080 rmikuos-dev
```

> 构建默认使用本地 cross-tools/ 目录中的 loongarch64 工具链。
> 没有的话,先从 loong64/cross-tools releases(https://github.com/loong64/cross-tools/releases)下载 x86_64 宿主版,解压后将 loongarch64-unknown-linux-gnu 里的内容移至 ./cross-tools。

### 无 Docker

```bash
bash first_run.sh   # 自动装 apt/rustup/工具链
```

之后这样就行:

```bash
./run.sh riscv64
./run.sh loongarch64
```

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

> 注:在 QEMU 软件模拟下,loongarch64 的指令翻译、串口 IO 与多 vCPU 执行效率可能明显低于 riscv64;日常开发建议以 riscv64 为主,loongarch64 用于跨架构正确性验证。

---

## Source Layout(用户程序与 rootfs 布局)

```text
user/
├── rootfs/                 rootfs 目录模板(etc/motd, home, share, tmp, fat ...)
├── include/                C/C++ 用户库(分层头文件,types/syscall/flag/io/process/fs/mem/lock/thread/sched/ipc/net/string/fmt/env + user.h 汇总)
│   └── my/                 C++ 桥接层与裸运行时库(stdcompat.h / cmath.h / vector.h ...)
├── lib/                    crt0 与 syscall_<arch>.S、cpp_runtime.cpp
├── src/                    C 系统程序 → /bin(ls / cat / echo / grep / shell / nslookup)
├── tests/                  C / 单文件 Rust / 单文件 C++ 测试程序 → /tests
├── c/                      C 项目型构建目录(多文件工程 httpd、lua)→ /programs
├── cpp/                    C++ 项目型构建目录(装载期 AOT 的 JVM)→ /programs
├── gcn/                    C++ 图神经网络项目(GCN/GAT)→ /gcn
├── java/                   Java 程序源码(.java → .class)→ /jvm
├── rust/                   cargo workspace(ulib no_std 公共库 + programs)
└── build.py                统一构建脚本(按来源/语言分派编译)
```

构建产物进入 `user/build/<arch>/`(bin / samples / programs / gcn),由 `user/mkfs_ext4.sh` 打包进 ext4 镜像,FAT 盘镜像由同一脚本生成

修改 `user/rootfs`、`user/src`、`user/tests`、`user/gcn` 或 `user/rust` 后重新运行 `./run.sh <arch> debug`,即可在系统 shell 中看到新的文件结构与用户程序。

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
 ▼          │        (另: /home 挂可写 ext4 —— rsext4+JBD2,
Block      BlockDevice(读写)   经 LABEL 识别数据盘,与 FAT 共享 BlockDevice)
Cache       │
 │          │
 ▼          ▼
BlockDevice ───┐
 /         \   │
virtio-mmio virtio-pci
 RISC-V     LoongArch64
```

网络子系统与文件系统并列,挂在同一棵调用树下,并与块设备共享 virtio transport:

```text
User Programs (httpd / wget / nslookup / ping / ntpdate / tftp)
                │  socket syscalls(100–117 专用号段) + poll
                ▼
            Socket 层(UDP / TCP 统一 socket table,动态扩容 + free list,
                      UDP connect,listen backlog,poll 就绪分派)
                │
        TCP(状态机/滑窗/Jacobson-Karn RTO)   UDP   ICMP
                │
        IPv4  ·  DHCP(自动配置 + T1/T2 租约续期) ·  DNS(域名解析 + TTL 缓存)
                │
        ARP(缓存 + 挂起队列)
                │
        Ethernet → virtio-net 驱动
                │
        virtio-mmio(riscv64)/ virtio-pci(loongarch64)
```

---

## Current Status

已经完成:

* **内核基础**:双架构启动 / trap / syscall / 进程线程 / 信号投递与用户态隔离 / buddy 帧分配器 / SMP 多核(per-hart timer、IPI reschedule、TLB shootdown)
* **调度器**:stride scheduling + alpha-scaled(连续 alpha `[0,100]`,纯整数幂)+ AIMD / SPSA-AdamW 自适应策略,完整调度实验框架与 7 篇实验报告(见 [docs/experiments/](docs/experiments/))
* **文件系统**:VFS 多挂载 / ext4 rootfs / 可写 tmpfs / 可落盘 FAT16(跨重启持久化)/ 可写 ext4 数据盘(rsext4 0.9.2 + JBD2,挂 `/home`:超级块卷标 LABEL 式盘身份识别、周期 writeback + `fsync`、首次开机内核内 mkfs 自举)/ 管道与重定向 / 环境变量(`$VAR` / `${VAR}` / `$?` 展开)/ 文件定位裁剪刷盘改名(号段 64–68)
  * **崩溃一致性实测**:16MB 大文件写入至 6MB 时 `kill -9` QEMU → 重启后 journal 自动回放,`/home` 干净挂载、已 sync 数据完好;镜像经宿主机 `e2fsck 1.47.2 -fn` 五遍检查零错误——内核内 rsext4 产生的 ext4 结构通过 e2fsprogs 参考实现认证
  * **存储布局 / bind mount**:一块可写数据盘(rsext4 + JBD2,经超级块卷标 `RMikuOS-DATA` 识别)挂载在 `/data`,`/home`(用户目录)与 `/var`(系统运行时状态)是它的 **bind mount**——Linux `mount --bind` / 容器卷的同款机制。VFS 挂载表存的是 inode 引用而非文件系统,因此一份盘可服务多个路径;盘上由内核首次挂载时自动建出 `home/`、`var/etc/`
  * **多用户与可写系统状态**:`useradd` / `groupadd` / `usermod` / `passwd` / `su` / `id`;账户库采用 **defaults + overrides**(可写 `/var/etc/{passwd,group}` 优先,回退只读出厂 `/etc/{passwd,group}`,写入即 fsync)。身份类命令仅 root 可执   ,`passwd` 是唯一例外:非 root 只能改自己的,且必须先验证旧口令
* **网络**:自研 TCP/IP 协议栈(Ethernet / ARP / IPv4 / UDP / TCP / DHCP / DNS / ICMP)、socket 100–117、通用 `poll()`(支持 socket / 管道 / 普通文件)、UDP `connect()` 与统一临时端口分配器(49152–65535)、DHCP T1/T2 租约续期、TCP listen backlog、用户态 httpd(宿主机浏览器访问)、DNS 客户端(压缩指针解析 + TTL 缓存,nslookup)、TFTP / NTP / wget(wget/ping/ntpdate 支持域名参数)、shell 命令替换 `$()` / 反引号、TCP Jacobson/Karn 自适应 RTO(100K 丢包实验提速 2.4–4.0×)
* **语言与工具链**:C 分层库 + Rust `ulib` + C++ `stdcompat.h` 桥接 + 自研 JVM(装载期 AOT,双架构后端)+ Lua 5.4 零改动 + Scheme;系统内 TCC(AOT + JIT)、SQLite 3.50(自定义 VFS 落盘)、kilo 编辑器
* **验证**:36 项 CI 回归测试 + 可写 ext4 崩溃一致性 CI 项(kill -9 → journal 回放 → `e2fsck -fn` 零错误)、VeryEasyGCN 78.3%、RmikuRay 定点光追、GCN/GAT gradcheck 1e-8 级 PASS

---

## Roadmap

### Network

* RTO 实验补强:交错（interleaved）重复尺寸扫描,消除跨 session 漂移图中的「版本–顺序」混杂
* TCP backlog 细化:拆分 SYN 半连接队列与 accept 全连接队列(当前教学版共用额度)
* DNS:CNAME 链追踪 / 多服务器 fallback / 域名合法性预检
* TCP/UDP 收发超时 `SO_RCVTIMEO` 可配
* 并发 httpd:基于 `poll()` 的多连接事件循环(不再必须每连接一个用户态线程)

### Filesystem

* 可写 ext4 精修:virtio-blk 真 flush(`VIRTIO_BLK_T_FLUSH`,当前 capabilities 已声明但驱动为空实现)、per-file fsync(当前退化为挂载点全量 sync)、根命令行 `root=`/设备树别名式盘识别(当前 LABEL 优先 + 顺序兜底)
* **overlayfs**:只读 rootfs(ext4-view)+ 可写层(rsext4)+ whiteout——整个 `/` 可写、删错可恢复(Docker rootfs 模式)
* 文件系统并发访问的细粒度锁(当前 rsext4 / fatfs 均为单核 + 全局锁)

### Scheduler

* per-process alpha 或调度 class(让 control / AI / logger 各自一档,而非全局单旋钮)
* 更复杂的反馈控制器(如以 tardiness 为误差信号的 PI 控制)
* 更丰富的动态负载模式(多阶段、随机突变)

### C++ Ecosystem

* 扩展 `stdcompat.h` 覆盖更多标准库容器与算法
* 探索更复杂的 C++ 应用移植(如线性代数库、小型游戏引擎)

---

## Project Goal

为了好玩,写 RmikuOS 的时候,挺开心的
