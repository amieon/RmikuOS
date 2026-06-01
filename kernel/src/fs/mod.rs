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

pub fn open(path: &str) -> Option<FileRef> {
    let inode = path::lookup_path(path)?;
    inode.open()
}

pub fn lookup(path: &str) -> Option<InodeRef> {
    path::lookup_path(path)
}