use core::arch;


pub fn shutdown() -> isize {
    crate::arch::shutdown();
}


pub fn sys_arch_time() -> isize {
    crate::timer::monotonic_time() as isize
}

pub fn sys_hartid() -> isize {
    crate::arch::hartid() as isize
}

