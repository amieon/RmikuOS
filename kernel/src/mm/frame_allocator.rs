use alloc::vec::Vec;

use crate::sync::sync::SpinLock;
use super::address::{PhysPageNum, PhysAddr};

pub trait FrameAllocator {
    fn alloc(&mut self) -> Option<PhysPageNum>;
    fn dealloc(&mut self, ppn: PhysPageNum);
}

/// order 最大为 10，即最大连续块 2^10 = 1024 页（4KiB 页时是 4MiB）
pub const MAX_ORDER: usize = 11;

/// 把 pages 向上取整到 2^order
const fn order_of(pages: usize) -> usize {
    let mut order = 0;
    let mut size = 1;
    while size < pages {
        size <<= 1;
        order += 1;
    }
    order
}

pub struct BuddyAllocator {
    start: usize,// 管理范围 [start, end)，单位 ppn
    end: usize,
    free_lists: [Vec<usize>; MAX_ORDER],
}

impl BuddyAllocator {
    pub const fn new() -> Self {
        Self {
            start: 0,
            end: 0,
            free_lists: [
                Vec::new(), Vec::new(), Vec::new(), Vec::new(),
                Vec::new(), Vec::new(), Vec::new(), Vec::new(),
                Vec::new(), Vec::new(), Vec::new(),
            ],
        }
    }

    pub fn init(&mut self, start_ppn: PhysPageNum, end_ppn: PhysPageNum) {
        self.start = start_ppn.0;
        self.end = end_ppn.0;
        for list in self.free_lists.iter_mut() {
            list.clear();
        }
        self.add_range(self.start, self.end);
    }

    pub fn add_free_range(&mut self, start_ppn: PhysPageNum, end_ppn: PhysPageNum) {
        self.add_range(start_ppn.0, end_ppn.0);
    }

    /// 把 [start, end) 切成一组对齐的 2^k 块挂进 free list
    fn add_range(&mut self, start: usize, end: usize) {
        let mut ppn = start;
        while ppn < end {
            let mut order = 0;
            // 在「不越界 + 保持对齐」的前提下尽量放大块
            while order + 1 < MAX_ORDER
                && ppn % (1 << (order + 1)) == 0
                && ppn + (1 << (order + 1)) <= end
            {
                order += 1;
            }
            self.free_lists[order].push(ppn);
            ppn += 1 << order;
        }
    }

    pub fn alloc_contiguous(&mut self, pages: usize) -> Option<PhysPageNum> {
        if pages == 0 {
            return None;
        }
        let order = order_of(pages);
        if order >= MAX_ORDER {
            log::warn!("[mm] contiguous alloc too large: {} pages", pages);
            return None;
        }

        // 从恰好够大的阶开始往上找第一个非空闲表
        for k in order..MAX_ORDER {
            if let Some(base) = self.free_lists[k].pop() {
                // 逐级分裂，后半块挂回低阶链表
                for j in (order..k).rev() {
                    self.free_lists[j].push(base + (1 << j));
                }
                return Some(PhysPageNum(base));
            }
        }
        None
    }

    pub fn dealloc_contiguous(&mut self, base_ppn: PhysPageNum, pages: usize) {
        if pages == 0 {
            return;
        }
        let mut order = order_of(pages);
        let mut base = base_ppn.0;

        assert!(
            base >= self.start && base + (1 << order) <= self.end,
            "[mm] invalid frame dealloc: ppn={:#x}, order={}, valid={:#x}..{:#x}",
            base, order, self.start, self.end,
        );

        // debug 下查 double-free：释放区间不能和任何空闲块重叠
        #[cfg(debug_assertions)]
        {
            let (lo, hi) = (base, base + (1 << order));
            for k in 0..MAX_ORDER {
                for &b in &self.free_lists[k] {
                    let (blo, bhi) = (b, b + (1 << k));
                    assert!(
                        hi <= blo || bhi <= lo,
                        "[mm] double free: {:#x}..{:#x} overlaps free block {:#x}..{:#x}",
                        lo, hi, blo, bhi,
                    );
                }
            }
        }

        // 伙伴空闲就合并，一路向上
        while order + 1 < MAX_ORDER {
            let buddy = base ^ (1 << order);
            if let Some(pos) = self.free_lists[order].iter().position(|&b| b == buddy) {
                self.free_lists[order].swap_remove(pos);
                base = base.min(buddy);
                order += 1;
            } else {
                break;
            }
        }
        self.free_lists[order].push(base);
    }
}

impl FrameAllocator for BuddyAllocator {
    fn alloc(&mut self) -> Option<PhysPageNum> {
        self.alloc_contiguous(1)
    }

    fn dealloc(&mut self, ppn: PhysPageNum) {
        self.dealloc_contiguous(ppn, 1)
    }
}

static FRAME_ALLOCATOR_LOCK: SpinLock = SpinLock::new();
static mut FRAME_ALLOCATOR: BuddyAllocator = BuddyAllocator::new();


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

pub fn add_free_frames(start_ppn: PhysPageNum, end_ppn: PhysPageNum) {
    FRAME_ALLOCATOR_LOCK.lock();
    unsafe { FRAME_ALLOCATOR.add_free_range(start_ppn, end_ppn); }
    FRAME_ALLOCATOR_LOCK.unlock();
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