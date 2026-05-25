// timer/loongarch64.rs

use core::arch::asm;
use core::sync::atomic::{AtomicUsize, Ordering};

static TICKS: AtomicUsize = AtomicUsize::new(0);

const TIMER_INITVAL: usize = 10_000_000;

pub fn init() {
    unsafe {
        // enable local timer interrupt: ECFG.LIE[11]
        asm!(
            "csrrd  $t0, 0x4",
            "ori    $t0, $t0, 0x800",
            "csrwr  $t0, 0x4",
            options(nostack)
        );

        // TCFG:
        // bit0 = En
        // bit1 = Periodic
        // bits[n-1:2] = InitVal
        let tcfg = (TIMER_INITVAL << 2) | 0b11;
        asm!("csrwr {0}, 0x41", in(reg) tcfg, options(nostack));

        // enable global interrupt: CRMD.IE
        asm!(
            "csrrd  $t0, 0x0",
            "ori    $t0, $t0, 0x4",
            "csrwr  $t0, 0x0",
            options(nostack)
        );
    }
}

pub fn tick() {
    unsafe {
        // clear timer interrupt: TICLR.CLR = 1
        asm!("csrwr {0}, 0x44", in(reg) 1usize, options(nostack));
    }

    let n = TICKS.fetch_add(1, Ordering::Relaxed) + 1;
    if n % 100 == 0 {
        crate::uart::puts_raw("[timer] tick\n");
    }
}