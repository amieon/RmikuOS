
mod fs;
mod process;
mod thread;
mod arch;
mod net;

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
pub const SYSCALL_GET_PROCESS_SCHED_STAT: usize = 29;
pub const SYSCALL_RESET_SCHED_STAT: usize = 30;
pub const SYSCALL_GET_TICKS: usize = 31;
pub const SYSCALL_PIPE: usize = 32;
pub const SYSCALL_DUP2: usize = 33;
pub const SYSCALL_MKDIR: usize = 34;
pub const SYSCALL_CREATE: usize = 35;
pub const SYSCALL_UNLINK: usize = 36;
pub const SYSCALL_RMDIR: usize = 37;
pub const SYSCALL_REMOVE_RECURSIVE: usize = 38;
pub const SYSCALL_SHUTDOWN: usize = 39;
pub const SYSCALL_KILL: usize = 40;
pub const SYSCALL_FCNTL: usize = 41;
pub const SYSCALL_GET_TIME: usize = 42;
pub const SYSCALL_HARTID: usize = 43;
pub const SYSCALL_GETPPID: usize = 44;



pub const SYSCALL_NET_SOCKET: usize = 100; 
pub const SYSCALL_NET_BIND: usize = 101;
pub const SYSCALL_NET_SENDTO: usize = 102;
pub const SYSCALL_NET_RECVFROM: usize = 103; 
pub const SYSCALL_NET_CLOSE: usize = 104;
pub const SYSCALL_NET_CONNECT: usize = 105;
pub const SYSCALL_NET_LISTEN: usize = 106;
pub const SYSCALL_NET_ACCEPT: usize = 107;
pub const SYSCALL_NET_SEND: usize = 108;
pub const SYSCALL_NET_RECV: usize = 109;
pub const SYSCALL_NET_SET_IP: usize = 110;
pub const SYSCALL_NET_GET_IP: usize = 111;

use core::{sync::atomic::{AtomicUsize, Ordering}};

const NO_HART: usize = usize::MAX;

static BKL_OWNER: AtomicUsize = AtomicUsize::new(NO_HART);

#[inline]
fn hartid() -> usize {
    crate::task::current_hart_id()
}




pub fn syscall(id: usize, args: [usize; 6]) -> isize {
    match id {
        SYSCALL_EXIT => process::sys_exit(args[0] as i32),
        SYSCALL_YIELD => process::sys_yield(),
        SYSCALL_WRITE => fs::sys_write(args[0], args[1], args[2]),
        SYSCALL_GETPID => process::sys_getpid(),
        SYSCALL_FORK => process::sys_fork(),
        SYSCALL_WAITPID => process::sys_waitpid(args[0] as isize, args[1], args[2]),
        SYSCALL_SLEEP => process::sys_sleep(args[0]),
        SYSCALL_EXEC => process::sys_exec(args[0], args[1], args[2]),
        SYSCALL_READ => fs::sys_read(args[0], args[1], args[2]),
        SYSCALL_OPEN => fs::sys_open(args[0], args[1], args[2]),
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
        SYSCALL_GET_PROCESS_SCHED_STAT => process::sys_get_process_sched_stat(args[0], args[1]),
        SYSCALL_RESET_SCHED_STAT => process::sys_reset_sched_stat(),
        SYSCALL_GET_TICKS => process::sys_get_ticks(),
        SYSCALL_PIPE => fs::sys_pipe(args[0]),
        SYSCALL_DUP2 => fs::sys_dup2(args[0],args[1]),
        SYSCALL_MKDIR => fs::sys_mkdir(args[0],args[1]),
        SYSCALL_CREATE => fs::sys_create(args[0],args[1]),
        SYSCALL_UNLINK => fs::sys_unlink(args[0],args[1]),
        SYSCALL_RMDIR => fs::sys_rmdir(args[0],args[1]),
        SYSCALL_REMOVE_RECURSIVE => fs::sys_remove_recursive(args[0],args[1]),
        SYSCALL_SHUTDOWN => arch::shutdown(),
        SYSCALL_KILL => process::sys_kill(args[0],args[1]),
        SYSCALL_FCNTL => process::sys_fcntl(args[0], args[1], args[2]),
        SYSCALL_GET_TIME => arch::sys_arch_time(),
        SYSCALL_HARTID => arch::sys_hartid(),
        SYSCALL_GETPPID => process::sys_getppid(),

        SYSCALL_NET_SOCKET => net::sys_net_socket(args[0],args[1]),
        SYSCALL_NET_BIND => net::sys_net_bind(args[0], args[1]),
        SYSCALL_NET_SENDTO => net::sys_net_sendto(args[0], args[1], args[2], args[3], args[4]),
        SYSCALL_NET_RECVFROM => net::sys_net_recvfrom(args[0], args[1], args[2], args[3]),
        SYSCALL_NET_CLOSE => net::sys_net_close(args[0]),
        SYSCALL_NET_CONNECT => net::sys_net_connect(args[0],args[1],args[2]),
        SYSCALL_NET_LISTEN => net::sys_net_listen(args[0], args[1]),
        SYSCALL_NET_ACCEPT => net::sys_net_accept(args[0], args[1]),
        SYSCALL_NET_SEND => net::sys_net_send(args[0], args[1], args[2]),
        SYSCALL_NET_RECV => net::sys_net_recv(args[0],args[1],args[2]),
        SYSCALL_NET_SET_IP => net::sys_net_set_ip(args[0]),
        SYSCALL_NET_GET_IP => net::sys_net_get_ip(),

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