use core::arch::asm;
use core::sync::atomic::{AtomicUsize, Ordering};

use crate::arch::MAX_HARTS;

const NO_HART: usize = usize::MAX;

static GLOBAL_TICKS: AtomicUsize = AtomicUsize::new(0);
static TIMEKEEPER_HART: AtomicUsize = AtomicUsize::new(NO_HART);

static LOCAL_TICKS: [AtomicUsize; MAX_HARTS] =
    [const { AtomicUsize::new(0) }; MAX_HARTS];
    
const TIMER_INITVAL: usize = 500_000;
const TICKS_PER_SLICE: usize = 3;

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

    unsafe {
        clear_timer_interrupt();

        /*
         * enable local timer interrupt: ECFG.LIE[11]
         */
        asm!(
            "csrrd  $t0, 0x4",
            "ori    $t0, $t0, 0x800",
            "csrwr  $t0, 0x4",
            options(nostack)
        );

        /*
         * TCFG:
         *   bit0 = En
         *   bit1 = Periodic
         *   bits[n-1:2] = InitVal
         */
        let tcfg = (TIMER_INITVAL << 2) | 0b11;
        asm!("csrwr {0}, 0x41", in(reg) tcfg, options(nostack));

        /*
         * enable global interrupt: CRMD.IE
         */
        asm!(
            "csrrd  $t0, 0x0",
            "ori    $t0, $t0, 0x4",
            "csrwr  $t0, 0x0",
            options(nostack)
        );
    }
}

/*
 * 返回 true 表示当前 hart 这次 tick 应该触发抢占调度。
 */
pub fn tick() -> bool {
    clear_timer_interrupt();

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

fn clear_timer_interrupt() {
    unsafe {
        // CSR.TICLR = 0x44, bit 0 clears timer interrupt pending bit.
        asm!("csrwr {0}, 0x44", in(reg) 1usize, options(nostack));
    }
}