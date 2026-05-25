// kernel/src/timer/mod.rs

#[cfg(target_arch = "riscv64")]
#[path = "riscv64.rs"]
mod arch_timer;

#[cfg(target_arch = "loongarch64")]
#[path = "loongarch64.rs"]
mod arch_timer;

pub use arch_timer::*;