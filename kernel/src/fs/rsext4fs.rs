use alloc::string::String;
use alloc::sync::Arc;
use alloc::vec::Vec;

use crate::drivers::block::BlockDevice;
use crate::fs::flag::{O_ACCMODE, O_APPEND, O_RDONLY, O_WRONLY};
use crate::sync::spin::Mutex;
use core::sync::atomic::{AtomicBool, AtomicUsize, Ordering};

use super::dirent::{DirEntry, FILE_TYPE_DIR, FILE_TYPE_FILE};
use super::file::{File, FileRef};
use super::inode::{Inode, InodeRef, InodeType, Metadata};
use super::mount::{self, FileSystem};
use super::stat::{Stat, STAT_TYPE_FILE};
use super::ReadOnlyDirFile;

use rsext4::{
    BlockIo, Clock, DeviceCapabilities, DeviceGeometry, DirectoryCursor, DirectoryEntryType,
    EntropySource, Ext4, Ext4Error, Ext4Result, Ext4Timestamp, FileName, FilePermissions,
    InodeMetadataUpdate, InodeNumber, MountOptions, MountServices, MountedServices, MutationContext,
    NoopObserver, RenameOptions, SectorId,
};

// 1. BlockIo 适配

pub struct RmikuBlockIo {
    inner: Arc<dyn BlockDevice>,
}

impl RmikuBlockIo {
    pub fn new(inner: Arc<dyn BlockDevice>) -> Self {
        assert_eq!(inner.block_size(), 512, "rsext4 适配层假设 512 字节扇区");
        Self { inner }
    }
}

fn io_errno(errno: isize) -> Ext4Error {
    // ErrorContext 是 Copy 枚举（只能装 'static str），动态 errno 记日志。
    log::warn!("[ext4-rw] block io error: errno={}", errno);
    Ext4Error::io().with_operation("rmiku-blk-io")
}

impl BlockIo for RmikuBlockIo {
    fn write(&mut self, buffer: &[u8], sector: SectorId, count: u32) -> Ext4Result<()> {
        let base = sector.raw() as usize;
        for i in 0..count as usize {
            let chunk = &buffer[i * 512..(i + 1) * 512];
            // RmikuOS 约定：成功返回写入字节数(512)，失败返回负数（同 blockio.rs）
            let rc = self.inner.write_block(base + i, chunk);
            if rc != 512 {
                return Err(io_errno(rc));
            }
        }
        Ok(())
    }

    fn read(&mut self, buffer: &mut [u8], sector: SectorId, count: u32) -> Ext4Result<()> {
        let base = sector.raw() as usize;
        for i in 0..count as usize {
            let chunk = &mut buffer[i * 512..(i + 1) * 512];
            let rc = self.inner.read_block(base + i, chunk);
            if rc != 512 {
                return Err(io_errno(rc));
            }
        }
        Ok(())
    }

    fn geometry(&self) -> DeviceGeometry {
        DeviceGeometry::new(512, self.inner.num_blocks() as u64)
    }

    fn capabilities(&self) -> DeviceCapabilities {
        DeviceCapabilities {
            read_only: false,
            flush: true,
            barrier: false,
            fua: false,
            discard: false,
        }
    }

    fn flush(&mut self) -> Ext4Result<()> {
        // trait 默认实现返回 0=成功；设备覆盖时也应遵守（负数=错）
        let rc = self.inner.flush();
        if rc < 0 {
            return Err(io_errno(rc));
        }
        Ok(())
    }
}

// 2. runtime services

/// 时钟：接 RTC。sec 是 Unix 秒。
pub struct RmikuClock;

impl Clock for RmikuClock {
    fn now(&self) -> Ext4Result<Ext4Timestamp> {
        // TODO: 换成 RmikuOS 的 RTC 读取
        Ok(Ext4Timestamp::new(rmiku_rtc_unix_seconds(), 0))
    }
}

// 占位：接内核 RTC 后删掉
fn rmiku_rtc_unix_seconds() -> i64 {
    0
}

/// 熵源：接内核 RNG。
pub struct RmikuEntropy;

impl EntropySource for RmikuEntropy {
    fn fill_bytes(&mut self, output: &mut [u8]) -> Ext4Result<()> {
        // TODO: 换成内核 RNG；返回 Err 则 rsext4 报 unsupported_capability("runtime:entropy")
        output.fill(0);
        Ok(())
    }
}

// 3. 组装
//    MountServices::new(clock, entropy, observer) —— mmp_delay 默认 ()
//    mount 消费 services，clock 移入内部，Ext4 持有 MountedServices<E, O, W>

