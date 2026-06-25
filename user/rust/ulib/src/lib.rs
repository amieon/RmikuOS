//! ulib —— RmikuOS 用户态公共库(Rust 版,对应 C 的 user.h 体系)。
//!
//! no_std 裸机库:不依赖标准库,只用 core。通过 inline asm 触发与 C 用户程序
//! 完全相同的 syscall ABI(号在 a7/r11、参数 a0.. / r4..、ecall / syscall 0),
//! 因此 Rust 与 C 用户程序在内核看来完全等价。
//!
//! 模块层次(对应 C 头文件):
//!   number   —— 系统调用号常量          (syscall.h 的 SYS_*)
//!   syscall  —— 原始 syscall3 / syscall6 (架构分离)
//!   io       —— read/write/open/close/create/puts
//!   process  —— exit/fork/waitpid/getpid/yield/exec
//!   fs       —— stat/getdents/chdir/getcwd/mkdir/unlink/rmdir
//!   sched    —— tickets/alpha/sched_proc_stat/get_ticks
//!
//! 本版为「核心」:malloc / thread / uprintf 暂未移植(C 侧已足够写测试程序,
//! Rust 侧待核心稳定后再扩)。

#![no_std]

pub mod number;
pub mod syscall;
pub mod io;
pub mod process;
pub mod fs;
pub mod sched;

// 便捷再导出:用户程序可以直接 `use ulib::prelude::*;`
pub mod prelude {
    pub use crate::io::{read, write, puts, put_char, open, open_create, create, close, strlen};
    pub use crate::process::{exit, fork, waitpid, getpid, yield_now, sleep};
    pub use crate::fs::{stat, mkdir, unlink, rmdir, chdir, getcwd, Stat, DirEntry};
    pub use crate::sched::{get_ticks, set_my_tickets, set_sched_alpha, get_sched_alpha};
}
