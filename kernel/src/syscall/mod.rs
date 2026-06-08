mod fs;
mod process;
mod thread;

pub const SYSCALL_EXIT: usize = 0;
pub const SYSCALL_YIELD: usize = 1;
pub const SYSCALL_WRITE: usize = 2;
pub const SYSCALL_GETPID: usize = 3;
pub const SYSCALL_FORK: usize = 4;
pub const SYSCALL_WAITPID: usize = 5;
pub const SYSCALL_SLEEP: usize = 6;
pub const SYSCALL_EXEC: usize = 7;
pub const SYSCALL_READ: usize = 8;
pub const SYSCALL_OPEN: usize = 9;
pub const SYSCALL_CLOSE: usize = 10;
pub const SYSCALL_GETDENTS: usize = 11;
pub const SYSCALL_CHDIR: usize = 12;
pub const SYSCALL_GETCWD: usize = 13;
pub const SYSCALL_STAT: usize = 14;
pub const SYSCALL_FSTAT: usize = 15;
pub const SYSCALL_THREAD_CREATE: usize = 16;
pub const SYSCALL_THREAD_EXIT: usize = 17;
pub const SYSCALL_THREAD_JOIN: usize = 18;
pub const SYSCALL_MMAP: usize = 19;
pub const SYSCALL_MUNMAP: usize = 20;
pub const SYSCALL_SET_THREAD_TICKETS: usize = 21;
pub const SYSCALL_SET_PROCESS_TICKETS: usize = 22;
pub const SYSCALL_SET_MY_TICKETS: usize = 23;
pub const SYSCALL_GET_THREAD_TICKETS: usize = 24;
pub const SYSCALL_GET_PROCESS_TICKETS: usize = 25;
pub const SYSCALL_GET_MY_TICKETS: usize = 26;
pub const SYSCALL_SET_SCHED_ALPHA: usize = 27;
pub const SYSCALL_GET_SCHED_ALPHA: usize = 28;



pub fn syscall(id: usize, args: [usize; 6]) -> isize {
    match id {
        SYSCALL_EXIT => process::sys_exit(args[0] as i32),
        SYSCALL_YIELD => process::sys_yield(),
        SYSCALL_WRITE => fs::sys_write(args[0], args[1], args[2]),
        SYSCALL_GETPID => process::sys_getpid(),
        SYSCALL_FORK => process::sys_fork(),
        SYSCALL_WAITPID => process::sys_waitpid(args[0] as isize, args[1]),
        SYSCALL_SLEEP => process::sys_sleep(args[0]),
        SYSCALL_EXEC => process::sys_exec(args[0], args[1], args[2]),
        SYSCALL_READ => fs::sys_read(args[0], args[1], args[2]),
        SYSCALL_OPEN => fs::sys_open(args[0], args[1]),
        SYSCALL_CLOSE => fs::sys_close(args[0]),
        SYSCALL_GETDENTS => fs::sys_getdents(args[0], args[1], args[2]),
        SYSCALL_CHDIR => fs::sys_chdir(args[0], args[1]),
        SYSCALL_GETCWD => fs::sys_getcwd(args[0], args[1]),
        SYSCALL_STAT => fs::sys_stat(args[0], args[1], args[2]),
        SYSCALL_FSTAT => fs::sys_fstat(args[0], args[1]),
        SYSCALL_THREAD_CREATE => thread::sys_thread_create(args[0], args[1], args[2], args[3]),
        SYSCALL_THREAD_EXIT => thread::sys_thread_exit(args[0] as i32),
        SYSCALL_THREAD_JOIN => thread::sys_sthread_join(args[0], args[1]),
        SYSCALL_MMAP => process::sys_mmap(args[0],args[1]),
        SYSCALL_MUNMAP => process::sys_munmap(args[0], args[1]),
        SYSCALL_SET_THREAD_TICKETS => thread::sys_set_thread_tickets(args[0], args[1]),
        SYSCALL_SET_PROCESS_TICKETS => process::sys_set_process_tickets(args[0], args[1]),
        SYSCALL_SET_MY_TICKETS => process::sys_set_my_tickets(args[0]),
        SYSCALL_GET_THREAD_TICKETS => thread::sys_get_thread_tickets(args[0]),
        SYSCALL_GET_PROCESS_TICKETS => process::sys_get_process_tickets(args[0]),
        SYSCALL_GET_MY_TICKETS => process::sys_get_my_tickets(),
        SYSCALL_SET_SCHED_ALPHA => process::sys_set_sched_alpha(args[0]),
        SYSCALL_GET_SCHED_ALPHA => process::sys_get_sched_alpha(),
        _ => {
            log::warn!(
                "[syscall] unsupported syscall id={} args=[{:#x}, {:#x}, {:#x}]",
                id,
                args[0],
                args[1],
                args[2],
            );
            -38
        }
    }
}