#[cfg(target_arch = "riscv64")]
#[path = "riscv64/mod.rs"]
mod arch_trap;

#[cfg(target_arch = "loongarch64")]
#[path = "loongarch64/mod.rs"]
mod arch_trap;


pub use arch_trap::*;