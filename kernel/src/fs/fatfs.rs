extern crate alloc;

use core::result;

use alloc::string::String;
use alloc::sync::Arc;
use alloc::vec::Vec;
use alloc::string::ToString;

use crate::drivers::block::blockio::BlockIo;
use crate::drivers::block::BlockDevice;
use crate::fs::flag::*;
use crate::sync::spin::Mutex;

use super::dirent::{DirEntry, FILE_TYPE_DIR, FILE_TYPE_FILE};
use super::file::FileRef;
use super::inode::{Inode, InodeRef, InodeType, Metadata};
use super::{ReadOnlyDirFile, ReadOnlyMemFile};

use crate::fs::mount::FileSystem;
use fatfs::{FileSystem as FatFileSystem, FsOptions, Read};

use super::file::File;
use super::stat::{Stat, STAT_TYPE_FILE};

// fatfs FileSystem 的完整类型(0.4 默认 provider/converter)
type FatFsInner = FatFileSystem<BlockIo, fatfs::NullTimeProvider, fatfs::LossyOemCpConverter>;

pub struct FatFs {
    inner: Mutex<FatFsInner>,
    dev: Arc<dyn BlockDevice>,  // 保存块设备引用,供 fsync 真正刷盘
}

// fatfs FileSystem 用了 RefCell,可能 !Sync。和 ext4 一样,单核 + 全局锁兜底。
unsafe impl Send for FatFs {}
unsafe impl Sync for FatFs {}





pub struct FatFile {
    fs: Arc<FatFs>,
    path: String,      
    offset: Mutex<u64>,
    flags: usize,
}

impl FatFile {
    pub fn new(fs: Arc<FatFs>, path: String, flags: usize) -> Self {
        Self { fs, path, offset: Mutex::new(0), flags }
    }
    pub fn is_append(&self) -> bool {
        return (self.flags & O_APPEND) != 0;
    }
}

impl File for FatFile {
     fn readable(&self) -> bool {
        let access = self.flags & O_ACCMODE;     
        access == O_RDONLY || access == O_RDWR
    }

    fn writable(&self) -> bool {
        let access = self.flags& O_ACCMODE;
        access == O_WRONLY || access == O_RDWR
    }

    fn read(&self, buf: &mut [u8]) -> isize {
        let mut off = self.offset.lock();
        let cur = *off;

        let n = {
            let fs = self.fs.inner.lock();
            let root = fs.root_dir();
            let result = match root.open_file(&self.path) {   // ← 存进 result
                Ok(mut file) => {
                    use fatfs::{Read, Seek, SeekFrom};
                    if file.seek(SeekFrom::Start(cur)).is_err() {
                        -1
                    } else {
                        // FAT crate 的 read 一次最多读一个簇,必须循环直到填满 buf 或 EOF
                        let mut total: usize = 0;
                        let mut err = false;
                        loop {
                            match file.read(&mut buf[total..]) {
                                Ok(r) => {
                                    if r == 0 { break; }
                                    total += r;
                                    if total >= buf.len() { break; }
                                }
                                Err(_) => { err = true; break; }
                            }
                        }
                        if err { -1 } else { total as isize }
                    }
                }
                Err(_) => -1,
            };
            result
        };

        if n > 0 {
            *off += n as u64;
        }
        n
    }

