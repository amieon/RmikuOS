// src/arch/loongarch64/mod.rs
pub mod boot; 
pub const NAME: &str = "LoongArch 64";
pub const UART_BASE: usize = 0x1fe0_01e0;
pub const MAX_HARTS: usize = 8;

/// 读取当前核的 CPUID
#[inline]
pub fn hartid() -> usize {
    let id: usize;
    unsafe {
        core::arch::asm!("csrrd {}, 0x20", out(reg) id);
    }
    id
}