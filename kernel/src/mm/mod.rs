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
    static _kernel_start: u8;
    static _kernel_end: u8;
    static _stext: u8;
    static _etext: u8;
    static _srodata: u8;
    static _erodata: u8;
    static _sdata: u8;
    static _edata: u8;
    static _sbss: u8;
    static _ebss: u8;
}

pub fn init() {
    let kernel_start_va = unsafe { core::ptr::addr_of!(_kernel_start) as usize };
    let kernel_end_va = unsafe { core::ptr::addr_of!(_kernel_end) as usize };

    let kernel_start_pa = virt_to_phys(kernel_start_va);
    let kernel_end_pa = virt_to_phys(kernel_end_va);

    let heap_start_pa = align_up(kernel_end_pa, PAGE_SIZE);
    let heap_end_pa = heap_start_pa + KERNEL_HEAP_SIZE;

    let heap_start_va = phys_to_virt(heap_start_pa);

    heap::init(heap_start_va);

    let free_start = PhysAddr::from(heap_end_pa).ceil();
    let free_end = PhysAddr::from(MEMORY_END).floor();

    frame_allocator::init_frame_allocator(free_start, free_end);


    log::info!("[mm] physical kernel: {:#x}..{:#x}", kernel_start_pa, kernel_end_pa);
    log::info!("[mm] virtual  kernel: {:#x}..{:#x}", kernel_start_va, kernel_end_va);
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
pub fn init_paging() {
    use alloc::boxed::Box;
    use crate::mm::page_table::{map_range, PageTable, PteFlags};

    let mut pt = PageTable::new();

    let flags = kernel_pte_flags();
    map_range(
        &mut pt,
        phys_to_virt(MEMORY_START),
        MEMORY_START,
        MEMORY_END - MEMORY_START,
        flags,
    );

    map_range(
        &mut pt,
        crate::arch::UART_BASE,
        crate::arch::UART_PADDR,
        PAGE_SIZE,
        flags,
    );

    let root = pt.root_ppn();

    let _pt = Box::leak(Box::new(pt));

    activate_kernel_page_table(root);

    log::info!("[mm] paging activated");
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


use config::{KERNEL_OFFSET, PAGE_SIZE};

pub fn phys_to_virt(pa: usize) -> usize {
    pa + KERNEL_OFFSET
}

pub fn virt_to_phys(va: usize) -> usize {
    va - KERNEL_OFFSET
}

pub fn align_up(value: usize, align: usize) -> usize {
    assert!(align.is_power_of_two());
    (value + align - 1) & !(align - 1)
}

pub fn align_down(value: usize, align: usize) -> usize {
    assert!(align.is_power_of_two());
    value & !(align - 1)
}