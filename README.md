# RmikuOS

RmikuOS 是一个从零实现的教学型操作系统内核，主要用于学习操作系统、体系结构、虚拟化设备和文件系统实现。目前支持 **RISC-V 64** 和 **LoongArch 64** 两个架构，可以在 QEMU 上启动用户态 shell，并从真实的 virtio 块设备中加载 ext4 rootfs。

这个项目不是简单的 `Hello, world` 内核。当前系统已经具备进程调度、系统调用、用户程序加载、VFS、只读 ext4 文件系统、virtio 块设备驱动和一个可交互 shell。

---

## Features

### 多架构支持

目前支持：

* `riscv64`
* `loongarch64`

两个架构共用大部分内核逻辑，包括：

* 任务管理
* 虚拟内存
* VFS
* ext4 rootfs
* block cache
* shell 和用户程序

架构相关部分主要集中在 trap、上下文切换、页表、时钟中断和设备发现。

---

### 用户态程序与 shell

RmikuOS 支持从 ext4 rootfs 中加载用户程序。

目前 shell 支持：

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

第一个用户进程不再依赖内核内置的 app table，而是通过 VFS 从：

```text
/bin/shell
```

加载。

---

### VFS 与文件描述符

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

### ext4 rootfs

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
│   └── fs_stress
├── etc/
│   └── motd
├── home/
├── share/
├── tmp/
├── dev/
└── proc/
```

---

### 真实 virtio 块设备

RmikuOS 当前已经不再只依赖内核内置 ramdisk，而是可以从 QEMU 挂载的真实磁盘镜像读取 ext4 rootfs。

不同架构使用不同 virtio transport：

```text
riscv64      -> virtio-mmio
loongarch64 -> virtio-pci
```

统一抽象为：

```text
BlockDevice
```

上层文件系统完全不关心底层设备类型：

```text
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

---

### RISC-V virtio-mmio

在 RISC-V QEMU `virt` 机器上，系统通过 virtio-mmio 扫描 virtio block device。

当前流程：

````text
扫描 virtio-mmio slot virtio block device。

当前流程：

```text
扫描 virtio-mmio slot
识别 virtio-blk
初始化 legacy virtio-mmio device
配置 virtqueue
提交 block read request
读取 ext4 rootfs
````

---

### LoongArch virtio-pci

在 LoongArch QEMU `virt` 机器上，系统通过 PCI/PCIe 枚举 virtio block device。

当前流程：

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

### BlockCache

块设备上方实现了 block cache，用于缓存最近访问的磁盘 block。

这使得 ext4 后端不需要每次都直接访问 virtio 设备。

当前路径：

```text
ext4 read
  ↓
BlockCache
  ↓
BlockDevice
```

---

### 压力测试

项目包含文件系统压力测试程序，例如：

```text
fs_stress2
```

测试内容包括：

* `open / read / close`
* `getdents`
* `stat / fstat`
* `chdir / getcwd`
* 相对路径
* fd 复用
* ext4 目录遍历
* 多次文件读取

示例：

```text
/ $ fs_stress2 1000
FS STRESS PASS
```

---

## Build and Run

### RISC-V

```bash
./run.sh riscv64 debug
```

或：

```bash
./run.sh riscv64 release
```

RISC-V 使用 QEMU virt 机器和 virtio-mmio 块设备。

---

### LoongArch64

```bash
./run.sh loongarch64 debug
```

或：

```bash
./run.sh loongarch64 release
```

LoongArch64 使用 QEMU virt 机器和 virtio-pci 块设备。

---

## Rootfs

rootfs 模板目录是：

```text
user/rootfs/
```

用户程序源码放在：

```text
user/src/
```

构建后用户程序会进入：

```text
user/build/<arch>/
```

随后被打包进 ext4 镜像的：

```text
/bin
```

生成的 rootfs 镜像位于：

```text
target/fs-riscv64.img
target/fs-loongarch64.img
```

修改 `user/rootfs` 后重新运行：

```bash
./run.sh riscv64 debug
```

或者：

```bash
./run.sh loongarch64 debug
```

即可在系统 shell 中看到新的文件结构。

---

## Current Architecture

```text
                  User Programs
                       │
                       ▼
                    Syscall
                       │
                       ▼
                    VFS Layer
                       │
                       ▼
                read-only ext4
                       │
                       ▼
                  Block Cache
                       │
                       ▼
                  BlockDevice
                 /           \
                /             \
        virtio-mmio       virtio-pci
         RISC-V           LoongArch
```

---

## Current Status

已经完成：

* RISC-V 64 内核启动
* LoongArch 64 内核启动
* trap handling
* syscall
* 进程调度
* `fork / exec / waitpid`
* 用户态 shell
* `argc / argv`
* fd table
* `open / close / read / write`
* `stat / fstat`
* `getdents`
* `cwd / chdir / getcwd`
* VFS
* read-only ext4 rootfs
* BlockDevice trait
* RamDisk
* BlockCache
* RISC-V virtio-mmio block device
* LoongArch virtio-pci block device
* 从 ext4 `/bin/shell` 启动 init shell
* rootfs overlay
* 文件系统压力测试

---

## Roadmap

后续计划：

### Threads

将当前 `TaskControlBlock` 拆分为：

```text
ProcessControlBlock
ThreadControlBlock
```

目标：

* 用户线程
* `thread_create`
* `thread_exit`
* `thread_join`
* 线程共享地址空间和 fd table

---

### Writable Filesystem

短期目标不是直接实现可写 ext4，而是先实现一个简单的内存文件系统：

```text
tmpfs / ramfs
```

目标：

* `open(O_CREAT)`
* `write`
* `mkdir`
* `unlink`
* 临时可写目录

---

### Network

后续可以实现 virtio-net，并逐步支持：

* Ethernet frame
* ARP
* IPv4
* ICMP
* UDP
* 简单 socket API

---

## Project Goal

RmikuOS 的目标不是追求一次性实现完整 Unix，而是逐步构建一个能真实运行、能调试、能扩展的教学型操作系统。

当前阶段的重点是：

```text
让用户程序从真实 ext4 rootfs 中运行，
让两个架构都能通过 virtio 块设备访问 QEMU 磁盘，
并保持上层 VFS 和 shell 的统一。
```

RmikuOS is small, but it is real enough to be fun.
