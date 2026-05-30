use alloc::boxed::Box;

use crate::mm::{MemorySet, PhysPageNum};
use crate::trap::TrapContext;

use super::context::TaskContext;
use super::kernel_stack::KernelStack;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum TaskStatus {
    Ready,
    Running,
    Zombie,
}

pub struct TaskControlBlock {
    pub id: usize,
    pub user_space: MemorySet,
    pub kernel_stack: Box<KernelStack>,
    pub trap_cx_addr: usize,
    pub task_cx: TaskContext,
    pub status: TaskStatus,
    pub exit_code: i32,
}

impl TaskControlBlock {
    pub fn new(id: usize, app: &[u8]) -> Self {
        let (user_space, entry, user_sp) = MemorySet::new_user_test(app);
        let trap_cx = TrapContext::app_init_context(entry, user_sp);

        let kernel_stack = Box::new(KernelStack::new());

        let trap_cx_ptr = unsafe {
            kernel_stack.push_context(trap_cx)
        };

        
        //第一次被 scheduler 切入时，进入 __task_entry。
        //Rust 栈放在 trap_cx 下方，避免覆盖 TrapContext。
        let task_cx = TaskContext::goto_task_entry(trap_cx_ptr as usize);

        Self {
            id,
            user_space,
            kernel_stack,
            trap_cx_addr: trap_cx_ptr as usize,
            task_cx,
            status: TaskStatus::Ready,
            exit_code: 0,
        }
    }

    pub fn root_ppn(&self) -> PhysPageNum {
        self.user_space.root_ppn()
    }

    pub fn trap_cx_ptr(&self) -> *const TrapContext {
        self.trap_cx_addr as *const TrapContext
    }

    pub fn task_cx_ptr(&mut self) -> *mut TaskContext {
        &mut self.task_cx as *mut TaskContext
    }
}