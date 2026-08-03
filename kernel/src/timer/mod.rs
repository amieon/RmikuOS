// kernel/src/timer/mod.rs

#[cfg(target_arch = "riscv64")]
#[path = "riscv64.rs"]
mod arch_timer;

#[cfg(target_arch = "loongarch64")]
#[path = "loongarch64.rs"]
mod arch_timer;

pub use arch_timer::*;
pub use ticks;

use core::sync::atomic::{AtomicIsize, AtomicU64, AtomicUsize, Ordering};

/// 硬件时间寄存器频率(Hz)。QEMU virt 默认 timebase-frequency = 10MHz。
pub const TIME_FREQ: usize = 10_000_000;

static TIME_OFFSET: [AtomicIsize; crate::arch::MAX_HARTS] =
    [const { AtomicIsize::new(0) }; crate::arch::MAX_HARTS];

static MONO_TIME: AtomicUsize = AtomicUsize::new(0);

/// 单调时间(微秒, 自启动起)。多核经 TIME_OFFSET 校准 + 单调钳制。
pub fn monotonic_us() -> u64 {
    (monotonic_time() as u64) * 1_000_000 / (TIME_FREQ as u64)
}

/* ---- 墙钟(网络同步时间, 见 ntpdate) ----
 * WALL_BASE: 校准时刻的绝对 epoch 微秒; WALL_TICKS: 校准时刻的单调微秒。
 * 未校准(WALL_BASE==0)时 now_secs() 返回 0。 */
static WALL_BASE: AtomicU64 = AtomicU64::new(0);
static WALL_TICKS: AtomicU64 = AtomicU64::new(0);

/// 校准墙钟: epoch_us 是"此刻"的绝对 Unix 时间(微秒)。
pub fn set_wall_clock(epoch_us: u64) {
    WALL_BASE.store(epoch_us, Ordering::Relaxed);
    WALL_TICKS.store(monotonic_us(), Ordering::Relaxed);
}

/// 当前墙钟秒(epoch)。未校准返回 0。
/// 注: Stat 时间戳(方案 C)即将使用; 在此之前仅 ntpdate 校准, 无调用点。
pub fn now_secs() -> u64 {
    let base = WALL_BASE.load(Ordering::Relaxed);
    if base == 0 {
        return 0;
    }
    let t0 = WALL_TICKS.load(Ordering::Relaxed);
    let us = monotonic_us();
    (base + us.saturating_sub(t0)) / 1_000_000
}

pub fn monotonic_time() -> usize {
    let hart = crate::arch::hartid();
    let raw = read_arch_time();

    let adjusted = if hart < crate::arch::MAX_HARTS {
        let off = TIME_OFFSET[hart].load(Ordering::Acquire);
        raw.wrapping_add(off as usize)
    } else {
        raw
    };

    monotonic_clamp(adjusted)
}

fn monotonic_clamp(t: usize) -> usize {
    let mut old = MONO_TIME.load(Ordering::Acquire);

    loop {
        let new = if t > old { t } else { old + 1 };

        match MONO_TIME.compare_exchange(
            old,
            new,
            Ordering::AcqRel,
            Ordering::Acquire,
        ) {
            Ok(_) => return new,
            Err(v) => old = v,
        }
    }
}
pub fn calibrate_current_hart_time() {
    let hart = crate::arch::hartid();

    if hart >= crate::arch::MAX_HARTS {
        return;
    }

    if hart == 0 {
        TIME_OFFSET[0].store(0, Ordering::Release);
        return;
    }

    let master_now = MONO_TIME.load(Ordering::Acquire);
    let local_raw = read_arch_time();

    let offset = master_now as isize - local_raw as isize;
    TIME_OFFSET[hart].store(offset, Ordering::Release);
}