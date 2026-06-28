#![no_std]

extern crate alloc;

pub mod number;
pub mod syscall;
pub mod io;
pub mod process;
pub mod fs;
pub mod sched;
pub mod flag;
pub mod allocator; 
pub mod args;


pub use alloc::vec::Vec;
pub use alloc::string::String;
pub use alloc::boxed::Box;
pub use alloc::vec;    

pub use args::Args;

pub mod prelude {
    pub use crate::io::{read, write, puts, put_char, open, open_create, create, close, strlen};
    pub use crate::process::{exit, fork, waitpid, getpid, yield_now, sleep};
    pub use crate::fs::{stat, mkdir, unlink, rmdir, chdir, getcwd, Stat, DirEntry};
    pub use crate::sched::{get_ticks, set_my_tickets, set_sched_alpha, get_sched_alpha};
}
