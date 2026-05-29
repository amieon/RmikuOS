// kernel/src/task/kernel_stack.rs

use crate::trap::TrapContext;

pub const KERNEL_STACK_SIZE: usize = 16 * 1024;
const TRAP_CONTEXT_SIZE: usize = core::mem::size_of::<TrapContext>();

#[repr(align(16))]
pub struct KernelStack {
    data: [u8; KERNEL_STACK_SIZE],
}

impl KernelStack {
    pub const fn new() -> Self {
        Self {
            data: [0; KERNEL_STACK_SIZE],
        }
    }

    pub fn top(&self) -> usize {
        self.data.as_ptr() as usize + KERNEL_STACK_SIZE
    }

    pub unsafe fn push_context(&self, cx: TrapContext) -> *mut TrapContext {
        let cx_ptr = (self.top() - TRAP_CONTEXT_SIZE) as *mut TrapContext;
        cx_ptr.write(cx);
        cx_ptr
    }
}