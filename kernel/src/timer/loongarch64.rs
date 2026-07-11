use core::arch::asm;
use core::sync::atomic::{AtomicUsize, Ordering};

use crate::arch::MAX_HARTS;

static GLOBAL_TICKS: AtomicUsize = AtomicUsize::new(0);

static LOCAL_TICKS: [AtomicUsize; MAX_HARTS] =
    [const { AtomicUsize::new(0) }; MAX_HARTS];

// ===== timer mask debug =====

static TIMER_INIT_MASK: AtomicUsize = AtomicUsize::new(0);
static TIMER_IRQ_MASK: AtomicUsize = AtomicUsize::new(0);
static TIMER_USER_IRQ_MASK: AtomicUsize = AtomicUsize::new(0);
static TIMER_USER_SHOULD_MASK: AtomicUsize = AtomicUsize::new(0);
static TIMER_KERNEL_IRQ_MASK: AtomicUsize = AtomicUsize::new(0);

// ===== per-hart counters =====

static USER_IRQ_COUNT: [AtomicUsize; MAX_HARTS] =
    [const { AtomicUsize::new(0) }; MAX_HARTS];

static USER_SHOULD_COUNT: [AtomicUsize; MAX_HARTS] =
    [const { AtomicUsize::new(0) }; MAX_HARTS];

static KERNEL_IRQ_COUNT: [AtomicUsize; MAX_HARTS] =
    [const { AtomicUsize::new(0) }; MAX_HARTS];

static LAST_KERNEL_ERA: [AtomicUsize; MAX_HARTS] =
    [const { AtomicUsize::new(0) }; MAX_HARTS];

static LAST_KERNEL_TID: [AtomicUsize; MAX_HARTS] =
    [const { AtomicUsize::new(usize::MAX) }; MAX_HARTS];

static LAST_KSAVE0: [AtomicUsize; MAX_HARTS] =
    [const { AtomicUsize::new(0) }; MAX_HARTS];

static LAST_CRMD: [AtomicUsize; MAX_HARTS] =
    [const { AtomicUsize::new(0) }; MAX_HARTS];

static LAST_PRMD: [AtomicUsize; MAX_HARTS] =
    [const { AtomicUsize::new(0) }; MAX_HARTS];

// LoongArch 调试阶段先保守一点。
// 稳定后再调小 TIMER_INITVAL 或 TICKS_PER_SLICE。
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
    tick_with_context(false, 0, 0)
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

// ===== mark functions called by trap handler =====

pub fn mark_user_timer_irq() {
    let hart = crate::arch::hartid();

    if hart < MAX_HARTS {
        TIMER_USER_IRQ_MASK.fetch_or(1usize << hart, Ordering::Relaxed);
        USER_IRQ_COUNT[hart].fetch_add(1, Ordering::Relaxed);
    }
}

pub fn mark_user_should_schedule() {
    let hart = crate::arch::hartid();

    if hart < MAX_HARTS {
        TIMER_USER_SHOULD_MASK.fetch_or(1usize << hart, Ordering::Relaxed);
        USER_SHOULD_COUNT[hart].fetch_add(1, Ordering::Relaxed);
    }
}

pub fn mark_kernel_timer_irq(era: usize, prmd: usize) {
    let hart = crate::arch::hartid();

    if hart >= MAX_HARTS {
        return;
    }

    let current_tid = crate::task::current_tid_opt()
        .unwrap_or(usize::MAX);

    let ksave0 = read_ksave0();
    let crmd = read_crmd();

    TIMER_KERNEL_IRQ_MASK.fetch_or(1usize << hart, Ordering::Relaxed);
    KERNEL_IRQ_COUNT[hart].fetch_add(1, Ordering::Relaxed);

    LAST_KERNEL_ERA[hart].store(era, Ordering::Relaxed);
    LAST_KERNEL_TID[hart].store(current_tid, Ordering::Relaxed);
    LAST_KSAVE0[hart].store(ksave0, Ordering::Relaxed);
    LAST_CRMD[hart].store(crmd, Ordering::Relaxed);
    LAST_PRMD[hart].store(prmd, Ordering::Relaxed);
}

// ===== dump =====


