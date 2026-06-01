extern crate alloc;

pub mod dirent;
pub mod file;
pub mod inode;
pub mod path;

pub mod initramfs;
mod stdio;

pub use file::{File, FileRef};
pub use inode::{Inode, InodeRef, Metadata, InodeType};
pub use stdio::{stdin, stdout};

pub fn normalize_path(cwd: &str, path: &str) -> Option<alloc::string::String> {
    path::normalize_path(cwd, path)
}

pub fn lookup(path: &str) -> Option<InodeRef> {
    path::lookup_abs_path(path)
}

pub fn lookup_at(cwd: &str, path: &str) -> Option<InodeRef> {
    path::lookup_path_at(cwd, path)
}

pub fn open(path: &str) -> Option<FileRef> {
    let inode = path::lookup_abs_path(path)?;
    inode.open()
}

pub fn open_at(cwd: &str, path: &str) -> Option<FileRef> {
    let inode = path::lookup_path_at(cwd, path)?;
    inode.open()
}

pub mod stat;

pub use stat::{
    Stat,
    STAT_TYPE_FILE,
    STAT_TYPE_DIR,
    STAT_TYPE_CHAR,
};


pub fn stat_at(cwd: &str, path: &str) -> Option<Stat> {
    let inode = path::lookup_path_at(cwd, path)?;
    let meta = inode.metadata();

    let file_type = match meta.inode_type {
        InodeType::File => STAT_TYPE_FILE,
        InodeType::Directory => STAT_TYPE_DIR,
    };

    Some(Stat::new(file_type, meta.size))
}

pub fn stat(path: &str) -> Option<Stat> {
    let inode = path::lookup_abs_path(path)?;
    let meta = inode.metadata();

    let file_type = match meta.inode_type {
        InodeType::File => STAT_TYPE_FILE,
        InodeType::Directory => STAT_TYPE_DIR,
    };

    Some(Stat::new(file_type, meta.size))
}