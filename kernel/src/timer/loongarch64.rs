use core::arch::asm;
use core::sync::atomic::{AtomicUsize, Ordering};

use crate::arch::MAX_HARTS;

static GLOBAL_TICKS: AtomicUsize = AtomicUsize::new(0);

static LOCAL_TICKS: [AtomicUsize; MAX_HARTS] =
    [const { AtomicUsize::new(0) }; MAX_HARTS];

// 调试 mask：不要在 timer 中断里持续 log，只记录 bit
static TIMER_INIT_MASK: AtomicUsize = AtomicUsize::new(0);
static TIMER_IRQ_MASK: AtomicUsize = AtomicUsize::new(0);
static TIMER_USER_IRQ_MASK: AtomicUsize = AtomicUsize::new(0);
static TIMER_USER_SHOULD_MASK: AtomicUsize = AtomicUsize::new(0);

// LoongArch 调试阶段先保守一点
const TIMER_INITVAL: usize = 1_000_000;
const TICKS_PER_SLICE: usize = 5;

pub fn init() {
    let hart = crate::arch::hartid();

    if hart >= MAX_HARTS {
        return;
    }

    TIMER_INIT_MASK.fetch_or(1usize << hart, Ordering::Release);
    LOCAL_TICKS[hart].store(0, Ordering::Relaxed);

    unsafe {
        clear_timer_interrupt();

        // ECFG.LIE[11]：开启本地 timer interrupt
        let mut ecfg: usize;
        asm!("csrrd {}, 0x4", out(reg) ecfg, options(nostack));
        ecfg |= 1usize << 11;
        asm!("csrwr {}, 0x4", in(reg) ecfg, options(nostack));

        // TCFG:
        // bit0 = En
        // bit1 = Periodic
        // bits[n-1:2] = InitVal
        let tcfg = (TIMER_INITVAL << 2) | 0b11;
        asm!("csrwr {}, 0x41", in(reg) tcfg, options(nostack));

        // CRMD.IE：全局中断开关
        let mut crmd: usize;
        asm!("csrrd {}, 0x0", out(reg) crmd, options(nostack));
        crmd |= 1usize << 2;
        asm!("csrwr {}, 0x0", in(reg) crmd, options(nostack));
    }
}

/// 返回 true 表示当前 hart 这次 tick 应该触发抢占调度。
pub fn tick() -> bool {
    clear_timer_interrupt();

    let hart = crate::arch::hartid();

    if hart >= MAX_HARTS {
        return false;
    }

    TIMER_IRQ_MASK.fetch_or(1usize << hart, Ordering::Relaxed);

    let local = LOCAL_TICKS[hart].fetch_add(1, Ordering::Relaxed) + 1;

    // 固定 hart0 做全局 timekeeper。
    // 这样 GLOBAL_TICKS 不会因为 8 核 timer 而涨 8 倍。
    if hart == 0 {
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

pub fn is_timekeeper_hart() -> bool {
    crate::arch::hartid() == 0
}

pub fn mark_user_timer_irq() {
    let hart = crate::arch::hartid();

    if hart < MAX_HARTS {
        TIMER_USER_IRQ_MASK.fetch_or(1usize << hart, Ordering::Relaxed);
    }
}

pub fn mark_user_should_schedule() {
    let hart = crate::arch::hartid();

    if hart < MAX_HARTS {
        TIMER_USER_SHOULD_MASK.fetch_or(1usize << hart, Ordering::Relaxed);
    }
}

pub fn dump_timer_masks() {
    log::warn!(
        "[timer-mask] init={:#x} irq={:#x} user_irq={:#x} user_should={:#x} kernel_irq={:#x} global={}",
        TIMER_INIT_MASK.load(Ordering::Acquire),
        TIMER_IRQ_MASK.load(Ordering::Acquire),
        TIMER_USER_IRQ_MASK.load(Ordering::Acquire),
        TIMER_USER_SHOULD_MASK.load(Ordering::Acquire),
        TIMER_KERNEL_IRQ_MASK.load(Ordering::Acquire),
        GLOBAL_TICKS.load(Ordering::Relaxed),
    );

    for hart in 0..MAX_HARTS {
        let kc = KERNEL_TIMER_COUNT[hart].load(Ordering::Relaxed);
        if kc != 0 {
            log::warn!(
                "[timer-kernel] hart={} count={} last_era={:#x} last_tid={}",
                hart,
                kc,
                LAST_KERNEL_ERA[hart].load(Ordering::Relaxed),
                LAST_KERNEL_TID[hart].load(Ordering::Relaxed),
            );
        }
    }
}

fn clear_timer_interrupt() {
    unsafe {
        // CSR.TICLR = 0x44, bit 0 clears timer interrupt pending bit.
        asm!("csrwr {}, 0x44", in(reg) 1usize, options(nostack));
    }
}

static TIMER_KERNEL_IRQ_MASK: AtomicUsize = AtomicUsize::new(0);

static KERNEL_TIMER_COUNT: [AtomicUsize; MAX_HARTS] =
    [const { AtomicUsize::new(0) }; MAX_HARTS];

static LAST_KERNEL_ERA: [AtomicUsize; MAX_HARTS] =
    [const { AtomicUsize::new(0) }; MAX_HARTS];

static LAST_KERNEL_TID: [AtomicUsize; MAX_HARTS] =
    [const { AtomicUsize::new(usize::MAX) }; MAX_HARTS];

    pub fn mark_kernel_timer_irq(era: usize) {
    let hart = crate::arch::hartid();

    if hart >= MAX_HARTS {
        return;
    }

    TIMER_KERNEL_IRQ_MASK.fetch_or(1usize << hart, Ordering::Relaxed);
    KERNEL_TIMER_COUNT[hart].fetch_add(1, Ordering::Relaxed);
    LAST_KERNEL_ERA[hart].store(era, Ordering::Relaxed);

    let tid = crate::task::current_tid_opt().unwrap_or(usize::MAX);
    LAST_KERNEL_TID[hart].store(tid, Ordering::Relaxed);
}