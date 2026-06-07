use crate::task::{
    thread_create_current,
    thread_exit_current,
    thread_join_current,
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

pub fn sys_set_thread_tickets(tid : usize, tickets: usize) -> isize {
    crate::task::set_thread_tickets_current(tid, tickets)
}