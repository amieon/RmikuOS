use core::panic::PanicInfo;

use crate::arch::shutdown;

// 链接脚本提供的内核 text 段边界（main.rs / mm/mod.rs 同款声明）
unsafe extern "C" {
    static _stext: u8;
    static _etext: u8;
}

fn print_backtrace() {
    const MAX_CAND: usize = 24;
    const WINDOW: usize = 3072;

    let (lo, hi) = unsafe {
        (
            &_stext as *const u8 as usize,
            &_etext as *const u8 as usize,
        )
    };

    #[cfg(target_arch = "riscv64")]
    fn current_sp() -> usize {
        let sp: usize;
        unsafe { core::arch::asm!("mv {}, sp", out(reg) sp, options(nostack, nomem)) };
        sp
    }
    #[cfg(target_arch = "loongarch64")]
    fn current_sp() -> usize {
        let sp: usize;
        unsafe { core::arch::asm!("move {}, $sp", out(reg) sp, options(nostack, nomem)) };
        sp
    }

    let base = current_sp();
    let mut found: [(usize, usize); MAX_CAND] = [(0, 0); MAX_CAND]; // (sp 偏移, 候选地址)
    let mut n = 0usize;
    let mut p = base;
    while p + core::mem::size_of::<usize>() <= base + WINDOW && n < MAX_CAND {
        let v = unsafe { core::ptr::read_volatile(p as *const usize) };
        if v >= lo && v < hi {
            found[n] = (p - base, v);
            n += 1;
        }
        p += core::mem::size_of::<usize>();
    }

    crate::println!(
        "[panic] hart={} sp={:#x} backtrace candidates={}",
        crate::arch::current_hart_id(),
        base,
        n
    );
    for i in 0..n {
        crate::println!("  [{:2}] {:#x}  (sp+{:#x})", i, found[i].1, found[i].0);
    }
    // 可直接粘贴的一行：把 <kernel-elf> 换成实际 ELF 路径即可
    crate::println!("[panic] addr2line -f -e <kernel-elf> \\");
    for i in 0..n {
        crate::print!("{:#x} ", found[i].1);
    }
    crate::println!();
}

#[panic_handler]
fn panic(info: &PanicInfo) -> ! {
    crate::println!();
    crate::println!("\x1b[31m KERNEL PANIC \x1b[0m");

    if let Some(location) = info.location() {
        crate::println!(
            "\x1b[31mPANIC at {}:{}:{}\x1b[0m",
            location.file(),
            location.line(),
            location.column()
        );
    } else {
        crate::println!("\x1b[31mPANIC at unknown location\x1b[0m");
    }

    crate::println!("\x1b[31m{}\x1b[0m", info.message());

    print_backtrace();

    crate::println!("\x1b[31mPANIC END\x1b[0m");

    shutdown();
    halt();
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
