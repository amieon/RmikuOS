[← 返回 RmikuOS 主页](../README.md)

---

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