    fn write(&self, buf: &[u8]) -> isize {
        let mut off = self.offset.lock();
        let cur = *off;

        let n = {
            let fs = self.fs.inner.lock();
            let root = fs.root_dir();
            let result = match root.open_file(&self.path) {
                Ok(mut file) => {
                    use fatfs::{Write, Seek, SeekFrom};

                    let seek_target = if self.is_append() {
                        SeekFrom::End(0)
                    } else {
                        SeekFrom::Start(cur)
                    };

                    match file.seek(seek_target) {
                        Ok(new_pos) => {
                            // FAT crate 的 write 一次最多写一个簇,必须循环直到写完整个 buf
                            let mut total: usize = 0;
                            let mut err = false;
                            loop {
                                match file.write(&buf[total..]) {
                                    Ok(w) => {
                                        if w == 0 { break; }
                                        total += w;
                                        if total >= buf.len() { break; }
                                    }
                                    Err(_) => { err = true; break; }
                                }
                            }
                            let _ = file.flush();
                            if err { (-1, cur) } else { (total as isize, new_pos + total as u64) }
                        }
                        Err(_) => (-1, cur),
                    }
                }
                Err(_) => (-1, cur),
            };
            result
        };

        // 更新 offset
        let (ret, new_off) = n;
        if ret > 0 {
            *off = new_off;
        }
        ret
    }
    fn stat(&self) -> Stat {
        let size = {
            let fs = self.fs.inner.lock();
            let root = fs.root_dir();
            let result = match root.open_file(&self.path) {
                Ok(mut file) => {
                    use fatfs::{Seek, SeekFrom};
                    file.seek(SeekFrom::End(0)).unwrap_or(0) as usize
                }
                Err(_) => 0,
            };
            result
        };
        Stat::new(STAT_TYPE_FILE, size, 0o644, 0, 0).with_mtime(crate::timer::now_secs() as u32)
    }

    fn seek(&self, offset: isize, whence: usize) -> isize {
        let mut off = self.offset.lock();
        let new = match whence {
            0 => offset as i64,
            1 => *off as i64 + offset as i64,
            2 => {
                // SEEK_END: 先拿到文件大小
                let size = {
                    let fs = self.fs.inner.lock();
                    let root = fs.root_dir();
                    let r =match root.open_file(&self.path) {
                        Ok(mut f) => {
                            use fatfs::{Seek, SeekFrom};
                            f.seek(SeekFrom::End(0)).unwrap_or(0) as i64
                        }
                        Err(_) => return -1,
                    };
                    r
                };
                size + offset as i64
            }
            _ => return -1,
        };
        if new < 0 {
            return -1;
        }
        *off = new as u64;
        new as isize
    }

    fn ftruncate(&self, len: usize) -> isize {
        if !self.writable() {
            return -1;
        }
        // self.path 在构造时已是 FAT 格式路径,无需再 to_fat_path
        let fs = self.fs.inner.lock();
        let root = fs.root_dir();
        let r = match root.open_file(&self.path) {
            Ok(mut f) => {
                use fatfs::{Write, Seek, SeekFrom};
                match f.seek(SeekFrom::Start(len as u64)) {
                    Ok(_) => match f.truncate() { Ok(_) => 0, Err(_) => -1 },
                    Err(_) => -1,
                }
            }
            Err(_) => -1,
        };
        r
    }

    fn fsync(&self) -> isize {
        // 把 fatfs 内部缓冲刷到 BlockIo,再让 BlockIo 真正落到块设备
        {
            // self.path 在构造时已是 FAT 格式路径(无前导 /)
            let fs = self.fs.inner.lock();
            let root = fs.root_dir();
            // 注意末尾分号:让 open_file 的临时 Result 在语句结束就 drop,
            // 否则它作为块尾表达式会活得比 fs 锁守卫久,触发 E0597
            if let Ok(mut f) = root.open_file(&self.path) {
                use fatfs::Write;
                let _ = f.flush();
            };
        }
        self.fs.dev.flush()
    }
}


impl FatFs {
    pub fn load(device: Arc<dyn BlockDevice>, num_sectors: u64) -> Option<Arc<Self>> {
        let io = BlockIo::new(device.clone(), num_sectors);
        let fs = match FatFileSystem::new(io, FsOptions::new()) {
            Ok(fs) => fs,
            Err(e) => {
                log::error!("[fat] load failed: {:?}", e);
                return None;
            }
        };
        log::info!("[fat] filesystem loaded");
        Some(Arc::new(Self { inner: Mutex::new(fs), dev: device }))
    }
}

impl super::mount::FileSystem for FatFs {
    fn root_inode(self: Arc<Self>) -> InodeRef {
        Arc::new(FatInode {
            fs: self,
            path: String::from("/"),
        })
    }
}

pub struct FatInode {
    fs: Arc<FatFs>,
    path: String,
}

fn join_path(parent: &str, name: &str) -> String {
    if parent == "/" {
        let mut s = String::from("/");
        s.push_str(name);
        s
    } else {
        let mut s = String::from(parent);
        s.push('/');
        s.push_str(name);
        s
    }
}

