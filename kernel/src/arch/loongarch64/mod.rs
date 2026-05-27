// src/arch/loongarch64/mod.rs
pub mod boot; 
pub const NAME: &str = "LoongArch 64";
pub const MAX_HARTS: usize = 8;

/// The kernel is loaded at 0x0100_0000 by the QEMU loader in run.sh.
pub const MEMORY_START: usize = 0x0100_0000;

/// run.sh uses `-m 2G` for LoongArch.
pub const MEMORY_SIZE: usize = 2 * 1024 * 1024 * 1024;

pub const MEMORY_END: usize = MEMORY_START + MEMORY_SIZE;

pub const UART_PADDR: usize = 0x1fe0_01e0;
pub const UART_BASE: usize = crate::mm::config::KERNEL_OFFSET + UART_PADDR;



/// 读取当前核的 CPUID
#[inline]
pub fn hartid() -> usize {
    let id: usize;
    unsafe {
        core::arch::asm!("csrrd {}, 0x20", out(reg) id);
    }
    id
}