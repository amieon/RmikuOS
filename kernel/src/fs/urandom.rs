use alloc::sync::Arc;

use super::file::{File, FileRef, POLLIN};
use super::stat::*;

/// `/dev/urandom` —— 只读熵源，背后是 virtio-rng（`drivers::rng`）。
pub struct UrandomFile;

impl File for UrandomFile {
    fn readable(&self) -> bool {
        true
    }

    /// 不可写。Linux 的 `/dev/urandom` 允许 write（把数据混进熵池），但我们
    /// 没有熵池，只有一台直通设备；收下写请求等于假装成功，不如明确拒绝。
    fn writable(&self) -> bool {
        false
    }

    fn read(&self, buf: &mut [u8]) -> isize {
        if buf.is_empty() {
            return 0;
        }
        if crate::drivers::rng::fill_bytes(buf) {
            buf.len() as isize
        } else {
            -crate::syscall::errno::EAGAIN
        }
    }

    fn write(&self, _buf: &[u8]) -> isize {
        -crate::syscall::errno::EACCES
    }

    /// 读永远立即可用：设备不排队，每次 read 都当场向 virtio-rng 要数据。
    fn poll_ready(&self, events: i16) -> i16 {
        if events & POLLIN != 0 {
            POLLIN
        } else {
            0
        }
    }

    fn stat(&self) -> Stat {
        /* 0o444：只读。Linux 上是 0o666，因为那边可写；我们的语义不同，
         * 权限位就该跟着不同，别照抄。 */
        Stat::new(STAT_TYPE_CHAR, 0, 0o444, 0, 0).with_dev(DEV_URANDOM)
    }
}

pub fn urandom() -> FileRef {
    Arc::new(UrandomFile)
}
