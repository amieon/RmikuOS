/// The kernel is loaded at 0x0100_0000 by the QEMU loader in run.sh.
pub const MEMORY_START: usize = 0x0100_0000;

/// run.sh uses `-m 2G` for LoongArch.
pub const MEMORY_SIZE: usize = 2 * 1024 * 1024 * 1024;

pub const MEMORY_END: usize = MEMORY_START + MEMORY_SIZE;

/// Early UART/MMIO address should still come from arch::UART_BASE.
pub const UART0: usize = crate::arch::UART_BASE;

use core::arch::asm;

use super::PhysPageNum;

const CSR_CRMD: usize = 0x0;
const CSR_DMW0: usize = 0x180;
const CSR_DMW1: usize = 0x181;

const CRMD_DA: usize = 1 << 3;
const CRMD_PG: usize = 1 << 4;

/// DMW flags
const DMW_PLV0: usize = 1 << 0;

/// MAT:
/// 0 = strongly ordered / uncached-like
/// 1 = coherent cached, depending on platform
const DMW_MAT_CC: usize = 1 << 4;
const DMW_MAT_SUC: usize = 0 << 4;

/// VSEG selects VA[63:60].
const fn dmw(vseg: usize, mat: usize, plv: usize) -> usize {
    (vseg << 60) | mat | plv
}

/// LoongArch first-stage paging activation.
///
/// This does not install a full page table yet.
/// It enters mapped mode but keeps the kernel reachable through DMW.
pub fn activate_kernel_page_table(_root_ppn: PhysPageNum) {
    unsafe {
        // DMW0: low address identity window, VA segment 0x0 -> PA segment 0x0.
        // Covers current low-half kernel/UART addresses used by RmikuOS.
        let dmw0 = dmw(0x0, DMW_MAT_CC, DMW_PLV0);

        // Optional high direct map window, useful later if you move kernel/direct-map
        // into 0x9000_0000_0000_0000-like region.
        let dmw1 = dmw(0x9, DMW_MAT_CC, DMW_PLV0);

        asm!(
            "csrwr {dmw0}, 0x180",
            "csrwr {dmw1}, 0x181",
            dmw0 = in(reg) dmw0,
            dmw1 = in(reg) dmw1,
            options(nostack)
        );

        // Switch from direct-address mode to mapped-address mode:
        // clear DA, set PG.
        let mut crmd: usize;
        asm!("csrrd {0}, 0x0", out(reg) crmd, options(nostack));

        crmd &= !CRMD_DA;
        crmd |= CRMD_PG;

        asm!("csrwr {0}, 0x0", in(reg) crmd, options(nostack));
    }

    log::info!("[mm] LoongArch mapped mode enabled with DMW");
}