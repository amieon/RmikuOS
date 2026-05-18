// src/arch/mod.rs
#[cfg(target_arch = "riscv64")]
#[path = "riscv64/mod.rs"]
mod platform;

#[cfg(target_arch = "loongarch64")]
#[path = "loongarch64/mod.rs"]
mod platform;

pub use platform::*;