fn to_fat_path(path: &str) -> &str {
    path.trim_start_matches('/')
}

impl Inode for FatInode {
    fn metadata(&self) -> Metadata {
        // FAT 没有 unix 权限位, 统一映射: 目录 0o755, 文件 0o644, 全部 root 拥有。
        const FAT_DIR_MODE: u16 = 0o755;
        const FAT_FILE_MODE: u16 = 0o644;

        if self.path == "/" {
            return Metadata { inode_type: InodeType::Directory, size: 0, uid: 0, gid: 0, mode: FAT_DIR_MODE };
        }

        let fs = self.fs.inner.lock();
        let fat_path = to_fat_path(&self.path);

        // 所有 fatfs 操作收进这个块,只让 Metadata(独立数据)逃出来
        let meta = {
            let root = fs.root_dir();

            if root.open_dir(fat_path).is_ok() {
                Metadata { inode_type: InodeType::Directory, size: 0, uid: 0, gid: 0, mode: FAT_DIR_MODE }
            } else {
                match root.open_file(fat_path) {
                    Ok(mut file) => {
                        use fatfs::{Seek, SeekFrom};
                        let size = file.seek(SeekFrom::End(0)).unwrap_or(0) as usize;
                        Metadata { inode_type: InodeType::File, size, uid: 0, gid: 0, mode: FAT_FILE_MODE }
                    }
                    Err(_) => Metadata { inode_type: InodeType::File, size: 0, uid: 0, gid: 0, mode: FAT_FILE_MODE },
                }
            }
        };  // ← root, file 在这里析构,fs 还活着,meta 是独立的

        meta
    }  // ← fs 在这里析构

    fn lookup(&self, name: &str) -> Option<InodeRef> {
        if name.is_empty() || name == "." {
            return Some(Arc::new(Self {
                fs: self.fs.clone(),
                path: self.path.clone(),
            }));
        }
        if name == ".." {
            return Some(self.fs.clone().root_inode());
        }

        let child_path = join_path(&self.path, name);   // String,你拥有

        let exists = {
            let fs = self.fs.inner.lock();
            let root = fs.root_dir();
            let fat_path = to_fat_path(&child_path);    // &str,借用 child_path(不借用 fs,OK)
            root.open_dir(fat_path).is_ok() || root.open_file(fat_path).is_ok()
        };  // fs/root 析构;fat_path 借用的是 child_path 不是 fs,块结束就还了

        if exists {
            Some(Arc::new(Self {
                fs: self.fs.clone(),
                path: child_path,
            }))
        } else {
            None
        }
    }

    fn open(&self, flags : usize) -> Option<FileRef> {
        // 判断类型(在块里),拿到结论后构造
        enum Kind { Dir, File, NotFound }

        let kind = {
            let fs = self.fs.inner.lock();
            let fat_path = to_fat_path(&self.path);

            if self.path == "/" {
                Kind::Dir
            } else {
                let root = fs.root_dir();
                if root.open_dir(fat_path).is_ok() {
                    Kind::Dir
                } else if root.open_file(fat_path).is_ok() {
                    Kind::File
                } else {
                    Kind::NotFound
                }
            }
        };

        match kind {
            Kind::Dir => Some(Arc::new(ReadOnlyDirFile::new(self.getdents()))),
            Kind::File => {
                // 可写 FatFile,存 owned 路径
                let fat_path = to_fat_path(&self.path).to_string();
                Some(Arc::new(FatFile::new(self.fs.clone(), fat_path, flags)))
            }
            Kind::NotFound => None,
        }
    }
    fn getdents(&self) -> Vec<DirEntry> {
        let mut entries = Vec::new();
        let fs = self.fs.inner.lock();
        let fat_path = to_fat_path(&self.path);

        {
            let root = fs.root_dir();
            if self.path == "/" {
                for er in root.iter() {
                    if let Ok(e) = er {
                        let name = e.file_name();
                        if name != "." && name != ".." {
                            let t = if e.is_dir() { FILE_TYPE_DIR } else { FILE_TYPE_FILE };
                            entries.push(DirEntry::new(&name, t));
                        }
                    }
                }
            } else if let Ok(dir) = root.open_dir(fat_path) {
                for er in dir.iter() {
                    if let Ok(e) = er {
                        let name = e.file_name();
                        if name != "." && name != ".." {
                            let t = if e.is_dir() { FILE_TYPE_DIR } else { FILE_TYPE_FILE };
                            entries.push(DirEntry::new(&name, t));
                        }
                    }
                }
            }
        }

        entries
    }

