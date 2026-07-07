mod context;
mod kernel_stack;
mod manager;
mod processor;
mod switch;
mod thread;
mod process;
mod manager_wrapper;
mod signal;

pub use context::TaskContext;
pub use kernel_stack::KernelStack;
pub use thread::{ThreadControlBlock, ThreadStatus};
pub use processor::{current_hart_id};
pub use signal::*;

pub const WNOHANG: usize = 1; 

pub type Pid = usize;
pub type Tid = usize;


pub use manager_wrapper::{
    init,
    run_first_task,
    run_tasks,

    exit_current_and_run_next,
    suspend_current_and_run_next,
    preempt_current_and_run_next,

    sleep_current_and_run_next,
    waitpid_current,
    fork_current,

    current_task_id,
    read_current_user_bytes,
    write_current_user_bytes,
    wake_sleeping_tasks,
    exec_current,

    current_file,
    alloc_fd_current,
    close_fd_current,
    get_fd_flags_current,
    set_fcntl,
    current_cwd,
    set_current_cwd,

    thread_create_current,
    thread_exit_current,
    thread_join_current,
    set_thread_tickets_current,
    set_process_tickets_current,
    set_my_tickets_current,
    get_thread_tickets_current,
    get_process_tickets_current,
    get_my_tickets_current,

    mmap_current,
    munmap_current,

    set_sched_alpha_current,
    get_sched_alpha_current,

    account_current_tick,
    get_process_sched_stat,
    reset_sched_stat,

    new_pipe,
    block_current_on_pipe_write,
    block_current_on_pipe_read,
    wake_pipe_readers,
    wake_pipe_writers,
    dup2,

    kill,
    do_signal,
    set_current_sig_pending,
    
};
