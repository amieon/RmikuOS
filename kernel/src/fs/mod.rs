extern crate alloc;

pub mod dirent;
pub mod file;
mod initramfs;
mod stdio;

pub use file::{File, FileRef};
pub use stdio::{stdin, stdout};

pub fn open(path: &str) -> Option<FileRef> {
    initramfs::open(path)
}