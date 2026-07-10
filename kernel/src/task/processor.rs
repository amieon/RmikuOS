use crate::arch::MAX_HARTS;
use crate::print;
use crate::task::thread::Tid;
use super::context::TaskContext;


static mut PROCESSORS: [Processor; MAX_HARTS] =
    [const { Processor::new() }; MAX_HARTS];

pub struct Processor {
    pub current_tid: Option<Tid>,
    pub idle_task_cx: TaskContext,
    pub force_exit: bool,
    pub need_resched: bool,
    pub preempt_count: usize,

    pub pending_ready_tid: Option<Tid>,
}
impl Processor {
    pub const fn new() -> Self {
        Self {
            current_tid: None,
            idle_task_cx: TaskContext::zero(),
            force_exit: false,
            need_resched: false,
            preempt_count: 0,
            pending_ready_tid: None,
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
    unsafe {
        core::arch::asm!("csrrd {}, 0x20", out(reg) hartid, options(nostack));
    }
    hartid & 0x1FF  // 取 CoreID 低 9 位
}
// ============== 无锁访问 ==============
fn processor() -> &'static mut Processor {
    let hart = current_hart_id();
    // crate::io::uart::print_i32(hart as i32);
    unsafe { &mut PROCESSORS[hart] }
}

pub fn current_tid() -> Tid {
    match processor().current_tid {
        Some(tid) => tid,
        None => {
            let hart = current_hart_id();

            #[cfg(target_arch = "riscv64")]
            {
                let sepc: usize;
                let sstatus: usize;

                unsafe {
                    core::arch::asm!("csrr {}, sepc", out(reg) sepc, options(nostack));
                    core::arch::asm!("csrr {}, sstatus", out(reg) sstatus, options(nostack));
                }

                panic!(
                    "no current task: hart={}, sepc={:#x}, sstatus={:#x}",
                    hart,
                    sepc,
                    sstatus,
                );
            }

            #[cfg(target_arch = "loongarch64")]
            {
                let era: usize;
                let prmd: usize;
                let crmd: usize;
                let estat: usize;

                unsafe {
                    // ERA  = CSR 0x6，异常返回地址
                    // PRMD = CSR 0x1，保存异常前的特权级/中断状态
                    // CRMD = CSR 0x0，当前模式
                    // ESTAT = CSR 0x5，异常/中断状态
                    core::arch::asm!("csrrd {}, 0x6", out(reg) era, options(nostack));
                    core::arch::asm!("csrrd {}, 0x1", out(reg) prmd, options(nostack));
                    core::arch::asm!("csrrd {}, 0x0", out(reg) crmd, options(nostack));
                    core::arch::asm!("csrrd {}, 0x5", out(reg) estat, options(nostack));
                }

                panic!(
                    "no current task: hart={}, era={:#x}, prmd={:#x}, crmd={:#x}, estat={:#x}",
                    hart,
                    era,
                    prmd,
                    crmd,
                    estat,
                );
            }
        }
    }
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

pub fn set_current_force_exit(val: bool) {
    processor().force_exit = val;
}

pub fn check_and_clear_force_exit() -> bool {
    let proc = processor();
    let old = proc.force_exit;
    proc.force_exit = false;
    old
}

pub fn set_current_need_resched(val: bool) {
    processor().need_resched = val;
}

pub fn check_and_clear_need_resched() -> bool {
    let proc = processor();
    let old = proc.need_resched;
    proc.need_resched = false;
    old
}

pub fn preempt_disable() {
    processor().preempt_count += 1;
    // 可选：内存屏障防止乱序
    core::sync::atomic::fence(core::sync::atomic::Ordering::SeqCst);
}

pub fn preempt_enable() {
    let p = processor();
    if p.preempt_count > 0 {
        p.preempt_count -= 1;
    }
}

pub fn can_preempt() -> bool {
    processor().preempt_count == 0
}

pub fn set_pending_ready_tid(tid: Tid) {
    processor().pending_ready_tid = Some(tid);
}

pub fn take_pending_ready_tid() -> Option<Tid> {
    let p = processor();
    let old = p.pending_ready_tid;
    p.pending_ready_tid = None;
    old
}