mod fs;
mod process;

pub const SYSCALL_EXIT: usize = 0;
pub const SYSCALL_YIELD: usize = 1;
pub const SYSCALL_WRITE: usize = 2;

pub fn syscall(id: usize, args: [usize; 3]) -> isize {
    match id {
        SYSCALL_EXIT => {
            process::sys_exit(args[0] as i32);
        }
        SYSCALL_YIELD => {
            process::sys_yield();
        }
        SYSCALL_WRITE => fs::sys_write(args[0], args[1], args[2]),
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