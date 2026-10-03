use alloc::string::String;
use alloc::sync::Arc;
use alloc::vec::Vec;

use crate::drivers::block::BlockDevice;
use crate::fs::flag::{O_ACCMODE, O_APPEND, O_RDONLY, O_WRONLY};
use crate::sync::spin::Mutex;
use core::sync::atomic::{AtomicUsize, Ordering};

use super::dirent::{DirEntry, FILE_TYPE_DIR, FILE_TYPE_FILE};
use super::file::{File, FileRef};
use super::inode::{Inode, InodeRef, InodeType, Metadata};
use super::mount::{self, FileSystem};
use super::stat::{Stat, STAT_TYPE_FILE};
use super::ReadOnlyDirFile;

use crate::fs::rsext4_adapter::{format_ext4, mount_ext4, Ext4Mount};
use rsext4::{
    DirectoryCursor, DirectoryEntryType, FileName, FilePermissions, InodeMetadataUpdate,
    InodeNumber, MutationContext, RenameOptions,
};

fn ctx() -> MutationContext {
    // TODO: 多用户后从当前进程取 uid/gid/umask
    MutationContext::new(0, 0, 0, 0o022)
}

fn fname(name: &str) -> Option<FileName<'_>> {
    FileName::new(name.as_bytes()).ok()
}

pub struct Ext4RwFs {
    ext4: Mutex<Ext4Mount>,
    root: InodeNumber,
    mount_point: String,
}

/// 数据盘文件系统的全局句柄：writeback timer 钩子从这里取。
static DATA_FS: Mutex<Option<Arc<Ext4RwFs>>> = Mutex::new(None);

/// timer 周期钩子（trap 里 net::on_timer_tick() 旁边调用）：限频 try_sync。
///
/// 这是 writeback 机制：把崩溃窗口从"无限"压到"一个周期"。
/// 中断上下文约束 → 全程 try_lock，锁忙即跳过，绝不自旋阻塞。
/// riscv64: INTERVAL=10_000 @ 10MHz timebase ≈ 1ms/tick → 5000 ticks ≈ 5s
pub fn on_timer_tick() {
    const WRITEBACK_EVERY_TICKS: usize = 5000;
    static COUNTER: AtomicUsize = AtomicUsize::new(0);

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
    /// main.rs: `fs::ext4_rw::Ext4RwFs::init_and_mount("/home", dev);`
    pub fn init_and_mount(mount_point: &str, dev: Arc<dyn BlockDevice>) {
        match Self::mount_or_format(dev, mount_point) {
            Some(fs) => {
                mount::mount(mount_point, fs.clone());
                // 全局句柄：writeback timer 钩子从这里拿
                *DATA_FS.lock() = Some(fs);
                log::info!("[ext4-rw] {} 挂载完成 (rsext4, 可写, JBD2)", mount_point);
            }
            None => log::error!("[ext4-rw] {} 挂载失败", mount_point),
        }
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
    pub fn try_sync(&self) -> bool {
        match self.ext4.try_lock() {
            Some(mut fs) => match fs.sync() {
                Ok(()) => true,
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
    /// POSIX 语义上偏强（刷了别的文件），对教学 OS 可接受，文档写明即可。
    fn fsync(&self) -> isize {
        if self.fs.sync() {
            0
        } else {
            -1
        }
    }
}
