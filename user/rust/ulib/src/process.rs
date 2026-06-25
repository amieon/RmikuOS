//! 进程控制:exit/fork/waitpid/getpid/yield/sleep/exec。
//! 对应 C 的 process.h。

use crate::number::*;
use crate::syscall::syscall3;

/// 退出当前进程,永不返回。
pub fn exit(code: usize) -> ! {
    unsafe { syscall3(SYS_EXIT, code, 0, 0); }
    loop {}
}

pub fn getpid() -> isize {
    unsafe { syscall3(SYS_GETPID, 0, 0, 0) }
}

/// fork:父进程得到子 pid(>0),子进程得到 0,失败 <0。
pub fn fork() -> isize {
    unsafe { syscall3(SYS_FORK, 0, 0, 0) }
}

/// 等待子进程退出。exit_code 传入一个可写的 i32 引用接收退出码。
pub fn waitpid(pid: isize, exit_code: &mut i32) -> isize {
    unsafe { syscall3(SYS_WAITPID, pid as usize, exit_code as *mut i32 as usize, 0) }
}

/// 不关心退出码的 waitpid。
pub fn waitpid_discard(pid: isize) -> isize {
    unsafe { syscall3(SYS_WAITPID, pid as usize, 0, 0) }
}

/// 让出 CPU(yield 是 Rust 关键字,故命名 yield_now)。
pub fn yield_now() -> isize {
    unsafe { syscall3(SYS_YIELD, 0, 0, 0) }
}

pub fn sleep(ticks: usize) -> isize {
    unsafe { syscall3(SYS_SLEEP, ticks, 0, 0) }
}

/// 简单 exec:只传路径(单参数,argv[0] = path),失败返回 <0,成功不返回。
/// 完整的带参 exec(exec_args 结构)待 process 层后续扩展。
pub fn exec(path: &[u8]) -> isize {
    unsafe { syscall3(SYS_EXEC, path.as_ptr() as usize, path.len(), 0) }
}
