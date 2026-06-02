extern crate alloc;

use alloc::boxed::Box;
use alloc::string::String;
use alloc::sync::Arc;
use alloc::vec::Vec;
use core::fmt;

use ext4_view::{
    Ext4,
    Ext4Read,
    FileType,
};

use crate::block::{
    get_block_cache,
    BlockDevice,
    BLOCK_SIZE,
};
use crate::sync::spin::Mutex;

use super::dirent::{
    DirEntry,
    FILE_TYPE_DIR,
    FILE_TYPE_FILE,
};
use super::file::FileRef;
use super::inode::{
    Inode,
    InodeRef,
    InodeType,
    Metadata,
};
use super::{
    ReadOnlyDirFile,
    ReadOnlyMemFile,
};

#[derive(Debug)]
struct Ext4BlockReadError;

impl fmt::Display for Ext4BlockReadError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.write_str("ext4 block read error")
    }
}

impl core::error::Error for Ext4BlockReadError {}

pub struct CachedBlockReader {
    device: Arc<dyn BlockDevice>,
}

impl CachedBlockReader {
    pub fn new(device: Arc<dyn BlockDevice>) -> Self {
        Self {
            device,
        }
    }
}

impl Ext4Read for CachedBlockReader {
    fn read(
        &mut self,
        start_byte: u64,
        dst: &mut [u8],
    ) -> Result<(), Box<dyn core::error::Error + Send + Sync + 'static>> {
        let mut copied = 0usize;
        let mut offset = start_byte as usize;

        while copied < dst.len() {
            let block_id = offset / BLOCK_SIZE;
            let block_offset = offset % BLOCK_SIZE;
            let copy_len = core::cmp::min(
                BLOCK_SIZE - block_offset,
                dst.len() - copied,
            );

            let cache = get_block_cache(block_id, self.device.clone());
            let block = cache.lock();

            let ret = block.read_bytes(
                block_offset,
                &mut dst[copied..copied + copy_len],
            );

            if ret < 0 {
                return Err(Box::new(Ext4BlockReadError));
            }

            copied += copy_len;
            offset += copy_len;
        }

        Ok(())
    }
}


pub struct Ext4Fs {
    inner: Mutex<Ext4>,
}


//ext4_view::Ext4 文档里是 !Send / !Sync。
//当前先按单核 + 全局锁使用。
//后面认真多核时，这块要重新设计。

unsafe impl Send for Ext4Fs {}
unsafe impl Sync for Ext4Fs {}

impl Ext4Fs {
    pub fn load(device: Arc<dyn BlockDevice>) -> Option<Arc<Self>> {
        let reader = CachedBlockReader::new(device);

        let fs = match Ext4::load(Box::new(reader)) {
            Ok(fs) => fs,
            Err(e) => {
                log::error!("[ext4] load failed: {:?}", e);
                return None;
            }
        };

        log::info!("[ext4] filesystem loaded");

        Some(Arc::new(Self {
            inner: Mutex::new(fs),
        }))
    }

    pub fn root_inode(self: &Arc<Self>) -> InodeRef {
        Arc::new(Ext4Inode {
            fs: self.clone(),
            path: String::from("/"),
        })
    }
}

pub struct Ext4Inode {
    fs: Arc<Ext4Fs>,
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

fn inode_type_from_ext4(file_type: FileType) -> Option<InodeType> {
    if file_type.is_dir() {
        Some(InodeType::Directory)
    } else if file_type.is_regular_file() {
        Some(InodeType::File)
    } else {
        None
    }
}

fn dirent_type_from_ext4(file_type: FileType) -> Option<u8> {
    if file_type.is_dir() {
        Some(FILE_TYPE_DIR)
    } else if file_type.is_regular_file() {
        Some(FILE_TYPE_FILE)
    } else {
        None
    }
}


impl Inode for Ext4Inode {
    fn metadata(&self) -> Metadata {
        let fs = self.fs.inner.lock();

        let meta = fs
            .metadata(self.path.as_str())
            .expect("[ext4] metadata failed");

        let inode_type = inode_type_from_ext4(meta.file_type())
            .unwrap_or(InodeType::File);

        Metadata {
            inode_type,
            size: meta.len() as usize,
        }
    }

    fn lookup(&self, name: &str) -> Option<InodeRef> {
        if name.is_empty() || name == "." {
            return Some(Arc::new(Self {
                fs: self.fs.clone(),
                path: self.path.clone(),
            }));
        }

        /*
         * path::normalize_path 正常会提前处理 ..
         * 这里兜底：.. 先回根目录。
         */
        if name == ".." {
            return Some(self.fs.root_inode());
        }

        let child_path = join_path(&self.path, name);

        let fs = self.fs.inner.lock();

        match fs.exists(child_path.as_str()) {
            Ok(true) => Some(Arc::new(Self {
                fs: self.fs.clone(),
                path: child_path,
            })),
            _ => None,
        }
    }

    fn open(&self) -> Option<FileRef> {
        let meta = {
            let fs = self.fs.inner.lock();
            fs.metadata(self.path.as_str()).ok()?
        };

        let file_type = meta.file_type();

        if file_type.is_dir() {
            return Some(Arc::new(ReadOnlyDirFile::new(self.getdents())));
        }

        if file_type.is_regular_file() {
            let data = {
                let fs = self.fs.inner.lock();
                fs.read(self.path.as_str()).ok()?
            };

            return Some(Arc::new(ReadOnlyMemFile::from_vec(data)));
        }

        None
    }

    fn getdents(&self) -> Vec<DirEntry> {
        let mut entries = Vec::new();

        let fs = self.fs.inner.lock();

        let read_dir = match fs.read_dir(self.path.as_str()) {
            Ok(read_dir) => read_dir,
            Err(_) => return entries,
        };

        for entry_result in read_dir {
            let entry = match entry_result {
                Ok(entry) => entry,
                Err(_) => continue,
            };

            let file_type = match entry.file_type() {
                Ok(file_type) => file_type,
                Err(_) => continue,
            };

            let Some(dirent_type) = dirent_type_from_ext4(file_type) else {
                continue;
            };

            let name_buf = entry.file_name();

            
            // ext4 文件名本质是字节串。 DirEntry::new 现在吃 &str，
            // 所以第一版只支持 UTF-8 文件名。
            //  /bin、/etc/motd 都是 ASCII，没问题。
            // 
            let name = match name_buf.as_str() {
                Ok(name) => name,
                Err(_) => continue,
            };

            if name == "." || name == ".." {
                continue;
            }

            entries.push(DirEntry::new(name, dirent_type));
        }

        entries
    }
}


static EXT4_ROOT_LOCK: Mutex<()> = Mutex::new(());
static mut EXT4_ROOT: Option<Arc<Ext4Fs>> = None;

pub fn init(device: Arc<dyn BlockDevice>) {
    let _guard = EXT4_ROOT_LOCK.lock();

    let fs = Ext4Fs::load(device)
        .expect("[ext4] load rootfs failed");

    unsafe {
        EXT4_ROOT = Some(fs);
    }
}

pub fn is_available() -> bool {
    let _guard = EXT4_ROOT_LOCK.lock();

    unsafe {
        EXT4_ROOT.is_some()
    }
}

pub fn root_inode() -> Option<InodeRef> {
    let _guard = EXT4_ROOT_LOCK.lock();

    unsafe {
        EXT4_ROOT.as_ref().map(|fs| fs.root_inode())
    }
}