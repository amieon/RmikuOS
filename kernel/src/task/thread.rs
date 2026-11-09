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
    Stopped,
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

    PipeRead,
    PipeWrite,

    /* futex 等待。key 必须带 pid：不同进程的同一个用户 VA 指向不同物理页，
     * 只按 VA 匹配会跨进程误唤醒。第一版不做 PROCESS_SHARED，所以 (pid, uaddr)
     * 就是唯一键；将来要共享锁得换成 translate(uaddr).ppn()。 */
    Futex {
        pid: Pid,
        uaddr: usize,
    },
}
pub const DEFAULT_THREAD_TICKETS: usize = 100;

pub struct ThreadControlBlock {
    
    pub tid: Tid,
    pub running_on: Option<usize>,
    /* 属主核仍在切出途中时收到了跨核唤醒：先记账不置 Ready，
     * 由属主核 run_tasks 的 back-from 补真正的唤醒。 */
    pub wake_pending: bool,
    /* 同窗口内收到 SIGCONT：保持 Stopped 挂账，back-from 补恢复。 */
    pub cont_pending: bool,
    pub pid: Pid,

    pub kernel_stack: KernelStack,
    pub trap_cx_addr: usize,
    pub task_cx: TaskContext,

    pub status: ThreadStatus,
    pub block_reason: BlockReason,

    pub tickets: usize,
    pub stride: usize,
    pub pass: usize,

    pub run_ticks: usize,

    pub sig_pending: u64,
    pub sig_blocked: u64,

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
            running_on: None,
            wake_pending: false,
            cont_pending: false,
            pid,

            kernel_stack,
            trap_cx_addr: trap_cx_ptr as usize,
            task_cx,

            status: ThreadStatus::Ready,
            block_reason: BlockReason::None,

            tickets: DEFAULT_THREAD_TICKETS,
            stride: crate::task::process::stride_from_tickets(DEFAULT_THREAD_TICKETS),
            pass: 0,

            run_ticks: 0,

            sig_blocked: 0,
            sig_pending: 0,

            exit_code: 0,
        }
    }

    pub fn new_user_thread(
        tid: Tid,
        pid: Pid,
        entry: usize,
        user_sp: usize,
        arg0: usize,
        arg1: usize,
    ) -> Self {
        let mut trap_cx =
            TrapContext::app_init_context(entry, user_sp);

        trap_cx.set_thread_args(arg0, arg1);

        let kernel_stack = KernelStack::new();

        let trap_cx_ptr = unsafe {
            kernel_stack.push_context(trap_cx)
        };

        let task_cx = TaskContext::goto_task_entry(trap_cx_ptr as usize);

        Self {
            tid,
            running_on: None,
            wake_pending: false,
            cont_pending: false,
            pid,

            kernel_stack,
            trap_cx_addr: trap_cx_ptr as usize,
            task_cx,

            tickets: DEFAULT_THREAD_TICKETS,
            stride: crate::task::process::stride_from_tickets(DEFAULT_THREAD_TICKETS),
            pass: 0,

            status: ThreadStatus::Ready,
            block_reason: BlockReason::None,

            run_ticks: 0,

            sig_blocked: 0,
            sig_pending: 0,

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


