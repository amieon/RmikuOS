use alloc::vec::Vec;
use crate::sync::spin::Mutex;

use crate::mm::PhysPageNum;
use crate::trap::TrapContext;

use super::task::{TaskControlBlock, TaskStatus};

unsafe extern "C" {
    fn __restore_user(cx: *const TrapContext) -> !;
}

#[derive(Clone, Copy)]
struct TaskRunInfo {
    id: usize,
    root: PhysPageNum,
    kstack_top: usize,
    trap_cx_addr: usize,
}

pub struct TaskManager {
    tasks: Vec<TaskControlBlock>,
    current: usize,
}

impl TaskManager {
    pub const fn new() -> Self {
        Self {
            tasks: Vec::new(),
            current: 0,
        }
    }

    pub fn add_task(&mut self, task: TaskControlBlock) {
        self.tasks.push(task);
    }

    fn find_next_ready(&self) -> Option<usize> {
        if self.tasks.is_empty() {
            return None;
        }

        let n = self.tasks.len();

        for step in 1..=n {
            let id = (self.current + step) % n;
            if self.tasks[id].status == TaskStatus::Ready {
                return Some(id);
            }
        }

        None
    }

    fn prepare_run_task(&mut self, id: usize) -> TaskRunInfo {
        self.current = id;
        self.tasks[id].status = TaskStatus::Running;

        let task = &self.tasks[id];

        TaskRunInfo {
            id: task.id,
            root: task.root_ppn(),
            kstack_top: task.kernel_stack.top(),
            trap_cx_addr: task.trap_cx_addr,
        }
    }

    fn prepare_first_task(&mut self) -> TaskRunInfo {
        if self.tasks.is_empty() {
            panic!("no user task");
        }

        self.prepare_run_task(0)
    }

    fn exit_current_and_prepare_next(&mut self, exit_code: i32) -> Option<TaskRunInfo> {
        let current = self.current;

        log::info!(
            "[task] task {} exited with code {}",
            self.tasks[current].id,
            exit_code,
        );

        self.tasks[current].status = TaskStatus::Exited;

        let next = self.find_next_ready()?;
        Some(self.prepare_run_task(next))
    }
}
static TASK_MANAGER: Mutex<TaskManager> = Mutex::new(TaskManager::new());

pub fn init() {
    let mut manager = TASK_MANAGER.lock();

    for id in 0..crate::loader::num_apps() {
        let app = crate::loader::get_app_data(id);
        manager.add_task(TaskControlBlock::new(id, app));
    }

    log::info!("[task] loaded {} user tasks", crate::loader::num_apps());
}

pub fn run_first_task() -> ! {
    let info = {
        let mut manager = TASK_MANAGER.lock();
        manager.prepare_first_task()
    };

    run_task(info)
}

pub fn exit_current_and_run_next(exit_code: i32) -> ! {
    let next = {
        let mut manager = TASK_MANAGER.lock();
        manager.exit_current_and_prepare_next(exit_code)
    };

    if let Some(info) = next {
        run_task(info);
    }

    log::info!("[task] all user tasks exited");

    loop {
        core::hint::spin_loop();
    }
}

fn run_task(info: TaskRunInfo) -> ! {
    log::info!(
        "[task] run task {}: root={:?}, kstack_top={:#x}, trap_cx={:#x}",
        info.id,
        info.root,
        info.kstack_top,
        info.trap_cx_addr,
    );

    crate::mm::activate_page_table(info.root);

    unsafe {
        __restore_user(info.trap_cx_addr as *const TrapContext);
    }
}