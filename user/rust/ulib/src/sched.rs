use crate::number::*;
use crate::syscall::syscall3;

pub fn set_process_tickets(pid: i32, tickets: i32) -> isize {
    unsafe { syscall3(SYS_SET_PROCESS_TICKETS, pid as usize, tickets as usize, 0) }
}

pub fn set_my_tickets(tickets: i32) -> isize {
    unsafe { syscall3(SYS_SET_MY_TICKETS, tickets as usize, 0, 0) }
}

pub fn get_process_tickets(pid: i32) -> isize {
    unsafe { syscall3(SYS_GET_PROCESS_TICKETS, pid as usize, 0, 0) }
}

pub fn get_my_tickets() -> isize {
    unsafe { syscall3(SYS_GET_MY_TICKETS, 0, 0, 0) }
}

pub fn set_thread_tickets(tid: i32, tickets: i32) -> isize {
    unsafe { syscall3(SYS_SET_THREAD_TICKETS, tid as usize, tickets as usize, 0) }
}

pub fn get_thread_tickets(tid: i32) -> isize {
    unsafe { syscall3(SYS_GET_THREAD_TICKETS, tid as usize, 0, 0) }
}

pub fn set_sched_alpha(alpha: i32) -> isize {
    unsafe { syscall3(SYS_SET_SCHED_ALPHA, alpha as usize, 0, 0) }
}

pub fn get_sched_alpha() -> isize {
    unsafe { syscall3(SYS_GET_SCHED_ALPHA, 0, 0, 0) }
}


#[repr(C)]
pub struct SchedProcStat {
    pub pid: i32,
    pub tickets: i32,
    pub effective_tickets: i32,
    pub ready_threads: i32,
    pub alpha: i32,
    pub run_ticks: usize,
    pub pass: usize,
    pub stride: usize,
}

impl SchedProcStat {
    pub const fn new() -> Self {
        SchedProcStat {
            pid: 0, tickets: 0, effective_tickets: 0, ready_threads: 0,
            alpha: 0, run_ticks: 0, pass: 0, stride: 0,
        }
    }
}

pub fn get_process_sched_stat(pid: i32, stat: &mut SchedProcStat) -> isize {
    unsafe {
        syscall3(
            SYS_GET_PROCESS_SCHED_STAT,
            pid as usize,
            stat as *mut SchedProcStat as usize,
            0,
        )
    }
}

pub fn reset_sched_stat() -> isize {
    unsafe { syscall3(SYS_RESET_SCHED_STAT, 0, 0, 0) }
}

pub fn get_ticks() -> usize {
    unsafe { syscall3(SYS_GET_TICKS, 0, 0, 0) as usize }
}
