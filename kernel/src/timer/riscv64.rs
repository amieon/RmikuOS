// timer/riscv64.rs

use core::arch::asm;
use core::sync::atomic::{AtomicUsize, Ordering};

static TICKS: AtomicUsize = AtomicUsize::new(0);

const INTERVAL: usize = 10_000_000;

pub fn init() {
    set_next_timer();
    unsafe {
        // enable supervisor timer interrupt: sie.STIE
        asm!("csrs sie, {}", in(reg) 1usize << 5);
        // enable supervisor global interrupt: sstatus.SIE
        asm!("csrs sstatus, {}", in(reg) 1usize << 1);
    }
}

pub fn tick() {
    let n = TICKS.fetch_add(1, Ordering::Relaxed) + 1;
    if n % 100 == 0 {
        crate::uart::puts_raw("[timer] tick\n");
    }
    set_next_timer();
}

fn read_time() -> usize {
    let time: usize;
    unsafe {
        asm!("csrr {}, time", out(reg) time);
    }
    time
}

fn set_next_timer() {
    sbi_set_timer(read_time() + INTERVAL);
}

fn sbi_set_timer(stime_value: usize) {
    unsafe {
        asm!(
            "ecall",
            in("a0") stime_value,
            in("a6") 0usize,          // FID: set_timer
            in("a7") 0x54494D45usize, // EID: TIME
            lateout("a0") _,
            lateout("a1") _,
        );
    }
}