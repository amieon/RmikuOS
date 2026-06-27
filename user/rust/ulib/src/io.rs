//! 对应 C 的 io.h。路径以 &[u8] 传入(裸字节,免去 C 的 NUL 结尾依赖);
//! 也提供基于 &str 的便捷封装。


use crate::number::*;
use crate::syscall::syscall3;
use crate::flag::*;

/// C 风格字符串长度(到 NUL 为止)。Rust 侧一般用 slice 的 len(),
/// 此函数仅在需要与裸指针互操作时使用。
pub unsafe fn strlen(mut p: *const u8) -> usize {
    let mut n = 0;
    while *p != 0 {
        n += 1;
        p = p.add(1);
    }
    n
}

pub fn write(fd: usize, buf: &[u8]) -> isize {
    unsafe { syscall3(SYS_WRITE, fd, buf.as_ptr() as usize, buf.len()) }
}

pub fn read(fd: usize, buf: &mut [u8]) -> isize {
    unsafe { syscall3(SYS_READ, fd, buf.as_mut_ptr() as usize, buf.len()) }
}

pub fn put_char(c: u8) {
    let b = [c];
    write(1, &b);
}

/// 打印字符串到 stdout(不追加换行)。
pub fn puts(s: &str) {
    write(1, s.as_bytes());
}

pub fn open(path: &[u8], flags:usize) -> isize {
    unsafe { syscall3(SYS_OPEN, path.as_ptr() as usize, path.len(), flags) }
}

pub fn create(path: &[u8]) -> isize {
    unsafe { syscall3(SYS_CREATE, path.as_ptr() as usize, path.len(), 0) }
}

pub fn close(fd: usize) -> isize {
    unsafe { syscall3(SYS_CLOSE, fd, 0, 0) }
}

/// 打开,不存在则创建后再打开(对应 C 的 open_create)。
pub fn open_create(path: &[u8], flags:usize) -> isize {
     unsafe { syscall3(SYS_OPEN, path.as_ptr() as usize, path.len(), flags|O_CREAT) }
}
