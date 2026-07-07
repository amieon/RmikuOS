use crate::arch::MAX_HARTS;
use crate::task::thread::Tid;
use super::context::TaskContext;

// ============== per-cpu 数组 ==============
static mut PROCESSORS: [Processor; MAX_HARTS] = [
    Processor::new(), Processor::new(), Processor::new(), Processor::new(),
    Processor::new(), Processor::new(), Processor::new(), Processor::new(),
];

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
}

// ============== hart id ==============
#[cfg(target_arch = "riscv64")]
#[inline]
pub fn current_hart_id() -> usize {
    let hartid: usize;
    unsafe { core::arch::asm!("mv {}, tp", out(reg) hartid, options(nostack)) };
    hartid
}

#[cfg(target_arch = "loongarch64")]
#[inline]
pub fn current_hart_id() -> usize {
    let hartid: usize;
    unsafe { core::arch::asm!("move {}, $r21", out(reg) hartid, options(nostack)) };
    hartid
}

// ============== 无锁访问 ==============
fn processor() -> &'static mut Processor {
    let hart = current_hart_id();
    unsafe { &mut PROCESSORS[hart] }
}

pub fn current_tid() -> Tid {
    processor().current_tid.expect("no current task")
}

pub fn current_tid_opt() -> Option<Tid> {
    processor().current_tid
}

pub fn set_current_tid(id: Option<Tid>) {
    processor().current_tid = id;
}

pub fn idle_task_cx_ptr() -> *mut TaskContext {
    &mut processor().idle_task_cx as *mut TaskContext
}