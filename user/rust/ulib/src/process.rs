//! 对应 C 的 process.h。

use crate::number::*;
use crate::syscall::syscall3;


pub fn exit(code: usize) -> ! {
    unsafe { syscall3(SYS_EXIT, code, 0, 0); }
    loop {}
}

pub fn getpid() -> isize {
    unsafe { syscall3(SYS_GETPID, 0, 0, 0) }
}


pub fn fork() -> isize {
    unsafe { syscall3(SYS_FORK, 0, 0, 0) }
}


pub fn waitpid(pid: isize, exit_code: &mut i32) -> isize {
    unsafe { syscall3(SYS_WAITPID, pid as usize, exit_code as *mut i32 as usize, 0) }
}


pub fn waitpid_discard(pid: isize) -> isize {
    unsafe { syscall3(SYS_WAITPID, pid as usize, 0, 0) }
}


pub fn yield_now() -> isize {
    unsafe { syscall3(SYS_YIELD, 0, 0, 0) }
}

pub fn sleep(ticks: usize) -> isize {
    unsafe { syscall3(SYS_SLEEP, ticks, 0, 0) }
}


pub fn exec(path: &[u8]) -> isize {
    unsafe { syscall3(SYS_EXEC, path.as_ptr() as usize, path.len(), 0) }
}
