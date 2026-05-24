// kernel/src/arch/loongarch64/boot.rs
use core::arch::global_asm;

global_asm!(include_str!("boot.S"));