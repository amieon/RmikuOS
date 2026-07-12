
pub fn shutdown() -> isize {
    crate::arch::shutdown();
}

#[cfg(target_arch = "riscv64")]
pub fn sys_arch_time() -> isize {
    let time: usize;
    unsafe {
        core::arch::asm!("csrr {}, time", out(reg) time, options(nostack));
    }
    time as isize
}

#[cfg(target_arch = "loongarch64")]
pub fn sys_arch_time() -> isize {
    let time: usize;
    let _id: usize;

    unsafe {
        core::arch::asm!(
            "rdtime.d {time}, {id}",
            time = out(reg) time,
            id = out(reg) _id,
            options(nostack)
        );
    }

    time as isize
}


pub fn sys_hartid() -> isize {
    crate::arch::hartid() as isize
}

