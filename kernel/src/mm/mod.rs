// kernel/src/mm/mod.rs
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

use alloc::boxed::Box;
use self::page_table::{activate_kernel_page_table ,map_range_identity, PageTable, PteFlags};

unsafe extern "C" {
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

pub fn init() {
    let kernel_start = _kernel_start as usize;
    let kernel_end = _kernel_end as usize;

    let heap_start = align_up(kernel_end, PAGE_SIZE);
    let heap_end = heap_start + KERNEL_HEAP_SIZE;

    heap::init(heap_start);

    let free_start = PhysAddr::from(heap_end).ceil();
    let free_end = PhysAddr::from(MEMORY_END).floor();

    frame_allocator::init_frame_allocator(free_start, free_end);

    log::info!("[mm] kernel: {:#x}..{:#x}", kernel_start, kernel_end);
}

#[cfg(target_arch = "loongarch64")]
pub fn init_paging() {
    use alloc::boxed::Box;
    use self::page_table::{
        map_range_identity,
        PageTable,
        kernel_rwx_flags,
        mmio_rw_flags,
    };

    let mut pt = PageTable::new();

    let uart_start = align_down(crate::arch::UART_BASE, PAGE_SIZE);
    let uart_end = uart_start + PAGE_SIZE;

    map_range_identity_exclude(
        &mut pt,
        MEMORY_START,
        MEMORY_END,
        uart_start,
        uart_end,
        kernel_rwx_flags(),
    );

    map_range_identity(
        &mut pt,
        uart_start,
        uart_end,
        mmio_rw_flags(),
    );

    let root = pt.root_ppn();
    let _pt = Box::leak(Box::new(pt));

    activate_kernel_page_table(root);

    log::info!("[mm] LoongArch paging activated");
}

#[cfg(target_arch = "riscv64")]
fn kernel_pte_flags() -> PteFlags {
    PteFlags::R
        .union(PteFlags::W)
        .union(PteFlags::X)
        .union(PteFlags::A)
        .union(PteFlags::D)
}

#[cfg(target_arch = "loongarch64")]
fn kernel_pte_flags() -> PteFlags {
    PteFlags::D
        .union(PteFlags::MAT_CC)
        .union(PteFlags::G)
}