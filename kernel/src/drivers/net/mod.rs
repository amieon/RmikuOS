pub mod virtio_net;
pub mod eth;
pub mod arp;
pub mod ip;
pub mod icmp;
pub mod udp;
pub mod tcp;
pub mod socket;
pub mod dhcp;
pub mod dns;

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


const RX_DRAIN_MAX: usize = 64;


pub fn poll() -> usize {
    let mut buf = [0u8; 2048];
    let hdr_len = core::mem::size_of::<VirtioNetHdr>() as usize;
    let mut got = 0usize;

    while got < RX_DRAIN_MAX {
        let n = {
            let mut guard = lock_detect!(NET);
            guard.as_mut().map(|net| net.poll_rx(&mut buf)).unwrap_or(0)
        };
        if n == 0 {
            break;
        }
        got += 1;
        if n > hdr_len {
            eth::input(&buf[hdr_len..n]);
        }
    }

    tcp::tick();
    dhcp::tick();
    got
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
}


static PUMPING: AtomicBool = AtomicBool::new(false);

pub fn pump_console() {
    if PUMPING.swap(true, Ordering::Relaxed) {
        return;
    }
    let ready = {
        let guard = lock_detect!(NET);
        guard.is_some()
    };
    if ready {
        poll();
    }
    PUMPING.store(false, Ordering::Relaxed);
}

pub fn maybe_poll() {
    if NET_POLL_PENDING.swap(false, Ordering::Relaxed) {
        poll();
    }
}