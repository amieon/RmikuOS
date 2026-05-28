use crate::mm::PAGE_SIZE;

pub const USER_TEXT_BASE: usize = 0x0001_0000;

#[cfg(target_arch = "riscv64")]
pub const USER_TOP: usize = 0x8000_0000;

#[cfg(target_arch = "loongarch64")]
pub const USER_TOP: usize = crate::arch::MEMORY_START;

pub const USER_HEAP_BASE: usize = 0x0020_0000;
pub const USER_MMAP_BASE: usize = 0x0080_0000;

pub const TRAMPOLINE: usize = USER_TOP - PAGE_SIZE;
pub const TRAP_CONTEXT_BASE: usize = TRAMPOLINE - PAGE_SIZE;

pub const USER_STACK_SIZE: usize = 64 * 1024;
pub const USER_STACK_TOP: usize = TRAP_CONTEXT_BASE - PAGE_SIZE;
pub const USER_STACK_BOTTOM: usize = USER_STACK_TOP - USER_STACK_SIZE;