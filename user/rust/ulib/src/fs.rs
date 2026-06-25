//! 对应 C 的 fs.h。结构体用 #[repr(C)] 保证与内核的内存布局一致。

use crate::number::*;
use crate::syscall::syscall3;

pub const STAT_TYPE_FILE: u8 = 1;
pub const STAT_TYPE_DIR: u8 = 2;
pub const STAT_TYPE_CHAR: u8 = 3;

pub const FILE_TYPE_FILE: u8 = 1;
pub const FILE_TYPE_DIR: u8 = 2;


#[repr(C)]
pub struct Stat {
    pub file_type: u8,
    pub reserved: [u8; 7],
    pub size: usize,
}

impl Stat {
    pub const fn new() -> Self {
        Stat { file_type: 0, reserved: [0; 7], size: 0 }
    }
}


#[repr(C)]
pub struct DirEntry {
    pub file_type: u8,
    pub name_len: u8,
    pub reserved: [u8; 6],
    pub name: [u8; 56],
}

impl DirEntry {
    pub const fn new() -> Self {
        DirEntry { file_type: 0, name_len: 0, reserved: [0; 6], name: [0; 56] }
    }

    /// 取出名字的有效字节切片。
    pub fn name_bytes(&self) -> &[u8] {
        let n = self.name_len as usize;
        let n = if n > self.name.len() { self.name.len() } else { n };
        &self.name[..n]
    }
}

pub fn stat(path: &[u8], st: &mut Stat) -> isize {
    unsafe {
        syscall3(SYS_STAT, path.as_ptr() as usize, path.len(), st as *mut Stat as usize)
    }
}

pub fn fstat(fd: usize, st: &mut Stat) -> isize {
    unsafe { syscall3(SYS_FSTAT, fd, st as *mut Stat as usize, 0) }
}

/// 读取目录项到一个 DirEntry 数组缓冲。返回值是写入的字节数(/ size_of::<DirEntry> 得条数)。
pub fn getdents(fd: usize, buf: &mut [DirEntry]) -> isize {
    let byte_len = buf.len() * core::mem::size_of::<DirEntry>();
    unsafe { syscall3(SYS_GETDENTS, fd, buf.as_mut_ptr() as usize, byte_len) }
}

pub fn chdir(path: &[u8]) -> isize {
    unsafe { syscall3(SYS_CHDIR, path.as_ptr() as usize, path.len(), 0) }
}

pub fn getcwd(buf: &mut [u8]) -> isize {
    unsafe { syscall3(SYS_GETCWD, buf.as_mut_ptr() as usize, buf.len(), 0) }
}

pub fn mkdir(path: &[u8]) -> isize {
    unsafe { syscall3(SYS_MKDIR, path.as_ptr() as usize, path.len(), 0) }
}

pub fn unlink(path: &[u8]) -> isize {
    unsafe { syscall3(SYS_UNLINK, path.as_ptr() as usize, path.len(), 0) }
}

pub fn rmdir(path: &[u8]) -> isize {
    unsafe { syscall3(SYS_RMDIR, path.as_ptr() as usize, path.len(), 0) }
}

pub fn remove_recursive(path: &[u8]) -> isize {
    unsafe { syscall3(SYS_REMOVE_RECURSIVE, path.as_ptr() as usize, path.len(), 0) }
}
