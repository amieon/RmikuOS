use crate::task::{
    thread_create_current,
    thread_exit_current,
    thread_join_current,
    thread_detach_current,
    futex_wait_current,
    futex_wake_current,
    tsd_get_current,
    tsd_set_current,
};


pub fn sys_thread_create (    
    entry: usize,
    arg0: usize,
    arg1: usize,
    user_stack_top: usize,
)-> isize{
    thread_create_current(entry,arg0,arg1,user_stack_top)
}


pub fn  sys_thread_exit(exit_code: i32) -> isize{
    thread_exit_current(exit_code)
}


pub fn sys_sthread_join(target_tid: crate::task::Tid, exit_code_ptr: usize) -> isize{
    thread_join_current(target_tid, exit_code_ptr)
}

pub fn sys_thread_detach(
    target_tid: crate::task::Tid,
    stack_base: usize,
    stack_size: usize,
) -> isize {
    thread_detach_current(target_tid, stack_base, stack_size)
}

pub fn sys_set_thread_tickets(tid : usize, tickets: usize) -> isize {
    crate::task::set_thread_tickets_current(tid, tickets)
}


pub fn sys_get_thread_tickets(tid : usize) -> isize {
    crate::task::get_thread_tickets_current(tid)
}


pub fn sys_futex_wait(uaddr: usize, val: usize) -> isize {
    futex_wait_current(uaddr, val as u32)
}

pub fn sys_futex_wake(uaddr: usize, max: usize) -> isize {
    futex_wake_current(uaddr, max)
}


pub fn sys_tsd_get(key: usize) -> isize {
    tsd_get_current(key)
}

pub fn sys_tsd_set(key: usize, value: usize) -> isize {
    tsd_set_current(key, value)
}
