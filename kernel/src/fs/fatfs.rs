extern crate alloc;

use core::result;

use alloc::string::String;
use alloc::sync::Arc;
use alloc::vec::Vec;
use alloc::string::ToString;

use crate::block::blockio::BlockIo;
use crate::block::BlockDevice;
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
}

// fatfs FileSystem 用了 RefCell,可能 !Sync。和 ext4 一样,单核 + 全局锁兜底。
unsafe impl Send for FatFs {}
unsafe impl Sync for FatFs {}





pub struct FatFile {
    fs: Arc<FatFs>,
    path: String,        // fatfs 相对路径(不带开头 /)
    offset: Mutex<u64>,
}

impl FatFile {
    pub fn new(fs: Arc<FatFs>, path: String) -> Self {
        Self { fs, path, offset: Mutex::new(0) }
    }
}

impl File for FatFile {
    fn readable(&self) -> bool { true }
    fn writable(&self) -> bool { true }

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
                        match file.read(buf) {
                            Ok(n) => n as isize,
                            Err(_) => -1,
                        }
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
                    if file.seek(SeekFrom::Start(cur)).is_err() {
                        -1
                    } else {
                        match file.write(buf) {
                            Ok(n) => {
                                let _ = file.flush();
                                n as isize
                            }
                            Err(_) => -1,
                        }
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
        Stat::new(STAT_TYPE_FILE, size)
    }
}


impl FatFs {
    pub fn load(device: Arc<dyn BlockDevice>, num_sectors: u64) -> Option<Arc<Self>> {
        let io = BlockIo::new(device, num_sectors);
        let fs = match FatFileSystem::new(io, FsOptions::new()) {
            Ok(fs) => fs,
            Err(e) => {
                log::error!("[fat] load failed: {:?}", e);
                return None;
            }
        };
        log::info!("[fat] filesystem loaded");
        Some(Arc::new(Self { inner: Mutex::new(fs) }))
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
        if self.path == "/" {
            return Metadata { inode_type: InodeType::Directory, size: 0 };
        }

        let fs = self.fs.inner.lock();
        let fat_path = to_fat_path(&self.path);

        // 所有 fatfs 操作收进这个块,只让 Metadata(独立数据)逃出来
        let meta = {
            let root = fs.root_dir();

            if root.open_dir(fat_path).is_ok() {
                Metadata { inode_type: InodeType::Directory, size: 0 }
            } else {
                match root.open_file(fat_path) {
                    Ok(mut file) => {
                        use fatfs::{Seek, SeekFrom};
                        let size = file.seek(SeekFrom::End(0)).unwrap_or(0) as usize;
                        Metadata { inode_type: InodeType::File, size }
                    }
                    Err(_) => Metadata { inode_type: InodeType::File, size: 0 },
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

    fn open(&self) -> Option<FileRef> {
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
                Some(Arc::new(FatFile::new(self.fs.clone(), fat_path)))
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