pub fn tick_with_context(from_user: bool, era: usize, prmd: usize) -> bool {
    clear_timer_interrupt();

    let hart = crate::arch::hartid();

    if hart >= MAX_HARTS {
        return false;
    }

    TIMER_IRQ_MASK.fetch_or(1usize << hart, Ordering::Relaxed);

    let local = LOCAL_TICKS[hart].fetch_add(1, Ordering::Relaxed) + 1;

    let mut global_now = GLOBAL_TICKS.load(Ordering::Relaxed);

    if hart == 0 {
        global_now = GLOBAL_TICKS.fetch_add(1, Ordering::Relaxed) + 1;
        watchdog_after_tick(global_now);
    }
    

    let should_schedule = local % TICKS_PER_SLICE == 0;

    if from_user {
        TIMER_USER_IRQ_MASK.fetch_or(1usize << hart, Ordering::Relaxed);
        USER_IRQ_COUNT[hart].fetch_add(1, Ordering::Relaxed);

        if should_schedule {
            TIMER_USER_SHOULD_MASK.fetch_or(1usize << hart, Ordering::Relaxed);
            USER_SHOULD_COUNT[hart].fetch_add(1, Ordering::Relaxed);
        }
    } else {
        let current_tid = crate::task::current_tid_opt()
            .unwrap_or(usize::MAX);

        let ksave0 = read_ksave0();
        let crmd = read_crmd();

        TIMER_KERNEL_IRQ_MASK.fetch_or(1usize << hart, Ordering::Relaxed);
        KERNEL_IRQ_COUNT[hart].fetch_add(1, Ordering::Relaxed);

        LAST_KERNEL_ERA[hart].store(era, Ordering::Relaxed);
        LAST_KERNEL_TID[hart].store(current_tid, Ordering::Relaxed);
        LAST_KSAVE0[hart].store(ksave0, Ordering::Relaxed);
        LAST_CRMD[hart].store(crmd, Ordering::Relaxed);
        LAST_PRMD[hart].store(prmd, Ordering::Relaxed);
    }

    should_schedule
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
        log::warn!(
            "[timer-hart] hart={} local={} user_irq={} user_should={} kernel_irq={} last_era={:#x} last_tid={} ksave0={:#x} crmd={:#x} prmd={:#x}",
            hart,
            LOCAL_TICKS[hart].load(Ordering::Relaxed),
            USER_IRQ_COUNT[hart].load(Ordering::Relaxed),
            USER_SHOULD_COUNT[hart].load(Ordering::Relaxed),
            KERNEL_IRQ_COUNT[hart].load(Ordering::Relaxed),
            LAST_KERNEL_ERA[hart].load(Ordering::Relaxed),
            LAST_KERNEL_TID[hart].load(Ordering::Relaxed),
            LAST_KSAVE0[hart].load(Ordering::Relaxed),
            LAST_CRMD[hart].load(Ordering::Relaxed),
            LAST_PRMD[hart].load(Ordering::Relaxed),
        );
    }
}

// ===== CSR helpers =====

fn clear_timer_interrupt() {
    unsafe {
        // CSR.TICLR = 0x44, bit0 clears timer interrupt pending bit.
        asm!("csrwr {}, 0x44", in(reg) 1usize, options(nostack));
    }
}

fn read_ksave0() -> usize {
    let value: usize;

    unsafe {
        asm!("csrrd {}, 0x30", out(reg) value, options(nostack));
    }

    value
}

fn read_crmd() -> usize {
    let value: usize;

    unsafe {
        asm!("csrrd {}, 0x0", out(reg) value, options(nostack));
    }

    value
}

static LAST_WATCHDOG_TICK: AtomicUsize = AtomicUsize::new(0);

fn watchdog_after_tick(global: usize) {
    let last_switch = crate::task::last_switch_back_tick();

    // 超过 2000 global ticks 没有任何 switch-back，认为 scheduler 可能卡住。
    if global.wrapping_sub(last_switch) < 2000 {
        return;
    }

    let last_dump = LAST_WATCHDOG_TICK.load(Ordering::Relaxed);

    // 每 2000 ticks 最多打印一次，避免刷屏。
    if global.wrapping_sub(last_dump) < 2000 {
        return;
    }

    if LAST_WATCHDOG_TICK
        .compare_exchange(last_dump, global, Ordering::AcqRel, Ordering::Relaxed)
        .is_err()
    {
        return;
    }

    log::warn!(
        "[watchdog] global={} last_switch_back={}",
        global,
        last_switch,
    );

    dump_timer_masks();
    crate::task::dump_preempt_masks();
    crate::task::dump_task_manager_lock_state();
}