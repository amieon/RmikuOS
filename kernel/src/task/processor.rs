use crate::sync::spin::Mutex;

use super::context::TaskContext;

pub struct Processor {
    pub current: Option<usize>,
    pub idle_task_cx: TaskContext,
}

impl Processor {
    pub const fn new() -> Self {
        Self {
            current: None,
            idle_task_cx: TaskContext::zero(),
        }
    }

    pub fn current(&self) -> Option<usize> {
        self.current
    }

    pub fn idle_task_cx_ptr(&mut self) -> *mut TaskContext {
        &mut self.idle_task_cx as *mut TaskContext
    }
}

static PROCESSOR: Mutex<Processor> = Mutex::new(Processor::new());

pub fn current_task_id() -> usize {
    PROCESSOR
        .lock()
        .current()
        .expect("no current task")
}

pub fn set_current(id: Option<usize>) {
    PROCESSOR.lock().current = id;
}

pub fn idle_task_cx_ptr() -> *mut TaskContext {
    PROCESSOR.lock().idle_task_cx_ptr()
}