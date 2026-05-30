use core::arch::global_asm;

#[cfg(target_arch = "riscv64")]
global_asm!(include_str!("switch_riscv64.S"));

#[cfg(target_arch = "loongarch64")]
global_asm!(include_str!("switch_loongarch64.S"));

use super::context::TaskContext;

extern "C" {
    pub fn __switch(current_task_cx_ptr: *mut TaskContext, next_task_cx_ptr: *const TaskContext);
}