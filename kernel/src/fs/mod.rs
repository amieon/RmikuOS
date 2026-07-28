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
use crate::fs::inode::{check_access, R_OK, W_OK, X_OK};
pub use stdio::{stdin, stdout};
pub use flag::*;
pub const EOF : isize = 0;
pub const EPIPE : isize = -1;

pub const F_GETFL: usize = 3;
pub const F_SETFL: usize = 4;
pub const O_NONBLOCK: usize = 2048;



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

    // 访问检查(现有文件): 目录/普通文件按 R/W 请求检查; O_EXEC 由 exec 自管
    if !check_open(&inode, flags) {
        return None;
    }

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

    if !check_open(&inode, flags) {
        return None;
    }

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

    Some(Stat::new(file_type, meta.size, meta.mode, meta.uid as u32, meta.gid as u32))
}

pub fn stat(path: &str) -> Option<Stat> {
    let inode = path::lookup_abs_path(path)?;
    let meta = inode.metadata();

    let file_type = match meta.inode_type {
        InodeType::File => STAT_TYPE_FILE,
        InodeType::Directory => STAT_TYPE_DIR,
    };

    Some(Stat::new(file_type, meta.size, meta.mode, meta.uid as u32, meta.gid as u32))
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
    // 在目录中创建文件需要父目录的 写+搜索 权限(root 绕过)
    if !check_dir_write(&parent_inode) {
        return None;
    }
    parent_inode.create(&name)                    
}

pub fn make_dir(path: &str) -> Option<InodeRef> {
    let abs = normalize_path("/", path)?;
    let (parent, name) = split_parent(&abs);
    let parent_inode = path::lookup_abs_path(&parent)?;
    if !check_dir_write(&parent_inode) {
        return None;
    }
    parent_inode.mkdir(&name)
}

pub fn unlink_file(path: &str) -> Option<isize>{
    let abs = normalize_path("/", path)?;
    let (parent, name) = split_parent(&abs);
    let parent_inode = path::lookup_abs_path(&parent)?;
    // 删除需要父目录的 写+搜索 权限
    if !check_dir_write(&parent_inode) {
        return None;
    }
    Some(parent_inode.unlink(&name))                  
}

pub fn remove_dir(path: &str) -> Option<isize> {
    let abs = normalize_path("/", path)?;
    let (parent, name) = split_parent(&abs);
    let parent_inode = path::lookup_abs_path(&parent)?;
    if !check_dir_write(&parent_inode) {
        return None;
    }
    Some(parent_inode.rmdir(&name))
}

pub fn remove_recursive(path: &str) -> Option<isize> {
    let abs = normalize_path("/", path)?;
    let (parent, name) = split_parent(&abs);
    let parent_inode = path::lookup_abs_path(&parent)?;
    if !check_dir_write(&parent_inode) {
        return None;
    }
    Some(parent_inode.remove_recursive(&name))
}

/// 打开现有文件时的访问检查: 按 flags 计算 R/W 请求, 与文件 mode + 当前凭证比对。
/// O_EXEC 由调用方(exec)自行检查 X 权限, 这里放行。
fn check_open(inode: &InodeRef, flags: usize) -> bool {
    if flags & O_EXEC != 0 {
        return true;
    }
    let meta = inode.metadata();
    let (_u, euid, _g, egid) = crate::task::current_creds();
    let request = if (flags & O_ACCMODE) == O_WRONLY || (flags & O_ACCMODE) == O_RDWR {
        W_OK
    } else {
        R_OK
    };
    check_access(meta.mode, meta.uid, meta.gid, euid, egid, request)
}

/// 在目录中创建/删除/改名的必要条件: 父目录需 写+搜索 权限(root 绕过)。
fn check_dir_write(inode: &InodeRef) -> bool {
    let meta = inode.metadata();
    let (_u, euid, _g, egid) = crate::task::current_creds();
    check_access(meta.mode, meta.uid, meta.gid, euid, egid, W_OK | X_OK)
}

/// chmod(path, mode): 修改文件权限位。特权检查由系统调用层(sys_chmod)完成。
pub fn chmod_path(path: &str, mode: u16) -> isize {
    let abs = match normalize_path("/", path) {
        Some(p) => p,
        None => return -1,
    };
    match path::lookup_abs_path(&abs) {
        Some(inode) => inode.chmod(mode),
        None => -1,
    }
}

/// chown(path, uid, gid): 修改文件属主。usize::MAX 表示不修改该项。特权检查由系统调用层完成。
pub fn chown_path(path: &str, uid: usize, gid: usize) -> isize {
    let abs = match normalize_path("/", path) {
        Some(p) => p,
        None => return -1,
    };
    match path::lookup_abs_path(&abs) {
        Some(inode) => inode.chown(uid, gid),
        None => -1,
    }
}