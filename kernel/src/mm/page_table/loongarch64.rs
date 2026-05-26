/// The kernel is loaded at 0x0100_0000 by the QEMU loader in run.sh.
pub const MEMORY_START: usize = 0x0100_0000;

/// run.sh uses `-m 2G` for LoongArch.
pub const MEMORY_SIZE: usize = 2 * 1024 * 1024 * 1024;

pub const MEMORY_END: usize = MEMORY_START + MEMORY_SIZE;

/// Early UART/MMIO address should still come from arch::UART_BASE.
pub const UART0: usize = crate::arch::UART_BASE;

use core::arch::asm;


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

