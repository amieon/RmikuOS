extern crate alloc;

pub mod dirent;
pub mod file;
pub mod inode;
pub mod path;
pub mod common_file;
pub mod ext4fs;
pub mod tmpfs;
pub mod pipe;
pub mod mount;
pub mod fatfs;
pub mod flag;

//pub mod initramfs;
mod stdio;

pub use file::{File, FileRef};
pub use inode::{Inode, InodeRef, Metadata, InodeType};
pub use stdio::{stdin, stdout};
pub use flag::*;
pub const EOF : isize = 0;
pub const EPIPE : isize = -1;


pub fn normalize_path(cwd: &str, path: &str) -> Option<alloc::string::String> {
    path::normalize_path(cwd, path)
}

pub fn lookup(path: &str) -> Option<InodeRef> {
    path::lookup_abs_path(path)
}

pub fn lookup_at(cwd: &str, path: &str) -> Option<InodeRef> {
    path::lookup_path_at(cwd, path)
}

pub fn open_at(cwd: &str, path: &str, flags: usize) -> Option<FileRef> {
    let inode = match path::lookup_path_at(cwd, path) {
        Some(i) => i,
        None => {
            // 文件不存在
            if flags & O_CREAT != 0 {
                // 算出绝对路径,复用 create_file
                let abs = path::normalize_path(cwd, path)?;
                create_file(&abs)?
            } else {
                return None;
            }
        }
    };

    if flags & O_TRUNC != 0 {
        inode.truncate();
    }
    inode.open(flags)
}

pub fn open(path: &str, flags: usize) -> Option<FileRef> {
    let inode = match path::lookup_abs_path(path) {
        Some(i) => i,
        None => {
            if flags & O_CREAT != 0 {
                create_file(path)?      // open 的 path 已是绝对路径
            } else {
                return None;
            }
        }
    };

    if flags & O_TRUNC != 0 {
        inode.truncate();
    }
    inode.open(flags)
}

pub mod stat;

pub use stat::{
    Stat,
    STAT_TYPE_FILE,
    STAT_TYPE_DIR,
    STAT_TYPE_CHAR,
};
pub use common_file::{
    ReadOnlyDirFile,
    ReadOnlyMemFile,
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




use alloc::{string::String, vec::Vec};

use crate::{fs::flag::O_RDONLY, io::uart::putchar_raw};

pub fn read_all(path: &str) -> Option<Vec<u8>> {
    let inode = crate::fs::path::lookup_abs_path(path)?;

    let meta = inode.metadata();

    if meta.inode_type != crate::fs::InodeType::File {
        return None;
    }

    let file = inode.open(O_RDONLY)?;

    let mut data = Vec::new();
    let mut buf = [0u8; 512];


    loop {

        let n = file.read(&mut buf);

        if n < 0 {
            return None;
        }

        if n == 0 {
            break;
        }


        data.extend_from_slice(&buf[..n as usize]);
    }


    Some(data)
}

// "/tmp/x" → ("/tmp", "x")
// "/tmp"   → ("/", "tmp")
fn split_parent(path: &str) -> (String, String) {
    let trimmed = path.trim_end_matches('/');
    match trimmed.rfind('/') {
        Some(0) => (String::from("/"), String::from(&trimmed[1..])),
        Some(pos) => (String::from(&trimmed[..pos]), String::from(&trimmed[pos+1..])),
        None => (String::from("/"), String::from(trimmed)),
    }
}

pub fn create_file(path: &str) -> Option<InodeRef> {
    let abs = normalize_path("/", path)?;
    let (parent, name) = split_parent(&abs);
    let parent_inode = path::lookup_abs_path(&parent)?;  
    parent_inode.create(&name)                    
}

pub fn make_dir(path: &str) -> Option<InodeRef> {
    let abs = normalize_path("/", path)?;
    let (parent, name) = split_parent(&abs);
    let parent_inode = path::lookup_abs_path(&parent)?;
    parent_inode.mkdir(&name)
}

pub fn unlink_file(path: &str) -> Option<isize>{
    let abs = normalize_path("/", path)?;
    let (parent, name) = split_parent(&abs);
    let parent_inode = path::lookup_abs_path(&parent)?;  
    Some(parent_inode.unlink(&name))                  
}

pub fn remove_dir(path: &str) -> Option<isize> {
    let abs = normalize_path("/", path)?;
    let (parent, name) = split_parent(&abs);
    let parent_inode = path::lookup_abs_path(&parent)?;
    Some(parent_inode.rmdir(&name))
}

pub fn remove_recursive(path: &str) -> Option<isize> {
    let abs = normalize_path("/", path)?;
    let (parent, name) = split_parent(&abs);
    let parent_inode = path::lookup_abs_path(&parent)?;
    Some(parent_inode.remove_recursive(&name))
}