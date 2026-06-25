#![no_std]
#![no_main]

use core::panic::PanicInfo;

const SYS_EXIT: usize = 0;
const SYS_WRITE: usize = 2;  

#[cfg(target_arch = "riscv64")]
unsafe fn syscall3(id: usize, a0: usize, a1: usize, a2: usize) -> isize {
    let ret: isize;
    core::arch::asm!(
        "ecall",
        in("a7") id,
        inlateout("a0") a0 => ret,
        in("a1") a1,
        in("a2") a2,
    );
    ret
}

#[cfg(target_arch = "loongarch64")]
pub unsafe fn syscall3(id: usize, a0: usize, a1: usize, a2: usize) -> isize {
    let ret: isize;
    core::arch::asm!(
        "syscall 0",
        in("$r11") id,
        inlateout("$r4") a0 => ret,
        in("$r5") a1,
        in("$r6") a2,
    );
    ret
}


fn sys_write(fd: usize, buf: &[u8]) -> isize {
    unsafe { syscall3(SYS_WRITE, fd, buf.as_ptr() as usize, buf.len()) }
}

fn sys_exit(code: usize) -> ! {
    unsafe { syscall3(SYS_EXIT, code, 0, 0); }
    loop {}
}

#[no_mangle]
#[link_section = ".text.entry"] 
pub extern "C" fn _start() -> ! {
    sys_write(1, b"hello from rust\n");
    sys_exit(0);
}

#[panic_handler]
fn panic(_: &PanicInfo) -> ! {
    sys_exit(1);
}