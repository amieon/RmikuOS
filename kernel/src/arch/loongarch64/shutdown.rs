
pub const GED_SLEEP_CTL_ADDR: usize = 0x100e001c; 
pub fn shutdown() -> ! {
    let virt = crate::mm::kernel_phys_to_virt(GED_SLEEP_CTL_ADDR);
    unsafe {
        // (S5=5 << 2) | (SLP_EN=1 << 5) = 0x34,8 位写
        core::ptr::write_volatile(virt as *mut u8, 0x34);
    }
    loop { core::hint::spin_loop(); }
}