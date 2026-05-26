/// QEMU virt DRAM starts at 0x8000_0000.
pub const MEMORY_START: usize = 0x8000_0000;

/// run.sh uses `-m 128M` for RISC-V.
pub const MEMORY_SIZE: usize = 128 * 1024 * 1024;

pub const MEMORY_END: usize = MEMORY_START + MEMORY_SIZE;

/// UART0 on QEMU virt.
pub const UART0: usize = 0x1000_0000;

use core::arch::asm;

use super::PhysPageNum;

const SATP_MODE_SV39: usize = 8usize << 60;

pub fn activate_kernel_page_table(root_ppn: PhysPageNum) {
    let satp = SATP_MODE_SV39 | root_ppn.0;

    unsafe {
        asm!(
            "csrw satp, {satp}",
            "sfence.vma",
            satp = in(reg) satp,
            options(nostack)
        );
    }

    log::info!(
        "[mm] RISC-V paging enabled: satp={:#x}, root_ppn={:#x}",
        satp,
        root_ppn.0
    );
}