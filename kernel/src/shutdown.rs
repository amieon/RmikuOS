pub const SIFIVE_TEST_BASE:usize = 0x100000;
pub fn shutdown() -> ! {
    
    let virt = crate::mm::kernel_phys_to_virt(SIFIVE_TEST_BASE);
    unsafe {
        // 写 0x5555 = 正常关机(FINISHER_PASS)
        core::ptr::write_volatile(virt as *mut u32, 0x5555);
    }
    loop {}
}