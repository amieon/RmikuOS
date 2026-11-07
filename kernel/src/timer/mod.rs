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

/// 硬件时间寄存器频率(Hz)——**必须按架构区分**。
///
/// - riscv64: QEMU virt 的 timebase-frequency = 10MHz（time CSR 的计数速率）
/// - loongarch64: LA464 的 CPUCFG4/crystal frequency = 100MHz（rdtime.d 的计数速率）
///
/// 这两个数完全不同，共用一个值会让其中一个架构的时间**快或慢 10 倍**。
/// 之前写死 10MHz，于是龙芯上 monotonic_us / now_secs / DNS TTL / uptime
/// 全部走快 10 倍——只是没人拿秒表量过，所以一直没暴露。
#[cfg(target_arch = "riscv64")]
pub const TIME_FREQ: usize = 10_000_000;
#[cfg(target_arch = "loongarch64")]
pub const TIME_FREQ: usize = 100_000_000;

static TIME_OFFSET: [AtomicIsize; crate::arch::MAX_HARTS] =
    [const { AtomicIsize::new(0) }; crate::arch::MAX_HARTS];

static MONO_TIME: AtomicUsize = AtomicUsize::new(0);

/// 单调时间(微秒, 自启动起)。多核经 TIME_OFFSET 校准 + 单调钳制。
pub fn monotonic_us() -> u64 {
    (monotonic_time() as u64) * 1_000_000 / (TIME_FREQ as u64)
}

/* 墙钟(epoch 微秒)
 * 开机按顺序有三条来源，后者成功会覆盖前者：
 * drivers::rtc::init() 读硬件 RTC（QEMU 有；真机 riscv 板上没有这段 MMIO）
 * drivers::rtc::stamp::restore() 读 /var/lib/rtcstamp（fake-hwclock 兜底）
 * login 后台跑 `ntpdate -b`，成功还会回写 stamp
 * WALL_BASE: 校准时刻的 epoch 微秒; WALL_TICKS: 校准时刻的 monotonic_us。
 * 三条都没成(WALL_BASE==0)时 now_us()/now_secs() 返回 0。 */
static WALL_BASE: AtomicU64 = AtomicU64::new(0);
static WALL_TICKS: AtomicU64 = AtomicU64::new(0);

/// 校准墙钟: epoch_us 是"此刻"的绝对 Unix 时间(微秒)。
pub fn set_wall_clock(epoch_us: u64) {
    WALL_BASE.store(epoch_us, Ordering::Relaxed);
    WALL_TICKS.store(monotonic_us(), Ordering::Relaxed);
}

/// 当前墙钟时间戳(epoch 微秒)。未校准返回 0。
///
/// 一次读齐 BASE/TICKS/monotonic 后返回总微秒：用户态拿这个数自己拆秒与
/// 小数，秒边界不会错位。若拆成"get_epoch() 的秒 + get_time_us() 的小数"，
/// 两个源相位不同，拼出来会在整秒处跳 ±1s。
pub fn now_us() -> u64 {
    let base = WALL_BASE.load(Ordering::Relaxed);
    if base == 0 {
        return 0;
    }
    let t0 = WALL_TICKS.load(Ordering::Relaxed);
    let us = monotonic_us();
    base.saturating_add(us.saturating_sub(t0))
}

/// 当前墙钟秒(epoch)。未校准返回 0。stat / mtime 走这里。
pub fn now_secs() -> u64 {
    now_us() / 1_000_000
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