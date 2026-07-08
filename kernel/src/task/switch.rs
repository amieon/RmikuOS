use core::arch::global_asm;

#[cfg(target_arch = "riscv64")]
global_asm!(include_str!("switch_riscv64.S"));

#[cfg(target_arch = "loongarch64")]
global_asm!(include_str!("switch_loongarch64.S"));

use crate::task::processor;

use super::context::TaskContext;


unsafe extern "C" {
    pub fn __switch(current: *mut TaskContext, next: *const TaskContext);
}

pub unsafe fn switch_unlock_and_switch(to: *mut TaskContext) {
    // 关中断，防止切换过程中被抢占
    crate::arch::disable_interrupt();
    // 释放大内核锁，允许其他核进入调度
    crate::syscall::bkl_unlock();
    // 获取当前核的 idle 上下文指针
    let idle_cx_ptr = super::processor::idle_task_cx_ptr();
    unsafe {
        __switch(idle_cx_ptr, to);
    }

}