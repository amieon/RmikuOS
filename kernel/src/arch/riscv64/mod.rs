 // src/arch/riscv64/mod.rs
pub const NAME: &str = "RISC-V 64";
pub const UART_PADDR: usize = 0x1000_0000;
pub const UART_BASE: usize = crate::mm::config::KERNEL_OFFSET + UART_PADDR;
pub const MAX_HARTS: usize = 8;


/// QEMU virt DRAM starts at 0x8000_0000.
pub const MEMORY_START: usize = 0x8000_0000;

/// run.sh uses `-m 128M` for RISC-V.
pub const MEMORY_SIZE: usize = 128 * 1024 * 1024;
pub const KERNEL_DIRECT_MAP_SIZE: usize = 128 * 1024 * 1024;

pub const MEMORY_END: usize = MEMORY_START + MEMORY_SIZE;


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

pub fn enable_interrupt() {
    unsafe {
        //sstatus.SIE = bit 1
        core::arch::asm!(
            "csrs sstatus, {0}",
            in(reg) 1usize << 1,
            options(nostack)
        );
    }
}

pub fn disable_interrupt() {
    unsafe {
        //clear sstatus.SIE
        core::arch::asm!(
            "csrc sstatus, {0}",
            in(reg) 1usize << 1,
            options(nostack)
        );
    }
}

pub fn wait_for_interrupt() {
    unsafe {
        core::arch::asm!("wfi", options(nostack));
    }
}