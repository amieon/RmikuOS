// kernel/src/mm/config.rs

/// 4 KiB page size.
pub const PAGE_SIZE: usize = 0x1000;
pub const PAGE_SIZE_BITS: usize = 12;
pub const KERNEL_HEAP_SIZE: usize = 16 * 1024 * 1024;

#[cfg(target_arch = "riscv64")]
pub const KERNEL_OFFSET: usize = 0xffff_ffc0_0000_0000;

#[cfg(target_arch = "loongarch64")]
pub const KERNEL_OFFSET: usize = 0xffff_8000_0000_0000;