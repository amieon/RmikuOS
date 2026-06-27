extern crate alloc;

use alloc::string::String;
use alloc::sync::Arc;
use alloc::vec::Vec;

use crate::block::blockio::BlockIo;
use crate::block::BlockDevice;
use crate::sync::spin::Mutex;

use super::dirent::{DirEntry, FILE_TYPE_DIR, FILE_TYPE_FILE};
use super::file::FileRef;
use super::inode::{Inode, InodeRef, InodeType, Metadata};
use super::{ReadOnlyDirFile, ReadOnlyMemFile};


use fatfs::{FileSystem, FsOptions, Read};

// fatfs FileSystem 的完整类型(0.4 默认 provider/converter)
type FatFsInner = FileSystem<BlockIo, fatfs::NullTimeProvider, fatfs::LossyOemCpConverter>;

pub struct FatFs {
    inner: Mutex<FatFsInner>,
}

// fatfs FileSystem 用了 RefCell,可能 !Sync。和 ext4 一样,单核 + 全局锁兜底。
unsafe impl Send for FatFs {}
unsafe impl Sync for FatFs {}

impl FatFs {
    pub fn load(device: Arc<dyn BlockDevice>, num_sectors: u64) -> Option<Arc<Self>> {
        let io = BlockIo::new(device, num_sectors);
        let fs = match FileSystem::new(io, FsOptions::new()) {
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
        let fs = self.fs.inner.lock();
        let root = fs.root_dir();

        // 根目录特判
        if self.path == "/" {
            return Metadata { inode_type: InodeType::Directory, size: 0 };
        }

        let fat_path = to_fat_path(&self.path);

        // 先试当目录打开
        if root.open_dir(fat_path).is_ok() {
            return Metadata { inode_type: InodeType::Directory, size: 0 };
        }

        // 再试当文件,取大小
        match root.open_file(fat_path) {
            Ok(file) => {
                // fatfs File 没有直接的 len(),要 seek 到末尾
                // 或者通过遍历父目录拿 entry.len()。先简单:用 seek
                use fatfs::{Seek, SeekFrom};
                let mut f = file;
                let size = f.seek(SeekFrom::End(0)).unwrap_or(0) as usize;
                Metadata { inode_type: InodeType::File, size }
            }
            Err(_) => Metadata { inode_type: InodeType::File, size: 0 },
        }
    }

    fn lookup(&self, name: &str) -> Option<InodeRef> {
        if name.is_empty() || name == "." {
            return Some(Arc::new(Self {
                fs: self.fs.clone(),
                path: self.path.clone(),
            }));
        }
        if name == ".." {
            return Some(self.fs.clone().root_inode());  // 兜底回根
        }

        let child_path = join_path(&self.path, name);
        let fat_path = to_fat_path(&child_path);

        let fs = self.fs.inner.lock();
        let root = fs.root_dir();

        // 检查 child 是否存在(目录或文件)
        let exists = root.open_dir(fat_path).is_ok() || root.open_file(fat_path).is_ok();

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
        let fs = self.fs.inner.lock();
        let root = fs.root_dir();
        let fat_path = to_fat_path(&self.path);

        // 根目录 / 是目录
        if self.path == "/" {
            drop(fs);  // 释放锁,getdents 自己会再 lock
            return Some(Arc::new(ReadOnlyDirFile::new(self.getdents())));
        }

        // 是目录?
        if root.open_dir(fat_path).is_ok() {
            drop(fs);
            return Some(Arc::new(ReadOnlyDirFile::new(self.getdents())));
        }

        // 是文件?读全部进内存(照搬 ext4 的套路)
        match root.open_file(fat_path) {
            Ok(mut file) => {
                let mut data = Vec::new();
                let mut buf = [0u8; 512];
                loop {
                    match file.read(&mut buf) {
                        Ok(0) => break,
                        Ok(n) => data.extend_from_slice(&buf[..n]),
                        Err(_) => return None,
                    }
                }
                Some(Arc::new(ReadOnlyMemFile::from_vec(data)))
            }
            Err(_) => None,
        }
    }

    fn getdents(&self) -> Vec<DirEntry> {
        let mut entries = Vec::new();
        let fs = self.fs.inner.lock();
        let root = fs.root_dir();
        let fat_path = to_fat_path(&self.path);

        // 拿到目标目录
        let dir = if self.path == "/" {
            root
        } else {
            match root.open_dir(fat_path) {
                Ok(d) => d,
                Err(_) => return entries,
            }
        };

        for entry_result in dir.iter() {
            let entry = match entry_result {
                Ok(e) => e,
                Err(_) => continue,
            };

            let name = entry.file_name();
            if name == "." || name == ".." {
                continue;
            }

            let dirent_type = if entry.is_dir() {
                FILE_TYPE_DIR
            } else {
                FILE_TYPE_FILE
            };

            entries.push(DirEntry::new(&name, dirent_type));
        }

        entries
    }
}

pub fn init(device: Arc<dyn BlockDevice>, num_sectors: u64) {
    let fs = FatFs::load(device, num_sectors)
        .expect("[fat] load failed");
    crate::fs::mount::mount("/fat", fs);
    log::info!("[fat] mounted at /fat");
}