// kernel/src/task/manager.rs

use alloc::boxed::Box;

use crate::trap::TrapContext;

use super::task::{TaskControlBlock, TaskStatus};

static mut FIRST_TASK: Option<&'static mut TaskControlBlock> = None;

unsafe extern "C" {
    fn __restore_user(cx: *const TrapContext) -> !;
}

pub fn init_first_task(app: &[u8]) {
    let task = Box::new(TaskControlBlock::new(app));
    let task = Box::leak(task);

    unsafe {
        FIRST_TASK = Some(task);
    }
}

pub fn run_first_task() -> ! {
    let task = unsafe {
        FIRST_TASK
            .as_mut()
            .expect("task::init_first_task must be called before run_first_task")
    };

    task.status = TaskStatus::Running;

    let root = task.root_ppn();

    log::info!(
        "[task] run first task: root={:?}, kstack_top={:#x}, trap_cx={:#x}",
        root,
        task.kernel_stack.top(),
        task.trap_cx_ptr as usize,
    );

    crate::mm::activate_page_table(root);

    unsafe {
        __restore_user(task.trap_cx_ptr as *const TrapContext);
    }
}