    fn create(&self, name: &str) -> Option<InodeRef> {
        let child_path = join_path(&self.path, name);
        let fat_path = to_fat_path(&child_path).to_string();  // owned,给 FatFile 用

        let ok = {
            let fs = self.fs.inner.lock();
            let root = fs.root_dir();
            // create_file:存在则打开,不存在则创建。要求"新建"语义可先查重
            let ret = match root.create_file(&fat_path) {
                Ok(_file) => true,    // file 在块内析构
                Err(_) => false,
            };
            ret
        };

        if ok {
            Some(Arc::new(FatInode {
                fs: self.fs.clone(),
                path: child_path,
            }))
        } else {
            None
        }
    }

    fn truncate(&self) -> isize {
        let fat_path = to_fat_path(&self.path).to_string();
        let fs = self.fs.inner.lock();
        let root = fs.root_dir();
        let r = match root.open_file(&fat_path) {
            Ok(mut f) => {
                use fatfs::Write;  // 或 fatfs::File 的方法,看 0.4
                match f.truncate() { Ok(_) => 0, Err(_) => -1 }
            }
            Err(_) => -1,
        };
        r
    }

    fn rename(&self, from: &str, to: &str) -> isize {
        log::info!("[rename-debug] FatInode::rename from={} to={}", from, to);
        // self = 源所在目录; from = 源名; to = 目标完整绝对路径(可跨目录)。
        let src = to_fat_path(&join_path(&self.path, from)).to_string();
        log::info!("[rename-debug] fat src={}", src);

        // 解析目标父目录(相对 root 的路径)与目标名
        let trimmed = to.trim_end_matches('/');
        let (parent, name) = match trimmed.rfind('/') {
            Some(0) => ("", &trimmed[1..]),
            Some(pos) => (&trimmed[..pos], &trimmed[pos + 1..]),
            None => ("", trimmed),
        };
        let dest_parent_fat = to_fat_path(parent);
        let dest_name = name;
        log::info!("[rename-debug] fat parent={} name={}", dest_parent_fat, dest_name);

        let fs = self.fs.inner.lock();
        let root = fs.root_dir();

        // 目标若是已存在的文件,先删以支持编辑器式原子覆盖保存
        let dest_fat_full = to_fat_path(to).to_string();
        if root.open_file(&dest_fat_full).is_ok() {
            log::info!("[rename-debug] fat removing existing dest {}", dest_fat_full);
            let _ = root.remove(&dest_fat_full);
        }

        let r = if dest_parent_fat.is_empty() {
            match root.rename(&src, &root, dest_name) {
                Ok(_) => 0,
                Err(_) => { log::info!("[rename-debug] fat rename(same-dir) failed"); -1 }
            }
        } else {
            match root.open_dir(dest_parent_fat) {
                Ok(dest_dir) => match root.rename(&src, &dest_dir, dest_name) {
                    Ok(_) => 0,
                    Err(_) => { log::info!("[rename-debug] fat rename failed"); -1 }
                },
                Err(_) => { log::info!("[rename-debug] fat open_dir('{}') failed", dest_parent_fat); -1 }
            }
        };
        log::info!("[rename-debug] fat result={}", r);
        r
    }

    fn truncate_to(&self, len: usize) -> isize {
        let fat_path = to_fat_path(&self.path).to_string();
        let fs = self.fs.inner.lock();
        let root = fs.root_dir();
        let r = match root.open_file(&fat_path) {
            Ok(mut f) => {
                use fatfs::{Write, Seek, SeekFrom};
                match f.seek(SeekFrom::Start(len as u64)) {
                    Ok(_) => match f.truncate() { Ok(_) => 0, Err(_) => -1 },
                    Err(_) => -1,
                }
            }
            Err(_) => -1,
        };
        r
    }