pub type Ext4Mount = Ext4<RmikuBlockIo, MountedServices<RmikuEntropy, NoopObserver>>;

/// 数据盘固定卷标（约定式身份）：discover_disks 按 s_volume_name 识别角色。
/// 与 mkfs_ext4.sh 里 rootfs 的 -L RMikuOS-ROOT 对应。
pub const DATA_VOLUME_LABEL: &[u8] = b"RMikuOS-DATA";

/// 在块设备上格式化 ext4（会抹掉盘上所有数据！）
/// 数据盘首次挂载流程：mount 失败 → format_ext4 → 重新 mount。
pub fn format_ext4(dev: Arc<dyn BlockDevice>) -> Ext4Result<()> {
    let io = RmikuBlockIo::new(dev);
    // 写入约定卷标（需 vendored rsext4 的 MkfsOptions.volume_label 补丁）
    let mut opts = rsext4::MkfsOptions::default();
    let n = DATA_VOLUME_LABEL.len().min(16);
    opts.volume_label[..n].copy_from_slice(&DATA_VOLUME_LABEL[..n]);
    // format 吃掉设备再还回来（mkfs 内部临时关 journal，结束前恢复）
    rsext4::format(io, RmikuClock, opts).map(|_| ())
}

pub fn mount_ext4(dev: Arc<dyn BlockDevice>) -> Ext4Result<Ext4Mount> {
    let io = RmikuBlockIo::new(dev);
    let services = MountServices::new(RmikuClock, RmikuEntropy, NoopObserver);
    // read_write() = { readonly: false, replay_journal: true, block_validity: true }
    // 若要启用 MMP：services.with_mmp(RmikuDelay, MmpIdentity{...}) 再传进来
    Ext4::mount(io, services, MountOptions::read_write())
}

// 4. VFS 接入层

/// 写操作的身份上下文。TODO: 多用户后从当前进程取 uid/gid/umask。
fn ctx() -> MutationContext {
    MutationContext::new(0, 0, 0, 0o022)
}

/// str → rsext4 的 FileName（含非法字符检查）
fn fname(name: &str) -> Option<FileName<'_>> {
    FileName::new(name.as_bytes()).ok()
}


// FileSystem

pub struct Ext4RwFs {
    ext4: Mutex<Ext4Mount>,
    root: InodeNumber,
    mount_point: String,
}


static DATA_FS: Mutex<Option<Arc<Ext4RwFs>>> = Mutex::new(None);

pub fn on_timer_tick() {
    #[cfg(target_arch = "riscv64")]
    const WRITEBACK_EVERY_TICKS: usize = 5000;
    #[cfg(target_arch = "loongarch64")]
    const WRITEBACK_EVERY_TICKS: usize = 10;
    static COUNTER: AtomicUsize = AtomicUsize::new(0);

    static FIRST_CALL: AtomicBool = AtomicBool::new(false);
    if !FIRST_CALL.swap(true, Ordering::Relaxed) {
        log::info!("[ext4-rw] timer 钩子首次触发（此后每周期一条 writeback ok）");
    }

    let n = COUNTER.fetch_add(1, Ordering::Relaxed) + 1;
    if n % WRITEBACK_EVERY_TICKS != 0 {
        return;
    }
    if let Some(holder) = DATA_FS.try_lock() {
        if let Some(fs) = holder.as_ref() {
            fs.try_sync();
        }
    }
}

// unsafe impl Send for Ext4RwFs {}
// unsafe impl Sync for Ext4RwFs {}

impl Ext4RwFs {
    /// 挂载；失败则 mkfs 后重挂（数据盘首次使用流程）。
    pub fn mount_or_format(dev: Arc<dyn BlockDevice>, mount_point: &str) -> Option<Arc<Self>> {
        let mut fs = match mount_ext4(dev.clone()) {
            Ok(fs) => fs,
            Err(e) => {
                log::warn!("[ext4-rw] mount 失败({:?}),尝试 mkfs 后重挂", e);
                format_ext4(dev.clone()).ok()?;
                mount_ext4(dev).ok()?
            }
        };
        let root = fs.root_inode();
        Some(Arc::new(Self {
            ext4: Mutex::new(fs),
            root,
            mount_point: String::from(mount_point),
        }))
    }

