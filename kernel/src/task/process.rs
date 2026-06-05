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

    pub mmap_areas: Vec<MmapArea>,
    pub mmap_next: usize,

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
            
            mmap_areas: Vec::new(),
            mmap_next: USER_MMAP_BASE,

            exit_code: 0,
        }
    }

    pub fn fork_from(
        pid: Pid,
        parent: Pid,
        user_space: MemorySet,
        fd_table: Vec<Option<FileRef>>,
        free_fds: Vec<usize>,
        cwd: String,
        parent_tickets: usize,
        parent_pass: usize,
        mmap_areas: Vec<MmapArea>,
        mmap_next: usize,
    ) -> Self {
        let tickets = parent_tickets.max(1);
        let stride = BIG_STRIDE / tickets;

        Self {
            pid,

            parent: Some(parent),
            children: Vec::new(),

            user_space,

            fd_table,
            free_fds,
            cwd,

            threads: Vec::new(),
            ready_threads: Vec::new(),

            tickets,
            stride,
            pass: parent_pass,

            mmap_areas,
            mmap_next,

            exit_code: 0,
        }
    }

    pub fn root_ppn(&self) -> PhysPageNum {
        self.user_space.root_ppn()
    }

    pub fn new_fd_table() -> Vec<Option<FileRef>> {
        let mut fd_table = Vec::new();

        /*
         * fd 0: stdin
         * fd 1: stdout
         * fd 2: stderr，暂时也接 stdout
         */
        fd_table.push(Some(crate::fs::stdin()));
        fd_table.push(Some(crate::fs::stdout()));
        fd_table.push(Some(crate::fs::stdout()));

        fd_table
    }

    pub fn close_non_standard_fds_on_exec(&mut self) {
        /*
         * fd 0/1/2 是 stdin/stdout/stderr，exec 后保留。
         * fd >= 3 作为普通打开文件，exec 成功后关闭。
         */
        for fd in 3..self.fd_table.len() {
            if self.fd_table[fd].take().is_some() {
                self.free_fds.push(fd);
            }
        }
    }
}


#[derive(Clone, Copy, Debug)]
pub struct MmapArea {
    pub start: usize,
    pub end: usize,
    pub prot: usize,
}

pub const USER_MMAP_BASE: usize = 0x4000_0000;
pub const USER_MMAP_TOP: usize = 0x7000_0000;

pub const PROT_READ: usize = 1;
pub const PROT_WRITE: usize = 2;
pub const PROT_EXEC: usize = 4;

impl ProcessControlBlock {
    pub fn alloc_mmap_range(&mut self, len: usize) -> Option<(usize, usize)> {
        let len = crate::mm::align_up(len, crate::mm::config::PAGE_SIZE);

        if len == 0 {
            return None;
        }

        let start = crate::mm::align_up(self.mmap_next, crate::mm::config::PAGE_SIZE);
        let end = start.checked_add(len)?;

        if end > USER_MMAP_TOP {
            return None;
        }

        self.mmap_next = end;

        Some((start, end))
    }

}
