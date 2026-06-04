use alloc::string::String;
use alloc::vec::Vec;

use crate::fs::FileRef;
use crate::mm::{MemorySet, PhysPageNum};

use super::thread::Tid;

pub type Pid = usize;

pub const DEFAULT_TICKETS: usize = 100;
pub const BIG_STRIDE: usize = 10_000_000;

pub struct ProcessControlBlock {
    pub pid: Pid,

    pub parent: Option<Pid>,
    pub children: Vec<Pid>,

    pub user_space: MemorySet,

    pub fd_table: Vec<Option<FileRef>>,
    pub free_fds: Vec<usize>,
    pub cwd: String,

    pub threads: Vec<Tid>,
    pub ready_threads: Vec<Tid>,

    pub tickets: usize,
    pub stride: usize,
    pub pass: usize,

    pub exit_code: i32,
}

impl ProcessControlBlock {
    pub fn new(
        pid: Pid,
        user_space: MemorySet,
        cwd: String,
    ) -> Self {
        Self {
            pid,
            parent: None,
            children: Vec::new(),

            user_space,

            fd_table: Self::new_fd_table(),
            free_fds: Vec::new(),
            cwd,

            threads: Vec::new(),
            ready_threads: Vec::new(),

            tickets: DEFAULT_TICKETS,
            stride: BIG_STRIDE / DEFAULT_TICKETS,
            pass: 0,

            exit_code: 0,
        }
    }

    pub fn new_fd_table() -> Vec<Option<FileRef>> {
        let mut fd_table = Vec::new();

        fd_table.push(Some(crate::fs::stdin()));
        fd_table.push(Some(crate::fs::stdout()));
        fd_table.push(Some(crate::fs::stdout()));

        fd_table
    }

    pub fn root_ppn(&self) -> PhysPageNum {
        self.user_space.root_ppn()
    }

    pub fn close_non_standard_fds_on_exec(&mut self) {
        for fd in 3..self.fd_table.len() {
            if self.fd_table[fd].take().is_some() {
                self.free_fds.push(fd);
            }
        }
    }
}


