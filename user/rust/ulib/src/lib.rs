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
