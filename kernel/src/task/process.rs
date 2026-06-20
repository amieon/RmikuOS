use alloc::string::String;
use alloc::vec::Vec;

use crate::fs::FileRef;
use crate::mm::{MemorySet, PhysPageNum};

use super::thread::Tid;

pub type Pid = usize;

pub const DEFAULT_TICKETS: usize = 100;
pub const BIG_STRIDE: usize = 10_000_000;

pub fn stride_from_tickets(tickets: usize) -> usize {
    let tickets = tickets.max(1);
    (BIG_STRIDE / tickets).max(1)
}

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

    pub run_ticks: usize,
    pub effective_tickets: usize,
    pub ready_thread_count_snapshot: usize,

    pub mmap_areas: Vec<MmapArea>,
    pub mmap_free_ranges: Vec<MmapFreeRange>,
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

            run_ticks: 0,
            effective_tickets: DEFAULT_TICKETS,
            ready_thread_count_snapshot: 0,
            
            mmap_areas: Vec::new(),
            mmap_free_ranges: Vec::new(),
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
        mmap_free_ranges: Vec<MmapFreeRange>,
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

            run_ticks: 0,
            effective_tickets: tickets,
            ready_thread_count_snapshot: 1,

            mmap_areas,
            mmap_free_ranges,
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

    pub fn close_non_standard_fds_on_exec(&mut self) -> Vec<FileRef> {
        let mut closed = Vec::new();
        for fd in 3..self.fd_table.len() {
            if let Some(file) = self.fd_table[fd].take() {
                self.free_fds.push(fd);
                closed.push(file);
            }
        }
        closed   // 把关掉的 file 返回出去
    }
}


#[derive(Clone, Copy, Debug)]
pub struct MmapArea {
    pub start: usize,
    pub end: usize,
    pub prot: usize,
}

#[derive(Clone, Copy, Debug)]
pub struct MmapFreeRange {
    pub start: usize,
    pub end: usize,
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

        // 优先复用 free range。先只做 first-fit。
        for i in 0..self.mmap_free_ranges.len() {
            let range = self.mmap_free_ranges[i];

            let start = crate::mm::align_up(
                range.start,
                crate::mm::config::PAGE_SIZE,
            );

            let end = start.checked_add(len)?;

            if end > range.end {
                continue;
            }

            //从这个 free range 里切出 [start, end)。
            if start == range.start && end == range.end {
                self.mmap_free_ranges.remove(i);
            } else if start == range.start {
                self.mmap_free_ranges[i].start = end;
            } else if end == range.end {
                self.mmap_free_ranges[i].end = start;
            } else {
                //中间切一段，拆成左右两个 free range。
                self.mmap_free_ranges[i].end = start;
                self.mmap_free_ranges.push(MmapFreeRange {
                    start: end,
                    end: range.end,
                });
            }

            return Some((start, end));
        }

        //free list 找不到，再从 mmap_next 扩张。
        let start = crate::mm::align_up(
            self.mmap_next,
            crate::mm::config::PAGE_SIZE,
        );

        let end = start.checked_add(len)?;

        if end > USER_MMAP_TOP {
            return None;
        }

        self.mmap_next = end;

        Some((start, end))
    }

    pub fn dealloc_mmap_range(&mut self, start: usize, end: usize) {
        if start >= end {
            return;
        }

        self.mmap_free_ranges.push(MmapFreeRange {
            start,
            end,
        });

        self.merge_mmap_free_ranges();
    }

    fn merge_mmap_free_ranges(&mut self) {
        //free range 数量一般不大，直接 O(n2) 合并，简单可靠。
        let mut changed = true;

        while changed {
            changed = false;

            'outer: for i in 0..self.mmap_free_ranges.len() {
                for j in (i + 1)..self.mmap_free_ranges.len() {
                    let a = self.mmap_free_ranges[i];
                    let b = self.mmap_free_ranges[j];

                    
                    //重叠或相邻都合并。
                    if a.end >= b.start && b.end >= a.start {
                        let start = if a.start < b.start { a.start } else { b.start };
                        let end = if a.end > b.end { a.end } else { b.end };

                        self.mmap_free_ranges[i] = MmapFreeRange {
                            start,
                            end,
                        };

                        self.mmap_free_ranges.remove(j);
                        changed = true;
                        break 'outer;
                    }
                }
            }
        }
    }
}
