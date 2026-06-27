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
pub const GED_SLEEP_CTL_ADDR: usize = 0x100e001c; 

#[cfg(target_arch = "loongarch64")]
pub fn shutdown() -> ! {
    let virt = crate::mm::kernel_phys_to_virt(GED_SLEEP_CTL_ADDR);
    unsafe {
        // (S5=5 << 2) | (SLP_EN=1 << 5) = 0x34,8 位写
        core::ptr::write_volatile(virt as *mut u8, 0x34);
    }
    loop { core::hint::spin_loop(); }
}