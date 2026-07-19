pub mod virtio_net;
pub mod eth;
pub mod arp;
pub mod ip;
pub mod icmp;
pub mod udp;
pub mod tcp;
pub mod socket;
pub mod dhcp;

use core::sync::atomic::{AtomicBool, Ordering};

use virtio_net::VirtioNet;
use crate::{drivers::net::virtio_net::VirtioNetHdr, sync::spin::Mutex, lock_detect};

pub(crate) static NET: Mutex<Option<VirtioNet>> = Mutex::new(None);

static NET_POLL_PENDING: AtomicBool = AtomicBool::new(false);

pub fn init() {
    let net = VirtioNet::init();
    let mut guard = lock_detect!(NET);
    if net.is_some() {
        *guard = net;
        log::info!("[net] virtio-net initialized");
    } else {
        log::warn!("[net] virtio-net init failed");
    }
}

pub fn poll() {
    let mut buf = [0u8; 2048];
    let n = {
        let mut guard = lock_detect!(NET);
        guard.as_mut().map(|net| net.poll_rx(&mut buf)).unwrap_or(0)
    };
    if n > 0 {
        //println!("[net] RX {} bytes", n);  // 确认收到包
        let hdr_len = core::mem::size_of::<VirtioNetHdr>() as usize;
        if n > hdr_len {
            eth::input(&buf[hdr_len..n]);
        }
    }
    tcp::tick();
}
/// 供 UDP/IP 层发送时获取网卡引用（调用者已持有锁或确保单核执行）
pub fn with_net<F, R>(f: F) -> R
where
    F: FnOnce(&mut VirtioNet) -> R,
{
    let mut guard = lock_detect!(NET);
    f(guard.as_mut().expect("net not initialized"))
}



pub fn on_timer_tick() {
    NET_POLL_PENDING.store(true, Ordering::Relaxed);
    tcp::tick();  
}

pub fn maybe_poll() {
    if NET_POLL_PENDING.swap(false, Ordering::Relaxed) {
        poll();
    }
}