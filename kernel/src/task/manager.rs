use alloc::vec::Vec;

use crate::mm::{PhysPageNum, VirtAddr, PAGE_SIZE_BITS};
use crate::mm::config::PAGE_SIZE;
use crate::sync::spin::Mutex;
use crate::trap::TrapContext;

use super::context::TaskContext;
use super::processor;
use super::switch::__switch;
use super::task::{TaskControlBlock, TaskStatus};

unsafe extern "C" {
    fn __restore_user(cx: *const TrapContext) -> !;
}

pub struct TaskManager {
    tasks: Vec<TaskControlBlock>,
}

impl TaskManager {
    pub const fn new() -> Self {
        Self { tasks: Vec::new() }
    }

    pub fn add_task(&mut self, task: TaskControlBlock) {
        self.tasks.push(task);
    }

    fn find_next_ready(&self) -> Option<usize> {
        for id in 0..self.tasks.len() {
            if self.tasks[id].status == TaskStatus::Ready {
                return Some(id);
            }
        }
        None
    }

    fn mark_current_ready(&mut self, id: usize) {
        if self.tasks[id].status == TaskStatus::Running {
            self.tasks[id].status = TaskStatus::Ready;
        }
    }

    fn mark_current_zombie(&mut self, id: usize, exit_code: i32) {
        self.tasks[id].status = TaskStatus::Zombie;
        self.tasks[id].exit_code = exit_code;
    }

    fn task_cx_ptr(&mut self, id: usize) -> *mut TaskContext {
        self.tasks[id].task_cx_ptr()
    }

    fn prepare_task(&mut self, id: usize) -> (PhysPageNum, usize, usize, *mut TaskContext) {
        self.tasks[id].status = TaskStatus::Running;

        let task = &mut self.tasks[id];

        (
            task.root_ppn(),
            task.kernel_stack.top(),
            task.trap_cx_addr,
            task.task_cx_ptr(),
        )
    }

    pub fn read_current_user_bytes(&self, user_buf: usize, len: usize) -> Option<Vec<u8>> {
        let current = processor::current_task_id();
        let task = self.tasks.get(current)?;

        let mut bytes = Vec::new();

        for offset in 0..len {
            let va = user_buf.checked_add(offset)?;
            let vpn = VirtAddr(va).floor();
            let page_offset = va & (PAGE_SIZE - 1);

            let pte = task.user_space.translate(vpn)?;

            let pa = (pte.ppn().0 << PAGE_SIZE_BITS) + page_offset;
            let kva = crate::mm::kernel_phys_to_virt(pa);

            let byte = unsafe { core::ptr::read_volatile(kva as *const u8) };
            bytes.push(byte);
        }

        Some(bytes)
    }
}

static TASK_MANAGER: Mutex<TaskManager> = Mutex::new(TaskManager::new());

pub fn init() {
    let mut manager = TASK_MANAGER.lock();

    for id in 0..crate::loader::num_apps() {
        let app = crate::loader::get_app_data(id);
        let name = crate::loader::get_app_name(id);

        log::info!(
            "[task] load app {}: name={}, size={} bytes",
            id,
            name,
            app.len(),
        );

        manager.add_task(TaskControlBlock::new(id, app));
    }

    log::info!("[task] loaded {} user tasks", crate::loader::num_apps());
}

pub fn run_first_task() -> ! {
    run_tasks()
}

pub fn run_tasks() -> ! {
    loop {
        let next = {
            let mut manager = TASK_MANAGER.lock();

            if let Some(id) = manager.find_next_ready() {
                let (root, kstack_top, trap_cx_addr, task_cx_ptr) = manager.prepare_task(id);

                log::info!(
                    "[task] schedule task {}: root={:?}, kstack_top={:#x}, trap_cx={:#x}",
                    id,
                    root,
                    kstack_top,
                    trap_cx_addr,
                );

                Some((id, root, task_cx_ptr))
            } else {
                None
            }
        };

        if let Some((id, root, task_cx_ptr)) = next {
            processor::set_current(Some(id));

            crate::mm::activate_page_table(root);

            let idle_cx_ptr = processor::idle_task_cx_ptr();

            unsafe {
                __switch(idle_cx_ptr, task_cx_ptr);
            }

            processor::set_current(None);
        } else {
            log::info!("[task] no ready task");

            loop {
                core::hint::spin_loop();
            }
        }
    }
}

#[no_mangle]
pub extern "C" fn __task_entry() -> ! {
    let current = processor::current_task_id();

    let trap_cx_addr = {
        let manager = TASK_MANAGER.lock();
        manager.tasks[current].trap_cx_addr
    };

    unsafe {
        __restore_user(trap_cx_addr as *const TrapContext);
    }
}

pub fn suspend_current_and_run_next() -> isize {
    let current = processor::current_task_id();

    let task_cx_ptr = {
        let mut manager = TASK_MANAGER.lock();
        manager.mark_current_ready(current);
        manager.task_cx_ptr(current)
    };

    let idle_cx_ptr = processor::idle_task_cx_ptr();

    unsafe {
        __switch(task_cx_ptr, idle_cx_ptr);
    }

    0
}

pub fn preempt_current_and_run_next() {
    let current = processor::current_task_id();

    let task_cx_ptr = {
        let mut manager = TASK_MANAGER.lock();
        manager.mark_current_ready(current);
        manager.task_cx_ptr(current)
    };

    let idle_cx_ptr = processor::idle_task_cx_ptr();

    unsafe {
        __switch(task_cx_ptr, idle_cx_ptr);
    }
}

pub fn exit_current_and_run_next(exit_code: i32) -> ! {
    let current = processor::current_task_id();

    let task_cx_ptr = {
        let mut manager = TASK_MANAGER.lock();

        log::info!(
            "[task] task {} exited with code {}",
            current,
            exit_code,
        );

        manager.mark_current_zombie(current, exit_code);
        manager.task_cx_ptr(current)
    };

    let idle_cx_ptr = processor::idle_task_cx_ptr();

    unsafe {
        __switch(task_cx_ptr, idle_cx_ptr);
    }

    panic!("zombie task returned after exit");
}

pub fn current_task_id() -> usize {
    processor::current_task_id()
}

pub fn read_current_user_bytes(user_buf: usize, len: usize) -> Option<Vec<u8>> {
    let manager = TASK_MANAGER.lock();
    manager.read_current_user_bytes(user_buf, len)
}