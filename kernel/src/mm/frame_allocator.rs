use alloc::vec::Vec;

use crate::sync::sync::SpinLock;
use super::address::{PhysPageNum,PhysAddr};

pub trait FrameAllocator {
    fn alloc(&mut self) -> Option<PhysPageNum>;
    fn dealloc(&mut self, ppn: PhysPageNum);
}

pub struct StackFrameAllocator {
    start: usize,
    current: usize,
    end: usize,
    recycled: Vec<usize>,
}

impl StackFrameAllocator {
    pub const fn new() -> Self {
        Self {
            start: 0,
            current: 0,
            end: 0,
            recycled: Vec::new(),
        }
    }

    pub fn init(&mut self, start_ppn: PhysPageNum, end_ppn: PhysPageNum) {
        self.start = start_ppn.0;
        self.current = start_ppn.0;
        self.end = end_ppn.0;
        self.recycled.clear();
    }

    pub fn alloc_contiguous(&mut self, pages: usize) -> Option<PhysPageNum> {
        if pages == 0 {
            return None;
        }

        // 优先从 current 后面直接切连续页。
        if self.current + pages <= self.end {
            let base = self.current;
            self.current += pages;
            return Some(PhysPageNum(base));
        }

        // current 不够时，再从 recycled 里找连续页。
        if self.recycled.len() >= pages {
            self.recycled.sort_unstable();

            let mut run_start = 0usize;
            let mut run_len = 1usize;

            for i in 1..self.recycled.len() {
                if self.recycled[i] == self.recycled[i - 1] + 1 {
                    run_len += 1;
                } else {
                    run_start = i;
                    run_len = 1;
                }

                if run_len >= pages {
                    let pos = run_start;
                    let base = self.recycled[pos];

                    for _ in 0..pages {
                        self.recycled.remove(pos);
                    }

                    return Some(PhysPageNum(base));
                }
            }
        }

        None
    }

    pub fn dealloc_contiguous(&mut self, base_ppn: PhysPageNum, pages: usize) {
        let base = base_ppn.0;

        for i in 0..pages {
            let ppn = base + i;

            if ppn < self.start || ppn >= self.current {
                panic!(
                    "[mm] invalid contiguous frame dealloc: ppn={:#x}, valid={:#x}..{:#x}",
                    ppn,
                    self.start,
                    self.current,
                );
            }

            if self.recycled.iter().any(|&x| x == ppn) {
                panic!("[mm] contiguous frame double free: ppn={:#x}", ppn);
            }

            self.recycled.push(ppn);
        }
    }

}

impl FrameAllocator for StackFrameAllocator {
    fn alloc(&mut self) -> Option<PhysPageNum> {
        if let Some(ppn) = self.recycled.pop() {
            Some(PhysPageNum(ppn))
        } else if self.current < self.end {
            let ppn = PhysPageNum(self.current);
            self.current += 1;
            Some(ppn)
        } else {
            None
        }
    }

    fn dealloc(&mut self, ppn: PhysPageNum) {
        let ppn = ppn.0;

        if ppn < self.start || ppn >= self.current {
            panic!(
                "[mm] invalid frame dealloc: ppn={:#x}, valid={:#x}..{:#x}",
                ppn,
                self.start,
                self.current
            );
        }

        if self.recycled.iter().any(|&x| x == ppn) {
            panic!("[mm] frame double free: ppn={:#x}", ppn);
        }

        self.recycled.push(ppn);
    }

    
}

static FRAME_ALLOCATOR_LOCK: SpinLock = SpinLock::new();
static mut FRAME_ALLOCATOR: StackFrameAllocator = StackFrameAllocator::new();


pub fn init_frame_allocator(start_ppn: PhysPageNum, end_ppn: PhysPageNum) {
    let mut actual_start_ppn = start_ppn;


    if crate::mm::heap::kernel_heap_inited() {
        let heap_end_va = crate::mm::heap::kernel_heap_end();
        let heap_end_pa = crate::mm::kernel_virt_to_phys(heap_end_va);
        let heap_end_ppn = PhysAddr(heap_end_pa).ceil();

        if heap_end_ppn.0 > actual_start_ppn.0 {
            log::warn!(
                "[mm] frame allocator start adjusted to avoid kernel heap: old=PPN:{:#x}, heap_end_va={:#x}, heap_end_pa={:#x}, new=PPN:{:#x}",
                actual_start_ppn.0,
                heap_end_va,
                heap_end_pa,
                heap_end_ppn.0,
            );

            actual_start_ppn = heap_end_ppn;
        }
    } else {
        log::warn!(
            "[mm] kernel heap not initialized before frame allocator; cannot protect heap range"
        );
    }

    assert!(
        actual_start_ppn.0 <= end_ppn.0,
        "[mm] invalid frame allocator range: start=PPN:{:#x}, end=PPN:{:#x}",
        actual_start_ppn.0,
        end_ppn.0,
    );

    FRAME_ALLOCATOR_LOCK.lock();

    unsafe {
        FRAME_ALLOCATOR.init(actual_start_ppn, end_ppn);
    }

    FRAME_ALLOCATOR_LOCK.unlock();

    log::info!(
        "[mm] frame allocator: PPN {:#x}..{:#x}",
        actual_start_ppn.0,
        end_ppn.0
    );
}

pub fn alloc_frame() -> Option<PhysPageNum> {
    FRAME_ALLOCATOR_LOCK.lock();

    let frame = unsafe { FRAME_ALLOCATOR.alloc() };

    FRAME_ALLOCATOR_LOCK.unlock();

    frame
}

pub fn dealloc_frame(ppn: PhysPageNum) {
    FRAME_ALLOCATOR_LOCK.lock();

    unsafe {
        FRAME_ALLOCATOR.dealloc(ppn);
    }

    FRAME_ALLOCATOR_LOCK.unlock();
}


pub fn alloc_contiguous_frames(pages: usize) -> Option<PhysPageNum> {
    FRAME_ALLOCATOR_LOCK.lock();

    let frame = unsafe {
        FRAME_ALLOCATOR.alloc_contiguous(pages)
    };

    FRAME_ALLOCATOR_LOCK.unlock();

    frame
}

pub fn dealloc_contiguous_frames(base_ppn: PhysPageNum, pages: usize) {
    FRAME_ALLOCATOR_LOCK.lock();

    unsafe {
        FRAME_ALLOCATOR.dealloc_contiguous(base_ppn, pages);
    }

    FRAME_ALLOCATOR_LOCK.unlock();
}