pub fn sys_exit(exit_code: i32) -> ! {
    crate::task::exit_current_and_run_next(exit_code);
}

pub fn sys_yield() -> isize {
    crate::task::suspend_current_and_run_next()
}