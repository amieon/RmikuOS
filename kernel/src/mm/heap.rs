use core::alloc::Layout;

use buddy_system_allocator::LockedHeap;

use super::config::KERNEL_HEAP_SIZE;


#[global_allocator]
static HEAP_ALLOCATOR: LockedHeap<32> = LockedHeap::<32>::new();

pub fn init(heap_start: usize) {
    unsafe {
        HEAP_ALLOCATOR
            .lock()
            .init(heap_start, KERNEL_HEAP_SIZE);
    }

    log::info!(
        "[mm] kernel heap: {:#x}..{:#x}, size={} KiB",
        heap_start,
        heap_start + KERNEL_HEAP_SIZE,
        KERNEL_HEAP_SIZE / 1024
    );
}


fn alloc_error_handler(layout: Layout) -> ! {
    panic!("allocation error: {:?}", layout);
}
