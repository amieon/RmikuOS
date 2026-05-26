use core::arch::asm;

use crate::mm::PhysPageNum;

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