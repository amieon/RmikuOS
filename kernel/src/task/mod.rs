mod context;
mod kernel_stack;
mod manager;
mod processor;
mod switch;
mod thread;
mod process;
mod manager_wrapper;
mod signal;
mod math;

pub use context::TaskContext;
pub use kernel_stack::KernelStack;
pub use thread::{ThreadControlBlock, ThreadStatus};
pub use processor::*;
pub use signal::*;

pub const WNOHANG: usize = 1;
/// 也报告"被停止"的子进程（作业控制）。必须是 2 —— 写成 0 会让
/// `waitpid(pid, &st, WUNTRACED)` 静默退化成普通 waitpid，父进程永远
/// 等不到 Ctrl+Z（这是"编译过、语义错"的典型）。
pub const WUNTRACED: usize = 2;
/// 也报告"被 SIGCONT 继续"的子进程。
pub const WCONTINUED: usize = 8;

pub type Pid = usize;
pub type Tid = usize;

pub use process::NGROUPS_MAX;

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
    current_task_ppid,
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
    mprotect_current,

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
    dup,

    kill,
    tgkill,
    do_signal,
    sigreturn_current,
    set_current_sig_pending,
    sig_set,
    sigprocmask,
    sigpending,
    signal_pending_now,
    take_pending_stop_signal,
    stop_current_process,
    exit_with_signal,
    set_front,
    front_pgid,
    kill_front_sigint,
    kill_front_tstp,
    pgid_of,
    set_pgid,
    set_itimer,
    get_itimer,

    env_get_current_value,
    env_unset_current_value,
    env_set_current_value,
    env_clear_current_values,
    env_get_all_current,

    current_creds,
    set_current_creds,
    current_groups,
    set_current_groups,

    current_umask,
    set_current_umask,
};
