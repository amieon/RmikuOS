// kernel/src/mm/mod.rs
pub mod address;
pub mod config;
pub mod frame_allocator;
pub mod heap;
pub mod page_table;
pub mod elf;

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

pub mod memory_set;
pub use memory_set::*;
pub mod map_area;
pub use map_area::*;

pub mod user_layout;
pub use user_layout::*;

use crate::arch::{MEMORY_END, MEMORY_START};

use alloc::boxed::Box;
use self::page_table::{map_range_identity, PageTable, PteFlags};
use core::sync::atomic::{AtomicUsize, Ordering};

static KERNEL_ROOT_PPN: AtomicUsize = AtomicUsize::new(0);


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

    let kernel_start_pa = kernel_virt_to_phys(kernel_start_va);
    let kernel_end_pa = kernel_virt_to_phys(kernel_end_va);

    let heap_start_pa = align_up(kernel_end_pa, PAGE_SIZE);
    let heap_end_pa = heap_start_pa + KERNEL_HEAP_SIZE;

    let heap_start_va = kernel_phys_to_virt(heap_start_pa);

    heap::init(heap_start_va);

    let free_start = PhysAddr::from(heap_end_pa).ceil();
    let free_end = PhysAddr::from(MEMORY_END).floor();

    frame_allocator::init_frame_allocator(free_start, free_end);

    log::info!(
        "[mm] physical kernel: {:#x}..{:#x}",
        kernel_start_pa,
        kernel_end_pa
    );
    log::info!(
        "[mm] virtual  kernel: {:#x}..{:#x}",
        kernel_start_va,
        kernel_end_va
    );
    log::info!(
        "[mm] heap: pa={:#x}..{:#x}, va={:#x}",
        heap_start_pa,
        heap_end_pa,
        heap_start_va
    );
}


pub fn init_paging() {
    let kernel_space = MemorySet::new_kernel();
    let root = kernel_space.root_ppn();

    KERNEL_ROOT_PPN.store(root.0, Ordering::Release);

    let _kernel_space = Box::leak(Box::new(kernel_space));

    activate_page_table(root);

    log::info!("[mm] kernel MemorySet activated");
}

pub fn kernel_root_ppn() -> PhysPageNum {
    let ppn = KERNEL_ROOT_PPN.load(Ordering::Acquire);
    assert!(ppn != 0, "kernel root page table not initialized");
    PhysPageNum(ppn)
}

pub fn activate_kernel_page_table() {
    activate_page_table(kernel_root_ppn());
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


pub fn kernel_virt_to_phys(va: usize) -> usize {
    virt_to_phys(va)
}



pub fn kernel_phys_to_virt(pa: usize) -> usize {
    phys_to_virt(pa)
}


pub fn align_up(value: usize, align: usize) -> usize {
    assert!(align.is_power_of_two());
    (value + align - 1) & !(align - 1)
}

pub fn align_down(value: usize, align: usize) -> usize {
    assert!(align.is_power_of_two());
    value & !(align - 1)
}