    /// 一站式接线：挂载（必要时 mkfs）+ 注册到 VFS。
    /// 返回文件系统句柄，供调用方接着做绑定挂载 / ensure_layout。
    ///
    /// 典型用法（一块数据盘服务多个路径）:
    /// ```ignore
    /// let data = Ext4RwFs::init_and_mount("/data", dev)?;   // 盘根挂 /data
    /// data.ensure_layout();                                // 建 home/ var/ var/etc/
    /// mount_bind("/home", lookup_abs_path("/data/home")?); // 绑定出去
    /// mount_bind("/var",  lookup_abs_path("/data/var")?);
    /// ```
    pub fn init_and_mount(mount_point: &str, dev: Arc<dyn BlockDevice>) -> Option<Arc<Self>> {
        match Self::mount_or_format(dev, mount_point) {
            Some(fs) => {
                mount::mount(mount_point, fs.clone());
                // 全局句柄：writeback timer 钩子从这里拿
                *DATA_FS.lock() = Some(fs.clone());
                log::info!("[ext4-rw] {} 挂载完成 (rsext4, 可写, JBD2)", mount_point);
                Some(fs)
            }
            None => {
                log::error!("[ext4-rw] {} 挂载失败", mount_point);
                None
            }
        }
    }

    /// 确保数据盘上的标准结构目录存在（首次 mkfs 后自动建好）。
    ///
    /// 布局（盘内视角）:
    ///   /  → home/        用户家目录，绑定到 /home
    ///     → var/etc/     系统运行时状态，绑定到 /var 后的 etc/
    pub fn ensure_layout(&self) {
        let mut fs = self.ext4.lock();
        let root = self.root;

        let home = Self::ensure_child(&mut fs, root, "home");
        let var = Self::ensure_child(&mut fs, root, "var");
        if let Some(var) = var {
            Self::ensure_child(&mut fs, var, "etc");
        }
        if home.is_none() || var.is_none() {
            log::warn!("[ext4-rw] 结构目录创建失败(盘已满?)");
        }
    }

    /// 目录不存在则创建，存在则直接返回其 inode 号。
    fn ensure_child(fs: &mut Ext4Mount, parent: InodeNumber, name: &str) -> Option<InodeNumber> {
        let bytes = name.as_bytes();
        if let Ok(Some(info)) = fs.lookup_child(parent, FileName::new(bytes).ok()?) {
            return Some(info.number);
        }
        let fname = FileName::new(bytes).ok()?;
        let perm = FilePermissions::new(0o755).ok()?;
        fs.create_directory(MutationContext::new(0, 0, 0, 0o022), parent, fname, perm)
            .ok()
            .map(|i| i.number)
    }

    /// 全量同步（数据缓存→元数据→journal commit）。
    /// fsync 系统调用走这里（进程上下文，可阻塞等锁）。
    pub fn sync(&self) -> bool {
        match self.ext4.lock().sync() {
            Ok(()) => true,
            Err(e) => {
                log::warn!("[ext4-rw] sync 失败: {:?}", e);
                false
            }
        }
    }

    /// 非阻塞版本：timer 中断上下文用。锁被占用就跳过本轮，绝不自旋等待。
    /// 成功时打 info 日志——writeback 是否真的在跑，LOG=info 下一目了然。
    pub fn try_sync(&self) -> bool {
        match self.ext4.try_lock() {
            Some(mut fs) => match fs.sync() {
                Ok(()) => {
                    //log::info!("[ext4-rw] writeback ok");
                    true
                }
                Err(e) => {
                    log::warn!("[ext4-rw] try_sync 失败: {:?}", e);
                    false
                }
            },
            None => false, // 有 syscall 正持有锁，跳过本轮 writeback
        }
    }
}

impl FileSystem for Ext4RwFs {
    fn root_inode(self: Arc<Self>) -> InodeRef {
        let root = self.root;
        Arc::new(Ext4RwInode { fs: self, number: root })
    }
}

// Inode
pub struct Ext4RwInode {
    fs: Arc<Ext4RwFs>,
    number: InodeNumber,
}

impl Ext4RwInode {
    fn info(&self) -> Option<rsext4::InodeInfo> {
        self.fs.ext4.lock().inode(self.number).ok()
    }
}

fn inode_type_of(t: DirectoryEntryType) -> InodeType {
    match t {
        DirectoryEntryType::Directory => InodeType::Directory,
        _ => InodeType::File,
    }
}

impl Inode for Ext4RwInode {
    fn metadata(&self) -> Metadata {
        match self.info() {
            Some(i) => Metadata {
                inode_type: inode_type_of(i.file_type()),
                size: i.size as usize,
                uid: i.uid as usize,
                gid: i.gid as usize,
                mode: i.mode & 0o7777,
            },
            None => Metadata {
                inode_type: InodeType::File,
                size: 0,
                uid: 0,
                gid: 0,
                mode: 0,
            },
        }
    }

