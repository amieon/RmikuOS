/// The kernel is loaded at 0x0100_0000 by the QEMU loader in run.sh.
pub const MEMORY_START: usize = 0x0100_0000;

/// run.sh uses `-m 2G` for LoongArch.
pub const MEMORY_SIZE: usize = 2 * 1024 * 1024 * 1024;

pub const MEMORY_END: usize = MEMORY_START + MEMORY_SIZE;

/// Early UART/MMIO address should still come from arch::UART_BASE.
pub const UART0: usize = crate::arch::UART_BASE;
