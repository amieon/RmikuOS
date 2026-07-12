use core::arch::asm;
use core::sync::atomic::{AtomicUsize, Ordering};

use crate::arch::MAX_HARTS;

static GLOBAL_TICKS: AtomicUsize = AtomicUsize::new(0);

static LOCAL_TICKS: [AtomicUsize; MAX_HARTS] =
    [const { AtomicUsize::new(0) }; MAX_HARTS];


const TIMER_INITVAL: usize = 500_000;
const TICKS_PER_SLICE: usize = 15;

pub fn init() {
    let hart = crate::arch::hartid();

    if hart >= MAX_HARTS {
        return;
    }

    LOCAL_TICKS[hart].store(0, Ordering::Relaxed);
    debug_mark_init(hart);

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

pub fn read_arch_time() -> usize {
    let time: usize;
    let _id: usize;

    unsafe {
        core::arch::asm!(
            "rdtime.d {time}, {id}",
            time = out(reg) time,
            id = out(reg) _id,
            options(nostack)
        );
    }

    time
}

/// 兼容旧接口。没有 trap context 的调用统一当作 kernel tick。
pub fn tick() -> bool {
    tick_with_context(false, 0, 0)
}

/// LoongArch trap handler 推荐调用这个。
///
/// `from_user` 只用于 debug 统计；生产路径下会被编译成空操作。
pub fn tick_with_context(from_user: bool, era: usize, prmd: usize) -> bool {
    clear_timer_interrupt();

    let hart = crate::arch::hartid();

    if hart >= MAX_HARTS {
        return false;
    }

    let local = LOCAL_TICKS[hart].fetch_add(1, Ordering::Relaxed) + 1;

    // 固定 hart0 作为全局 timekeeper。
    // 否则 8 个 hart 都开 timer 后，GLOBAL_TICKS 会涨 8 倍。
    if hart == 0 {
        GLOBAL_TICKS.fetch_add(1, Ordering::Relaxed);
    }

    let should_schedule = local % TICKS_PER_SLICE == 0;

    debug_mark_tick(hart, from_user, should_schedule, era, prmd);

    should_schedule
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

fn clear_timer_interrupt() {
    unsafe {
        // CSR.TICLR = 0x44, bit0 clears timer interrupt pending bit.
        asm!("csrwr {}, 0x44", in(reg) 1usize, options(nostack));
    }
}

// ======================================================================
// Debug section
// ======================================================================
//
// 默认完全无效。只有打开 feature = "timer-debug" 时才统计。
// 性能测试时不要打开 timer-debug。
// ======================================================================

#[cfg(feature = "timer-debug")]
mod debug {
    use super::*;
    use core::sync::atomic::{AtomicUsize, Ordering};

    static TIMER_INIT_MASK: AtomicUsize = AtomicUsize::new(0);
    static TIMER_IRQ_MASK: AtomicUsize = AtomicUsize::new(0);
    static TIMER_USER_IRQ_MASK: AtomicUsize = AtomicUsize::new(0);
    static TIMER_USER_SHOULD_MASK: AtomicUsize = AtomicUsize::new(0);
    static TIMER_KERNEL_IRQ_MASK: AtomicUsize = AtomicUsize::new(0);

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

    pub fn mark_init(hart: usize) {
        TIMER_INIT_MASK.fetch_or(1usize << hart, Ordering::Release);
    }

    pub fn mark_tick(hart: usize, from_user: bool, should_schedule: bool, era: usize, prmd: usize) {
        TIMER_IRQ_MASK.fetch_or(1usize << hart, Ordering::Relaxed);

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

            TIMER_KERNEL_IRQ_MASK.fetch_or(1usize << hart, Ordering::Relaxed);
            KERNEL_IRQ_COUNT[hart].fetch_add(1, Ordering::Relaxed);

            LAST_KERNEL_ERA[hart].store(era, Ordering::Relaxed);
            LAST_KERNEL_TID[hart].store(current_tid, Ordering::Relaxed);
            LAST_KSAVE0[hart].store(read_ksave0(), Ordering::Relaxed);
            LAST_CRMD[hart].store(read_crmd(), Ordering::Relaxed);
            LAST_PRMD[hart].store(prmd, Ordering::Relaxed);
        }
    }

    pub fn dump() {
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
}

#[cfg(feature = "timer-debug")]
#[inline]
fn debug_mark_init(hart: usize) {
    debug::mark_init(hart);
}

#[cfg(not(feature = "timer-debug"))]
#[inline]
fn debug_mark_init(_hart: usize) {}

#[cfg(feature = "timer-debug")]
#[inline]
fn debug_mark_tick(hart: usize, from_user: bool, should_schedule: bool, era: usize, prmd: usize) {
    debug::mark_tick(hart, from_user, should_schedule, era, prmd);
}

#[cfg(not(feature = "timer-debug"))]
#[inline]
fn debug_mark_tick(
    _hart: usize,
    _from_user: bool,
    _should_schedule: bool,
    _era: usize,
    _prmd: usize,
) {}

#[cfg(feature = "timer-debug")]
pub fn dump_timer_masks() {
    debug::dump();
}

#[cfg(not(feature = "timer-debug"))]
pub fn dump_timer_masks() {
    log::warn!("[timer-debug] disabled");
}