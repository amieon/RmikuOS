use core::arch::asm;
use core::sync::atomic::{AtomicUsize, Ordering};

use crate::arch::MAX_HARTS;

const NO_HART: usize = usize::MAX;

static GLOBAL_TICKS: AtomicUsize = AtomicUsize::new(0);
static TIMEKEEPER_HART: AtomicUsize = AtomicUsize::new(NO_HART);

static LOCAL_TICKS: [AtomicUsize; MAX_HARTS] =
    [const { AtomicUsize::new(0) }; MAX_HARTS];

const INTERVAL: usize = 500_000;
const TICKS_PER_SLICE: usize = 15;

pub fn init() {
    let hart = crate::arch::hartid();

    if hart >= MAX_HARTS {
        return;
    }

    let _ = TIMEKEEPER_HART.compare_exchange(
        NO_HART,
        hart,
        Ordering::AcqRel,
        Ordering::Acquire,
    );

    LOCAL_TICKS[hart].store(0, Ordering::Relaxed);

    set_next_timer();

    unsafe {
        // sie.STIE
        asm!("csrs sie, {}", in(reg) 1usize << 5, options(nostack));

        // sstatus.SIE
        asm!("csrs sstatus, {}", in(reg) 1usize << 1, options(nostack));
    }
}

pub fn read_arch_time() -> usize {
    let time: usize;
    unsafe {
        core::arch::asm!("csrr {}, time", out(reg) time, options(nostack));
    }
    time
}

/*
 * 返回 true 表示当前 hart 这次 tick 应该触发抢占调度。
 */
pub fn tick() -> bool {
    // RISC-V timer 是一次性的，第一时间设置下一次，避免中断风暴
    set_next_timer();

    let hart = crate::arch::hartid();

    if hart >= MAX_HARTS {
        return false;
    }

    let local = LOCAL_TICKS[hart].fetch_add(1, Ordering::Relaxed) + 1;

    if TIMEKEEPER_HART.load(Ordering::Acquire) == hart {
        GLOBAL_TICKS.fetch_add(1, Ordering::Relaxed);
    }

    local % TICKS_PER_SLICE == 0
}

pub fn ticks() -> usize {
    GLOBAL_TICKS.load(Ordering::Relaxed)
}

pub fn local_ticks(hart: usize) -> usize {
    if hart >= MAX_HARTS {
        return 0;
    }

    LOCAL_TICKS[hart].load(Ordering::Relaxed)
}

fn read_time() -> usize {
    let time: usize;
    unsafe {
        asm!("csrr {}, time", out(reg) time, options(nostack));
    }
    time
}

fn set_next_timer() {
    sbi_set_timer(read_time() + INTERVAL);
}

fn sbi_set_timer(stime_value: usize) {
    let _error: isize;
    let _value: usize;

    unsafe {
        asm!(
            "ecall",
            inlateout("a0") stime_value as isize => _error,
            lateout("a1") _value,
            in("a6") 0usize,
            in("a7") 0x54494D45usize,
            options(nostack),
        );
    }
}

pub fn dump_timer_masks(){}