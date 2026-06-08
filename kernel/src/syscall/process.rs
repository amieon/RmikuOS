pub fn sys_exit(exit_code: i32) -> ! {
    crate::task::exit_current_and_run_next(exit_code);
}

pub fn sys_yield() -> isize {
    crate::task::suspend_current_and_run_next()
}

pub fn sys_getpid() -> isize {
    crate::task::current_task_id() as isize
}

pub fn sys_sleep(ticks: usize) -> isize {
    crate::task::sleep_current_and_run_next(ticks)
}

pub fn sys_waitpid(pid: isize, exit_code_ptr: usize) -> isize {
    crate::task::waitpid_current(pid, exit_code_ptr)
}

pub fn sys_fork() -> isize {
    crate::task::fork_current()
}

pub fn sys_exec(path_ptr: usize, path_len: usize, args_ptr: usize) -> isize {
    crate::task::exec_current(path_ptr, path_len, args_ptr)
}

pub fn sys_mmap(len: usize, prot: usize) -> isize {
    crate::task::mmap_current(len, prot)
}


pub fn sys_munmap(addr: usize, len: usize) -> isize {
    crate::task::munmap_current(addr, len)
}

pub fn sys_set_process_tickets(tid : usize, tickets: usize) -> isize {
    crate::task::set_process_tickets_current(tid, tickets)
}

pub fn sys_set_my_tickets(tickets: usize) -> isize {
    crate::task::set_my_tickets_current(tickets)
}

pub fn sys_get_process_tickets(pid : usize) -> isize {
    crate::task::get_process_tickets_current(pid)
}

pub fn sys_get_my_tickets() -> isize {
    crate::task::get_my_tickets_current()
}

pub fn sys_set_sched_alpha(alpha: usize) -> isize {
    crate::task::set_sched_alpha_current(alpha.try_into().unwrap())
}

pub fn sys_get_sched_alpha() -> isize {
    crate::task::get_sched_alpha_current()
}

pub fn sys_get_process_sched_stat(pid: usize, stat_ptr: usize) -> isize {
    crate::task::get_process_sched_stat(pid, stat_ptr)
}

pub fn sys_reset_sched_stat() -> isize {
    crate::task::reset_sched_stat()
}

pub fn sys_get_ticks() -> isize {
    crate::timer::sys_get_ticks()
}

