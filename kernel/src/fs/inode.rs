use alloc::sync::Arc;
use alloc::vec::Vec;

use super::dirent::DirEntry;
use super::file::FileRef;

pub type InodeRef = Arc<dyn Inode>;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum InodeType {
    File,
    Directory,
}

#[derive(Clone, Copy, Debug)]
pub struct Metadata {
    pub inode_type: InodeType,
    pub size: usize,
}

pub trait Inode: Send + Sync {
    fn metadata(&self) -> Metadata;

    fn lookup(&self, _name: &str) -> Option<InodeRef> {
        None
    }

    fn open(&self) -> Option<FileRef>;

    fn getdents(&self) -> Vec<DirEntry> {
        Vec::new()
    }

    fn is_dir(&self) -> bool {
        self.metadata().inode_type == InodeType::Directory
    }

    fn is_file(&self) -> bool {
        self.metadata().inode_type == InodeType::File
    }
}