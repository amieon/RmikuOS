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

use crate::fs::mount::FileSystem;
use fatfs::{FileSystem as FatFileSystem, FsOptions, Read};

// fatfs FileSystem 的完整类型(0.4 默认 provider/converter)
type FatFsInner = FatFileSystem<BlockIo, fatfs::NullTimeProvider, fatfs::LossyOemCpConverter>;

pub struct FatFs {
    inner: Mutex<FatFsInner>,
}

// fatfs FileSystem 用了 RefCell,可能 !Sync。和 ext4 一样,单核 + 全局锁兜底。
unsafe impl Send for FatFs {}
unsafe impl Sync for FatFs {}

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
        // 先判断类型 + 读数据,全在持锁的块里完成,只让结果逃出来
        enum FatOpen {
            Dir,
            File(Vec<u8>),
            NotFound,
        }

        let result = {
            let fs = self.fs.inner.lock();
            let fat_path = to_fat_path(&self.path);

            if self.path == "/" {
                FatOpen::Dir
            } else {
                let root = fs.root_dir();

                if root.open_dir(fat_path).is_ok() {
                    FatOpen::Dir
                } else {
                    match root.open_file(fat_path) {
                        Ok(mut file) => {
                            use fatfs::Read;
                            let mut data = Vec::new();
                            let mut buf = [0u8; 512];
                            let mut ok = true;
                            loop {
                                match file.read(&mut buf) {
                                    Ok(0) => break,
                                    Ok(n) => data.extend_from_slice(&buf[..n]),
                                    Err(_) => { ok = false; break; }
                                }
                            }
                            if ok { FatOpen::File(data) } else { FatOpen::NotFound }
                        }
                        Err(_) => FatOpen::NotFound,
                    }
                }
            }
        };  // ← fs/root/file 全析构,result 是独立的(Vec 或枚举)

        // 锁已释放,现在用结果构造 FileRef
        match result {
            FatOpen::Dir => Some(Arc::new(ReadOnlyDirFile::new(self.getdents()))),
            FatOpen::File(data) => Some(Arc::new(ReadOnlyMemFile::from_vec(data))),
            FatOpen::NotFound => None,
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
}

pub fn init(device: Arc<dyn BlockDevice>) {
    let num_sectors = device.num_blocks() as u64;
    let fs = FatFs::load(device, num_sectors)
        .expect("[fat] load failed");
    crate::fs::mount::mount("/fat", fs);
    log::info!("[fat] mounted at /fat");
}