    fn lookup(&self, name: &str) -> Option<InodeRef> {
        let name = fname(name)?;
        let child = self.fs.ext4.lock().lookup_child(self.number, name).ok()??;
        Some(Arc::new(Ext4RwInode {
            fs: self.fs.clone(),
            number: child.number,
        }))
    }

    fn open(&self, flags: usize) -> Option<FileRef> {
        let info = self.info()?;
        if info.file_type() == DirectoryEntryType::Directory {
            // 目录 fd：快照式（同 FatInode 的做法）
            Some(Arc::new(ReadOnlyDirFile::new(self.getdents())))
        } else {
            Some(Arc::new(Ext4RwFile {
                fs: self.fs.clone(),
                number: self.number,
                offset: Mutex::new(0),
                flags,
            }))
        }
    }

    fn getdents(&self) -> Vec<DirEntry> {
        let mut out = Vec::new();
        let mut fs = self.fs.ext4.lock();
        let mut reader = match fs.open_directory_reader(self.number) {
            Ok(r) => r,
            Err(_) => return out,
        };
        let mut cursor = DirectoryCursor::Start;
        loop {
            match fs.read_directory_with_reader(&mut reader, cursor, 64) {
                Ok(entries) if entries.is_empty() => break,
                Ok(entries) => {
                    // 拿到最后一条的 next_cursor 作为下一轮起点
                    let Some(last) = entries.last() else { break };
                    let next = last.next_cursor;
                    for e in &entries {
                        if e.name == b"." || e.name == b".." {
                            continue;
                        }
                        let name = core::str::from_utf8(&e.name).unwrap_or("");
                        let t = if e.file_type == DirectoryEntryType::Directory {
                            FILE_TYPE_DIR
                        } else {
                            FILE_TYPE_FILE
                        };
                        out.push(DirEntry::new(name, t));
                    }
                    if next == DirectoryCursor::End {
                        break;
                    }
                    cursor = next;
                }
                Err(_) => break,
            }
        }
        out
    }

    fn create(&self, name: &str) -> Option<InodeRef> {
        let name = fname(name)?;
        let mut fs = self.fs.ext4.lock();
        let info = fs
            .create_regular_file(ctx(), self.number, name, FilePermissions::new(0o644).ok()?)
            .ok()?;
        Some(Arc::new(Ext4RwInode {
            fs: self.fs.clone(),
            number: info.number,
        }))
    }

    fn mkdir(&self, name: &str) -> Option<InodeRef> {
        let name = fname(name)?;
        let mut fs = self.fs.ext4.lock();
        let info = fs
            .create_directory(ctx(), self.number, name, FilePermissions::new(0o755).ok()?)
            .ok()?;
        Some(Arc::new(Ext4RwInode {
            fs: self.fs.clone(),
            number: info.number,
        }))
    }

    fn truncate(&self) -> isize {
        self.truncate_to(0)
    }

    fn truncate_to(&self, len: usize) -> isize {
        match self.fs.ext4.lock().truncate_inode(self.number, len as u64) {
            Ok(()) => 0,
            Err(_) => -1,
        }
    }

    fn unlink(&self, name: &str) -> isize {
        let Some(name) = fname(name) else { return -1 };
        let mut fs = self.fs.ext4.lock();
        match fs.unlink(self.number, name) {
            Ok(outcome) => {
                // 链接数归零且 VFS 不再持有引用 → 回收 inode（孤儿收割，umount 前必须做完）
                if outcome.requires_reap() {
                    if let Err(e) = fs.reap_unlinked_inode(outcome.inode) {
                        log::warn!("[ext4-rw] reap inode {} 失败: {:?}", outcome.inode.raw(), e);
                    }
                }
                0
            }
            Err(_) => -1,
        }
    }

    fn rmdir(&self, name: &str) -> isize {
        let Some(name) = fname(name) else { return -1 };
        let mut fs = self.fs.ext4.lock();
        match fs.remove_empty_directory(self.number, name) {
            Ok(outcome) => {
                if outcome.requires_reap() {
                    let _ = fs.reap_unlinked_inode(outcome.inode);
                }
                0
            }
            Err(_) => -1,
        }
    }

    fn chmod(&self, mode: u16) -> isize {
        let Ok(perm) = FilePermissions::new(mode) else { return -1 };
        let update = InodeMetadataUpdate {
            permissions: Some(perm),
            owner: None,
            device_number: None,
            atime: None,
            mtime: None,
            project_id: None,
            flags: None,
        };
        match self.fs.ext4.lock().update_inode_metadata(self.number, update) {
            Ok(_) => 0,
            Err(_) => -1,
        }
    }

