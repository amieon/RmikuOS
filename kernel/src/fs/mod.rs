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