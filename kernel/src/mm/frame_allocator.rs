use crate::sync::SpinLock;
use super::config::{PAGE_SIZE, PAGE_SIZE_BITS};
use super::address::{PhysAddr, PhysPageNum};

pub trait FrameAllocator {
    fn alloc(&mut self) -> Option<PhysPageNum>;
    fn dealloc(&mut self, ppn: PhysPageNum);
}

pub struct StackFrameAllocator {
    current: usize,
    end: usize,
}

impl StackFrameAllocator {
    pub const fn new() -> Self {
        Self {
            current: 0,
            end: 0,
        }
    }

    pub fn init(&mut self, start_ppn: PhysPageNum, end_ppn: PhysPageNum) {
        self.current = start_ppn.0;
        self.end = end_ppn.0;
    }
}

impl FrameAllocator for StackFrameAllocator {
    fn alloc(&mut self) -> Option<PhysPageNum> {
        if self.current == self.end {
            None
        } else {
            let ppn = PhysPageNum(self.current);
            self.current += 1;
            Some(ppn)
        }
    }

    fn dealloc(&mut self, _ppn: PhysPageNum) {
        // 第一版先不实现回收
        // 后面可以加 recycled Vec/栈
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