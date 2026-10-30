pub mod virtio_rng;

use core::sync::atomic::{AtomicBool, Ordering};

use crate::lock_detect;
use crate::sync::spin::Mutex;

use virtio_rng::VirtioRng;

static RNG: Mutex<Option<VirtioRng>> = Mutex::new(None);
static RNG_READY: AtomicBool = AtomicBool::new(false);

/// 必须在 PCI 总线扫描之后调用（discover_disks 里会 scan_pci_bus）。
pub fn init() {
    match VirtioRng::init() {
        Some(rng) => {
            *lock_detect!(RNG) = Some(rng);
            RNG_READY.store(true, Ordering::Release);
            log::info!("[rng] virtio-rng initialized");
        }
        None => {
            // 这件事要喊出来：没有熵源时 OpenSSL / ssh 会在握手阶段失败，
            // 而失败原因看起来像"证书或算法问题"，极难联想到这里。
            log::warn!("[rng] no virtio-rng device, entropy unavailable");
        }
    }
}

pub fn available() -> bool {
    RNG_READY.load(Ordering::Acquire)
}

/// 用设备熵填满 `out`。返回 false = 没有可信随机数，调用方必须当失败处理。
pub fn fill_bytes(out: &mut [u8]) -> bool {
    if out.is_empty() {
        return true;
    }
    if !available() {
        return false;
    }
    let mut guard = lock_detect!(RNG);
    match guard.as_mut() {
        Some(rng) => rng.fill_bytes(out),
        None => false,
    }
}
