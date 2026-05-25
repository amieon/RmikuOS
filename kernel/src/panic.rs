use core::panic::PanicInfo;

#[panic_handler]
fn panic(info: &PanicInfo) -> ! {
    println!();
    println!("\x1b[31m KERNEL PANIC \x1b[0m");

    if let Some(location) = info.location() {
        println!(
            "\x1b[31mPANIC at {}:{}:{}\x1b[0m",
            location.file(),
            location.line(),
            location.column()
        );
    } else {
        println!("\x1b[31mPANIC at unknown location\x1b[0m");
    }

    println!("\x1b[31m{}\x1b[0m", info.message());
    println!("\x1b[31mPANIC END\x1b[0m");

    halt()
}

#[inline(always)]
fn halt() -> ! {
    unsafe {
        #[cfg(target_arch = "riscv64")]
        {
            core::arch::asm!("csrci sstatus, 2");
            loop {
                core::arch::asm!("wfi");
            }
        }

        #[cfg(target_arch = "loongarch64")]
        {
            // Clear CRMD.IE，关全局中断
            core::arch::asm!(
                "csrrd  $t0, 0x0",
                "li.d   $t1, -5",
                "and    $t0, $t0, $t1",
                "csrwr  $t0, 0x0",
                options(nostack)
            );

            loop {
                core::arch::asm!("idle 0");
            }
        }

        #[allow(unreachable_code)]
        loop {
            core::hint::spin_loop();
        }
    }
}