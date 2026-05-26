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

pub fn heap_test() {
    use alloc::boxed::Box;
    use alloc::vec::Vec;

    let b = Box::new(0x2333usize);
    log::info!("[heap] Box test: {:#x}", *b);

    let mut v = Vec::new();
    for i in 0..16 {
        v.push(i);
    }
    log::info!("[heap] Vec test: len={}, sum={}", v.len(), v.iter().sum::<usize>());
}


fn alloc_error_handler(layout: Layout) -> ! {
    panic!("allocation error: {:?}", layout);
}
