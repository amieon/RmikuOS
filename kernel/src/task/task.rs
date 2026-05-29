use alloc::boxed::Box;

use crate::mm::{MemorySet, PhysPageNum};
use crate::trap::TrapContext;

use super::kernel_stack::KernelStack;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum TaskStatus {
    Ready,
    Running,
    Exited,
}

pub struct TaskControlBlock {
    pub id: usize,
    pub user_space: MemorySet,
    pub kernel_stack: Box<KernelStack>,
    pub trap_cx_ptr: *mut TrapContext,
    pub status: TaskStatus,
}

impl TaskControlBlock {
    pub fn new(id: usize, app: &[u8]) -> Self {
        let (user_space, entry, user_sp) = MemorySet::new_user_test(app);
        let trap_cx = TrapContext::app_init_context(entry, user_sp);

        let kernel_stack = Box::new(KernelStack::new());

        let trap_cx_ptr = unsafe {
            kernel_stack.push_context(trap_cx)
        };

        Self {
            id,
            user_space,
            kernel_stack,
            trap_cx_ptr,
            status: TaskStatus::Ready,
        }
    }

    pub fn root_ppn(&self) -> PhysPageNum {
        self.user_space.root_ppn()
    }
}