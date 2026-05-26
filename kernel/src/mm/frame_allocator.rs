use alloc::vec::Vec;

use crate::sync::SpinLock;
use super::address::PhysPageNum;

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
    FRAME_ALLOCATOR_LOCK.lock();

    unsafe {
        FRAME_ALLOCATOR.init(start_ppn, end_ppn);
    }

    FRAME_ALLOCATOR_LOCK.unlock();

    log::info!(
        "[mm] frame allocator: PPN {:#x}..{:#x}",
        start_ppn.0,
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