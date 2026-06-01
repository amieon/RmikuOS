mod fs;
mod process;

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


pub fn syscall(id: usize, args: [usize; 3]) -> isize {
    match id {
        SYSCALL_EXIT => {
            process::sys_exit(args[0] as i32);
        }
        SYSCALL_YIELD => process::sys_yield(),
        SYSCALL_WRITE => fs::sys_write(args[0], args[1], args[2]),
        SYSCALL_GETPID => process::sys_getpid(),
        SYSCALL_FORK => process::sys_fork(),
        SYSCALL_WAITPID => process::sys_waitpid(args[0] as isize, args[1]),
        SYSCALL_SLEEP => process::sys_sleep(args[0]),
        SYSCALL_EXEC => process::sys_exec(args[0], args[1]),
        SYSCALL_READ => fs::sys_read(args[0], args[1], args[2]),
        SYSCALL_OPEN => fs::sys_open(args[0], args[1]),
        SYSCALL_CLOSE => fs::sys_close(args[0]),
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