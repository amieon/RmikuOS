use core::arch::asm;

use crate::mm::PhysPageNum;

const SATP_MODE_SV39: usize = 8usize << 60;

pub fn activate_kernel_page_table_by_root(root_ppn: PhysPageNum) {
    activate_page_table(root_ppn);
    let satp = SATP_MODE_SV39 | root_ppn.0;
    log::info!(
        "[mm] RISC-V paging enabled: satp={:#x}, root_ppn={:#x}",
        satp,
        root_ppn.0
    );
}


pub fn activate_page_table(root_ppn: PhysPageNum) {
    let satp = SATP_MODE_SV39 | root_ppn.0;

    unsafe {
        asm!(
            "csrw satp, {0}",
            "sfence.vma",
            in(reg) satp,
            options(nostack)
        );
    }
}

