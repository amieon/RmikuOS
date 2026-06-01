use core::alloc::Layout;
use core::sync::atomic::{AtomicUsize, Ordering};

use buddy_system_allocator::LockedHeap;

use super::config::KERNEL_HEAP_SIZE;

#[global_allocator]
static HEAP_ALLOCATOR: LockedHeap<32> = LockedHeap::<32>::new();

static KERNEL_HEAP_START: AtomicUsize = AtomicUsize::new(0);
static KERNEL_HEAP_END: AtomicUsize = AtomicUsize::new(0);

pub fn init(heap_start: usize) {
    let heap_end = heap_start + KERNEL_HEAP_SIZE;

    KERNEL_HEAP_START.store(heap_start, Ordering::Relaxed);
    KERNEL_HEAP_END.store(heap_end, Ordering::Relaxed);

    unsafe {
        HEAP_ALLOCATOR
            .lock()
            .init(heap_start, KERNEL_HEAP_SIZE);
    }
    crate::mm::heap::dump_heap_stats("after init");


    log::info!(
        "[mm] kernel heap: {:#x}..{:#x}, size={} KiB",
        heap_start,
        heap_end,
        KERNEL_HEAP_SIZE / 1024
    );
}

pub fn dump_heap_stats(tag: &str) {
    let heap = HEAP_ALLOCATOR.lock();

    log::error!(
        "[heap] {}: total={} KiB, user={} KiB, actual={} KiB",
        tag,
        heap.stats_total_bytes() / 1024,
        heap.stats_alloc_user() / 1024,
        heap.stats_alloc_actual() / 1024,
    );
}

pub fn kernel_heap_start() -> usize {
    KERNEL_HEAP_START.load(Ordering::Relaxed)
}

pub fn kernel_heap_end() -> usize {
    KERNEL_HEAP_END.load(Ordering::Relaxed)
}

pub fn kernel_heap_inited() -> bool {
    kernel_heap_start() != 0 && kernel_heap_end() != 0
}


fn alloc_error_handler(layout: Layout) -> ! {
    panic!("allocation error: {:?}", layout);
}