    fn mkdir(&self, name: &str) -> Option<InodeRef> {
        let child_path = join_path(&self.path, name);
        let fat_path = to_fat_path(&child_path).to_string();

        let ok = {
            let fs = self.fs.inner.lock();
            let root = fs.root_dir();
            let ret = match root.create_dir(&fat_path) {
                Ok(_dir) => true,
                Err(_) => false,
            };
            ret
        };

        if ok {
            Some(Arc::new(FatInode {
                fs: self.fs.clone(),
                path: child_path,
            }))
        } else {
            None
        }
    }

    fn unlink(&self, name: &str) -> isize {
        let child_path = join_path(&self.path, name);
        let fat_path = to_fat_path(&child_path).to_string();

        let fs = self.fs.inner.lock();
        let root = fs.root_dir();

        // 确认是文件(不是目录),再删
        if root.open_dir(&fat_path).is_ok() {
            return -1;   // 是目录,unlink 拒绝
        }
        if root.open_file(&fat_path).is_err() {
            return -1;   // 不存在
        }

        match root.remove(&fat_path) {
            Ok(_) => 0,
            Err(_) => -1,
        }
    }

    fn rmdir(&self, name: &str) -> isize {
        let child_path = join_path(&self.path, name);
        let fat_path = to_fat_path(&child_path).to_string();

        let fs = self.fs.inner.lock();
        let root = fs.root_dir();

        // 确认是目录 + 是空的
        let dir = match root.open_dir(&fat_path) {
            Ok(d) => d,
            Err(_) => return -1,   // 不是目录或不存在
        };

        // 检查空:遍历看有没有非 . / .. 的条目
        let mut empty = true;
        for entry in dir.iter() {
            if let Ok(e) = entry {
                let n = e.file_name();
                if n != "." && n != ".." {
                    empty = false;
                    break;
                }
            }
        }
        if !empty {
            return -1;   // 非空目录,拒绝
        }

        // fatfs 的 remove 对空目录也用 remove
        drop(dir);   // 先释放 dir(它借用 root),再 remove
        match root.remove(&fat_path) {
            Ok(_) => 0,
            Err(_) => -1,
        }
    }

    fn remove_recursive(&self, name: &str) -> isize {
        // fatfs 的 remove 不能删非空目录。要递归:先删子项再删自己。
        let child_path = join_path(&self.path, name);
        let fat_path = to_fat_path(&child_path).to_string();

        // 先判断是文件还是目录
        let is_dir = {
            let fs = self.fs.inner.lock();
            let root = fs.root_dir();
            let ret = root.open_dir(&fat_path).is_ok();
            ret
        };

        if !is_dir {
            // 文件,直接删
            let fs = self.fs.inner.lock();
            let root = fs.root_dir();
            return match root.remove(&fat_path) {
                Ok(_) => 0,
                Err(_) => -1,
            };
        }

        // 目录:先递归删所有子项
        // 收集子项名字(在块内),再逐个递归删
        let children: Vec<String> = {
            let fs = self.fs.inner.lock();
            let root = fs.root_dir();
            let dir = match root.open_dir(&fat_path) {
                Ok(d) => d,
                Err(_) => return -1,
            };
            let mut names = Vec::new();
            for entry in dir.iter() {
                if let Ok(e) = entry {
                    let n = e.file_name();
                    if n != "." && n != ".." {
                        names.push(n);
                    }
                }
            }
            names
        };

        // 对这个目录的 Inode,递归删每个子项
        let child_inode = FatInode {
            fs: self.fs.clone(),
            path: child_path.clone(),
        };
        for cname in children {
            if child_inode.remove_recursive(&cname) != 0 {
                return -1;
            }
        }

        // 子项都删完,删空目录自己
        let fs = self.fs.inner.lock();
        let root = fs.root_dir();
        match root.remove(&fat_path) {
            Ok(_) => 0,
            Err(_) => -1,
        }
    }
}

pub fn init(device: Arc<dyn BlockDevice>) {
    let num_sectors = device.num_blocks() as u64;
    let fs = FatFs::load(device, num_sectors)
        .expect("[fat] load failed");
    crate::fs::mount::mount("/fat", fs);
    log::info!("[fat] mounted at /fat");
}