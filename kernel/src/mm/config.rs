// kernel/src/mm/config.rs

/// 4 KiB page size.
pub const PAGE_SIZE: usize = 0x1000;
pub const PAGE_SIZE_BITS: usize = 12;
pub const KERNEL_HEAP_SIZE: usize = 8 * 1024 * 1024;
pub const KERNEL_OFFSET: usize = 0xffff_ffc0_0000_0000;