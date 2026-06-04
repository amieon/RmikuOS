use crate::{sync::spin::Mutex, task::thread::Tid};

use super::context::TaskContext;

pub struct Processor {
    pub current_tid: Option<Tid>,
    pub idle_task_cx: TaskContext,
}

impl Processor {
    pub const fn new() -> Self {
        Self {
            current_tid: None,
            idle_task_cx: TaskContext::zero(),
        }
    }

    pub fn current_tid(&self) -> Option<usize> {
        self.current_tid
    }

    pub fn idle_task_cx_ptr(&mut self) -> *mut TaskContext {
        &mut self.idle_task_cx as *mut TaskContext
    }
}

static PROCESSOR: Mutex<Processor> = Mutex::new(Processor::new());

pub fn current_tid() -> Tid {
    PROCESSOR
        .lock()
        .current_tid()
        .expect("no current task")
}

pub fn set_current_tid(id: Option<Tid>) {
    PROCESSOR.lock().current_tid = id;
}

pub fn idle_task_cx_ptr() -> *mut TaskContext {
    PROCESSOR.lock().idle_task_cx_ptr()
}