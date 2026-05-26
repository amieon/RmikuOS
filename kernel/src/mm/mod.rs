//kernel/src/mm/mode.rs

mod address;
mod frame_allocator;


#[cfg(target_arch = "riscv64")]
#[path = "riscv64.rs"]
mod arch_mm;

#[cfg(target_arch = "loongarch64")]
#[path = "loongarch64.rs"]
mod arch_mm;

pub mod heap;
pub use address::*;
pub use frame_allocator::{alloc_frame, dealloc_frame};
pub use arch_mm::*;
pub mod config;
pub use config::*;
pub mod page_table;

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
