//! fake-hwclock —— 没有可用 RTC 时的开机时间来源。
//!
//! 和 Debian 无 RTC 机型上的 `fake-hwclock` 是同一套思路：把上次关机/校准时的
//! Unix 秒存成一个文件，开机读回来，再用 `monotonic_us()` 往前推。
use crate::fs::{self, flag::O_RDONLY};

pub const STAMP_PATH: &str = "/var/lib/rtcstamp";

/// 解析十进制 Unix 秒。
fn parse_secs(buf: &[u8]) -> Option<u64> {
    let mut seen_digit = false;
    let mut secs: u64 = 0;

    for &b in buf {
        match b {
            b'0'..=b'9' => {
                seen_digit = true;
                secs = secs.checked_mul(10)?.checked_add((b - b'0') as u64)?;
            }
            b'\n' | b'\r' | b' ' | b'\t' | 0 => break,
            _ => return None,
        }
    }

    if seen_digit && super::plausible(secs) {
        Some(secs)
    } else {
        None
    }
}


pub fn restore() -> bool {
    // RTC 已经给出真值 → stamp 必然更旧，不能覆盖。
    if crate::timer::now_secs() != 0 {
        return false;
    }

    let file = match fs::open(STAMP_PATH, O_RDONLY, 0) {
        Ok(f) => f,
        Err(e) => {
            // 首次启动、或没有数据盘：都不算错误，只是没有历史值可用。
            // e 是正 errno（fs::open 的约定；syscall 层才取负）。
            log::info!(
                "[hwclock] {} 打不开 (errno={}), 无历史时间; 跑一次 ntpdate 即可建立",
                STAMP_PATH,
                e
            );
            return false;
        }
    };

    let mut buf = [0u8; 32];
    let n = file.read(&mut buf);
    if n <= 0 {
        log::warn!("[hwclock] {} 读取失败 (n={})", STAMP_PATH, n);
        return false;
    }

    match parse_secs(&buf[..n as usize]) {
        Some(secs) => {
            crate::timer::set_wall_clock(secs * 1_000_000);
            log::info!(
                "[hwclock] 从 stamp 恢复墙钟 {} (上次运行时刻, 等 ntpdate 校正)",
                secs
            );
            true
        }
        None => {
            log::warn!("[hwclock] {} 内容不可解析, 忽略", STAMP_PATH);
            false
        }
    }
}
