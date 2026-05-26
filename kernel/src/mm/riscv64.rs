/// QEMU virt DRAM starts at 0x8000_0000.
pub const MEMORY_START: usize = 0x8000_0000;

/// run.sh uses `-m 128M` for RISC-V.
pub const MEMORY_SIZE: usize = 128 * 1024 * 1024;

pub const MEMORY_END: usize = MEMORY_START + MEMORY_SIZE;

/// UART0 on QEMU virt.
pub const UART0: usize = 0x1000_0000;