    fn chown(&self, uid: usize, gid: usize) -> isize {
        let update = InodeMetadataUpdate {
            permissions: None,
            owner: Some((uid as u32, gid as u32)),
            device_number: None,
            atime: None,
            mtime: None,
            project_id: None,
            flags: None,
        };
        match self.fs.ext4.lock().update_inode_metadata(self.number, update) {
            Ok(_) => 0,
            Err(_) => -1,
        }
    }

    /// self = 源目录, from = 源名, to = 目标**绝对路径**（可跨目录，仅限本挂载点内）。
    fn rename(&self, from: &str, to: &str) -> isize {
        let Some(from_name) = fname(from) else { return -1 };

        // 去掉挂载点前缀 → 本 fs 内相对路径
        let mp = self.fs.mount_point.trim_end_matches('/');
        let rel = match to.strip_prefix(mp) {
            Some(r) => r,
            None => return -1, // 跨挂载点，交给上层处理（或拒绝）
        };
        // 拆成父目录路径 + 目标名
        let (parent_path, child) = match rel.rfind('/') {
            Some(i) => (&rel[..i], &rel[i + 1..]),
            None => ("", rel),
        };
        let Some(to_name) = fname(child) else { return -1 };

        let mut fs = self.fs.ext4.lock();
        // 从根走目标父目录
        let mut dir = self.fs.root;
        if !parent_path.is_empty() {
            for comp in parent_path.split('/').filter(|c| !c.is_empty()) {
                let Some(cname) = fname(comp) else { return -1 };
                match fs.lookup_child(dir, cname) {
                    Ok(Some(info)) => dir = info.number,
                    _ => return -1,
                }
            }
        }
        match fs.rename(self.number, from_name, dir, to_name, RenameOptions::REPLACE) {
            Ok(_) => 0,
            Err(_) => -1,
        }
    }
}

// File

pub struct Ext4RwFile {
    fs: Arc<Ext4RwFs>,
    number: InodeNumber,
    offset: Mutex<u64>,
    flags: usize,
}

impl Ext4RwFile {
    fn acc(&self) -> usize {
        self.flags & O_ACCMODE
    }
}

impl File for Ext4RwFile {
    fn readable(&self) -> bool {
        self.acc() != O_WRONLY
    }

    fn writable(&self) -> bool {
        self.acc() != O_RDONLY
    }

    fn stat(&self) -> Stat {
        match self.fs.ext4.lock().inode(self.number) {
            Ok(i) => Stat::new(
                STAT_TYPE_FILE,
                i.size as usize,
                i.mode & 0o7777,
                i.uid,
                i.gid,
            )
            .with_mtime(i.mtime),
            Err(_) => Stat::new(STAT_TYPE_FILE, 0, 0, 0, 0),
        }
    }

    fn read(&self, buf: &mut [u8]) -> isize {
        let mut off = self.offset.lock();
        let mut fs = self.fs.ext4.lock();
        match fs.read_inode(self.number, *off, buf) {
            Ok(n) => {
                *off += n as u64;
                n as isize
            }
            Err(_) => -1,
        }
    }

    fn write(&self, buf: &[u8]) -> isize {
        let mut off = self.offset.lock();
        if self.flags & O_APPEND != 0 {
            if let Ok(i) = self.fs.ext4.lock().inode(self.number) {
                *off = i.size;
            }
        }
        let mut fs = self.fs.ext4.lock();
        match fs.write_inode(self.number, *off, buf) {
            Ok(()) => {
                *off += buf.len() as u64;
                buf.len() as isize
            }
            Err(_) => -1,
        }
    }

    fn seek(&self, offset: isize, whence: usize) -> isize {
        let mut off = self.offset.lock();
        let size = self
            .fs
            .ext4
            .lock()
            .inode(self.number)
            .map(|i| i.size)
            .unwrap_or(0);
        let new = match whence {
            0 => offset as i64,
            1 => *off as i64 + offset as i64,
            2 => size as i64 + offset as i64,
            _ => return -1,
        };
        if new < 0 {
            return -1;
        }
        *off = new as u64;
        new as isize
    }

    /// fsync(fd)：rsext4 没有 per-file sync，退化为整个挂载点的全量 sync
    /// （数据缓存→inode 表→bitmap→GDT→超级块→journal commit）。
    /// POSIX 语义上偏强（刷了别的文件），对教学 OS 可接受，文档写明。
    fn fsync(&self) -> isize {
        if self.fs.sync() {
            0
        } else {
            -1
        }
    }
}
