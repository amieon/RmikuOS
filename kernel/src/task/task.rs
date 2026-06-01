use alloc::boxed::Box;
use alloc::vec::Vec;

use crate::mm::{MemorySet, PhysPageNum};
use crate::trap::TrapContext;

use super::context::TaskContext;
use super::kernel_stack::KernelStack;

use alloc::sync::Arc;
use crate::fs::FileRef;


#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum TaskStatus {
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
}

pub struct TaskControlBlock {
    pub id: usize,

    pub parent: Option<usize>,
    pub children: Vec<usize>,

    pub user_space: MemorySet,
    pub kernel_stack: KernelStack,
    pub trap_cx_addr: usize,
    pub task_cx: TaskContext,

    pub status: TaskStatus,
    pub block_reason: BlockReason,

    pub fd_table: Vec<Option<FileRef>>,

    pub exit_code: i32,
}

impl TaskControlBlock {
    pub fn new(id: usize, app: &[u8]) -> Self {
        let (user_space, entry, user_sp) = MemorySet::new_user_test(app);


        let trap_cx = TrapContext::app_init_context(entry, user_sp);

        let kernel_stack = KernelStack::new();

        let trap_cx_ptr = unsafe {
            kernel_stack.push_context(trap_cx)
        };

        let task_cx = TaskContext::goto_task_entry(trap_cx_ptr as usize);

        Self {
            id,

            parent: None,
            children: Vec::new(),

            user_space,
            kernel_stack,
            trap_cx_addr: trap_cx_ptr as usize,
            task_cx,

            status: TaskStatus::Ready,
            block_reason: BlockReason::None,

            fd_table: Self::new_fd_table(),

            exit_code: 0,
        }
    }

    pub fn fork_from(
        id: usize,
        parent: usize,
        user_space: MemorySet,
        trap_cx: TrapContext,
        fd_table: Vec<Option<FileRef>>,
    ) -> Self {
        
          
        let kernel_stack = KernelStack::new();

 
        let trap_cx_ptr = unsafe {
            kernel_stack.push_context(trap_cx)
        };

        let task_cx = TaskContext::goto_task_entry(trap_cx_ptr as usize);

        Self {
            id,

            parent: Some(parent),
            children: Vec::new(),

            user_space,
            kernel_stack,
            trap_cx_addr: trap_cx_ptr as usize,
            task_cx,

            status: TaskStatus::Ready,
            block_reason: BlockReason::None,

            fd_table : fd_table,

            exit_code: 0,
        }
    }

    pub fn root_ppn(&self) -> PhysPageNum {
        self.user_space.root_ppn()
    }

    pub fn trap_cx(&self) -> &TrapContext {
        unsafe { &*(self.trap_cx_addr as *const TrapContext) }
    }

    pub fn trap_cx_mut(&mut self) -> &mut TrapContext {
        unsafe { &mut *(self.trap_cx_addr as *mut TrapContext) }
    }

    pub fn task_cx_ptr(&mut self) -> *mut TaskContext {
        &mut self.task_cx as *mut TaskContext
    }



    pub fn new_fd_table() -> Vec<Option<FileRef>> {
        let mut fd_table = Vec::new();

        /*
        * fd 0: stdin
        * fd 1: stdout
        * fd 2: stderr，暂时也接 stdout
        */
        fd_table.push(Some(crate::fs::stdin()));
        fd_table.push(Some(crate::fs::stdout()));
        fd_table.push(Some(crate::fs::stdout()));

        fd_table
    }
}



