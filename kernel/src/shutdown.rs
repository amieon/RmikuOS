#[cfg(target_arch = "riscv64")]
pub const SIFIVE_TEST_BASE: usize = 0x100000;

#[cfg(target_arch = "riscv64")]
pub fn shutdown() -> ! {
    let virt = crate::mm::kernel_phys_to_virt(SIFIVE_TEST_BASE);
    unsafe {
        core::ptr::write_volatile(virt as *mut u32, 0x5555);
    }
    loop {}
}


#[cfg(target_arch = "loongarch64")]
pub fn shutdown() -> ! {
    log::warn!("[shutdown] loongarch shutdown not implemented yet, use Ctrl+A X to exit QEMU");
    loop {
        core::hint::spin_loop();
    }
}