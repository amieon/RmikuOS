use alloc::vec::Vec;

use crate::trap::TrapContext;

use super::task::{TaskControlBlock, TaskStatus};

unsafe extern "C" {
    fn __restore_user(cx: *const TrapContext) -> !;
}

pub struct TaskManager {
    tasks: Vec<TaskControlBlock>,
    current: usize,
}

impl TaskManager {
    pub fn new() -> Self {
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

    fn run_task(&mut self, id: usize) -> ! {
        self.current = id;
        self.tasks[id].status = TaskStatus::Running;

        let task = &self.tasks[id];

        log::info!(
            "[task] run task {}: root={:?}, kstack_top={:#x}, trap_cx={:#x}",
            task.id,
            task.root_ppn(),
            task.kernel_stack.top(),
            task.trap_cx_ptr as usize,
        );

        crate::mm::activate_page_table(task.root_ppn());

        unsafe {
            __restore_user(task.trap_cx_ptr as *const TrapContext);
        }
    }

    pub fn run_first_task(&mut self) -> ! {
        if self.tasks.is_empty() {
            panic!("no user task");
        }

        self.current = 0;
        self.run_task(0)
    }

    pub fn exit_current_and_run_next(&mut self, exit_code: i32) -> ! {
        let current = self.current;

        log::info!(
            "[task] task {} exited with code {}",
            self.tasks[current].id,
            exit_code,
        );

        self.tasks[current].status = TaskStatus::Exited;

        if let Some(next) = self.find_next_ready() {
            self.run_task(next);
        }

        log::info!("[task] all user tasks exited");

        loop {
            core::hint::spin_loop();
        }
    }
}

static mut TASK_MANAGER: Option<TaskManager> = None;

pub fn init() {
    let mut manager = TaskManager::new();

    for id in 0..crate::loader::num_apps() {
        let app = crate::loader::get_app_data(id);
        manager.add_task(TaskControlBlock::new(id, app));
    }

    unsafe {
        TASK_MANAGER = Some(manager);
    }

    log::info!("[task] loaded {} user tasks", crate::loader::num_apps());
}

pub fn run_first_task() -> ! {
    unsafe {
        TASK_MANAGER
            .as_mut()
            .expect("task manager not initialized")
            .run_first_task()
    }
}

pub fn exit_current_and_run_next(exit_code: i32) -> ! {
    unsafe {
        TASK_MANAGER
            .as_mut()
            .expect("task manager not initialized")
            .exit_current_and_run_next(exit_code)
    }
}