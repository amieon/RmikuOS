//! 两个平台的单位不一样
//! | 平台 | 设备 | 单位 |
//! | riscv64 | goldfish-rtc | **纳秒** since epoch |
//! | loongarch64 | LS7A RTC | **秒**（1Hz TOY 计数器） |
//!


use core::ptr::read_volatile;

pub mod stamp;

/// 合理下界：2000-01-01（写错 RTC 或寄存器布局不对时几乎必然落到界外）
pub(crate) const MIN_UNIX: u64 = 946_684_800;
/// 合理上界：2100-01-01
pub(crate) const MAX_UNIX: u64 = 4_102_444_800;

/// 这个秒数像不像一个真实时间。stamp 那边也用同一个判据。
pub(crate) fn plausible(secs: u64) -> bool {
    secs >= MIN_UNIX && secs < MAX_UNIX
}

#[inline]
fn rd32(off: usize) -> u32 {
    unsafe {
        read_volatile(crate::mm::kernel_phys_to_virt(crate::arch::RTC_PADDR + off) as *const u32)
    }
}

/// 读 64 位计数器（低 32 位在 0x00，高 32 位在 0x04）。
///
/// 低字大约每 4.3 秒回绕一次（纳秒计数），"先读低再读高"存在进位竞态：
/// 若恰好在两次读之间低字溢出，拼出来的值会少 2^32。误差只有一次、且只
/// 在开机时，但仍值得用"读高-读低-再读高，高字变了就重试"消掉。
fn read_raw() -> u64 {
    for _ in 0..8 {
        let hi1 = rd32(4) as u64;
        let lo = rd32(0) as u64;
        let hi2 = rd32(4) as u64;
        if hi1 == hi2 {
            return (hi1 << 32) | lo;
        }
    }
    // 连续 8 次都撞上进位，说明设备行为异常，返回最后一次的拼接值让上层去判定
    ((rd32(4) as u64) << 32) | rd32(0) as u64
}

/// 把原始读数解释成 Unix 秒。两种单位都试，只接受合理值。
fn normalize(raw: u64) -> Option<u64> {
    // ① 本来就是秒（LS7A）
    if plausible(raw) {
        return Some(raw);
    }
    // ② 纳秒（goldfish-rtc）
    let secs = raw / 1_000_000_000;
    if plausible(secs) {
        return Some(secs);
    }
    None
}

/// 开机调用一次。必须在 `mm::init_paging()` 之后（要访问 MMIO）。
pub fn init() {
    let raw = read_raw();

    match normalize(raw) {
        Some(secs) => {
            crate::timer::set_wall_clock(secs * 1_000_000);
            log::info!(
                "[rtc] wall clock = {} (raw={:#x}, pa={:#x})",
                secs,
                raw,
                crate::arch::RTC_PADDR,
            );
        }
        None => {
            // 不硬用：一个错误的时间比没有时间更坏
            log::info!(
                "[rtc] 无可用 RTC (raw={:#x}, pa={:#x}), 墙钟交给 stamp 兜底",
                raw,
                crate::arch::RTC_PADDR
            );
        }
    }
}
