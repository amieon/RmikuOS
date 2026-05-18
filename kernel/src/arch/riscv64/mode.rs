// src/arch/riscv64/mod.rs
pub const NAME: &str = "RISC-V 64";
pub const UART_BASE: usize = 0x1000_0000;
pub const MAX_HARTS: usize = 8;

/// 读取当前核的 hartid
/// 在 boot.S 里已经把 hartid 存到了 tp 寄存器
#[inline]
pub fn hartid() -> usize {
    let id: usize;
    unsafe {
        core::arch::asm!("mv {}, tp", out(reg) id);
    }
    id
}