//kernel/src/mm/mode.rs

pub mod address;
pub mod config;
pub mod frame_allocator;
pub mod heap;
pub mod page_table;

#[cfg(target_arch = "riscv64")]
#[path = "riscv64.rs"]
mod arch_mm;

#[cfg(target_arch = "loongarch64")]
#[path = "loongarch64.rs"]
mod arch_mm;

pub use address::*;
pub use config::*;
pub use frame_allocator::{alloc_frame, dealloc_frame};
pub use page_table::*;
pub use arch_mm::*;


extern "C" {
    fn _kernel_start();
    fn _kernel_end();
    fn _stext();
    fn _etext();
    fn _srodata();
    fn _erodata();
    fn _sdata();
    fn _edata();
    fn _sbss();
    fn _ebss();
}

/// Initialize the physical frame allocator.
///
/// This does not enable paging yet.
pub fn init() {
    let kernel_start = _kernel_start as usize;
    let kernel_end = _kernel_end as usize;
    let heap_start = align_up(kernel_end, PAGE_SIZE);
    let heap_end = heap_start + KERNEL_HEAP_SIZE;

    heap::init(heap_start);

    let free_start = PhysAddr::from(heap_end).ceil();
    let free_end = PhysAddr::from(MEMORY_END).floor();


    log::info!("[mm] kernel: {:#x}..{:#x}", kernel_start, kernel_end);
    log::info!(
        "[mm] sections: text={:#x}..{:#x}, rodata={:#x}..{:#x}, data={:#x}..{:#x}, bss={:#x}..{:#x}",
        _stext as usize,
        _etext as usize,
        _srodata as usize,
        _erodata as usize,
        _sdata as usize,
        _edata as usize,
        _sbss as usize,
        _ebss as usize,
    );
    log::info!("[mm] free frames: {:?}..{:?}", free_start, free_end);

    frame_allocator::init_frame_allocator(free_start, free_end);
}



pub fn init_paging() {
    use alloc::boxed::Box;

    let mut pt = PageTable::new();

    // 第一阶段：恒等映射整个物理内存 + UART
    map_range_identity(
        &mut pt,
        MEMORY_START,
        MEMORY_END,
        PteFlags::R
            .union(PteFlags::W)
            .union(PteFlags::X)
            .union(PteFlags::A)
            .union(PteFlags::D),
    );

    map_range_identity(
        &mut pt,
        crate::arch::UART_BASE,
        crate::arch::UART_BASE + PAGE_SIZE,
        PteFlags::R
            .union(PteFlags::W)
            .union(PteFlags::A)
            .union(PteFlags::D),
    );

    let root_ppn = pt.root_ppn();

    let _pt: &'static mut PageTable = Box::leak(Box::new(pt));

    activate_kernel_page_table(root_ppn);

    log::info!("[mm] paging activated");
}