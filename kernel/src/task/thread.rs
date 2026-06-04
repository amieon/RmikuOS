use crate::trap::TrapContext;

use super::context::TaskContext;
use super::kernel_stack::KernelStack;
use super::process::Pid;

pub type Tid = usize;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum ThreadStatus {
    Ready,
    Running,
    Blocking,
    Zombie,
    Dead,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum BlockReason {
    None,

    Sleep {
        wake_tick: usize,
    },

    WaitPid {
        pid: isize,
    },

    Join {
        tid: Tid,
    },
}

pub struct ThreadControlBlock {
    pub tid: Tid,
    pub pid: Pid,

    pub kernel_stack: KernelStack,
    pub trap_cx_addr: usize,
    pub task_cx: TaskContext,

    pub status: ThreadStatus,
    pub block_reason: BlockReason,

    pub exit_code: i32,
}

impl ThreadControlBlock {
    pub fn new_main_thread(
        tid: Tid,
        pid: Pid,
        trap_cx: TrapContext,
    ) -> Self {
        let kernel_stack = KernelStack::new();

        let trap_cx_ptr = unsafe {
            kernel_stack.push_context(trap_cx)
        };

        let task_cx = TaskContext::goto_task_entry(trap_cx_ptr as usize);

        Self {
            tid,
            pid,

            kernel_stack,
            trap_cx_addr: trap_cx_ptr as usize,
            task_cx,

            status: ThreadStatus::Ready,
            block_reason: BlockReason::None,

            exit_code: 0,
        }
    }

    pub fn trap_cx(&self) -> &TrapContext {
        unsafe {
            &*(self.trap_cx_addr as *const TrapContext)
        }
    }

    pub fn trap_cx_mut(&mut self) -> &mut TrapContext {
        unsafe {
            &mut *(self.trap_cx_addr as *mut TrapContext)
        }
    }

    pub fn task_cx_ptr(&mut self) -> *mut TaskContext {
        &mut self.task_cx as *mut TaskContext
    }

    pub fn trap_cx_ptr_addr(&self) -> usize {
        self.trap_cx_addr
    }

    pub fn is_ready(&self) -> bool {
        self.status == ThreadStatus::Ready
    }

    pub fn is_blocking(&self) -> bool {
        self.status == ThreadStatus::Blocking
    }

    pub fn is_zombie(&self) -> bool {
        self.status == ThreadStatus::Zombie
    }
}