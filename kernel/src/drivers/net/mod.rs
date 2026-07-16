pub mod virtio_net;
pub mod eth;
pub mod arp;
pub mod ip;
pub mod icmp;
pub mod udp;
pub mod socket;

use virtio_net::VirtioNet;
use crate::{drivers::net::virtio_net::VirtioNetHdr, println, sync::spin::Mutex};

pub(crate) static NET: Mutex<Option<VirtioNet>> = Mutex::new(None);

pub fn init() {
    let net = VirtioNet::init();
    let mut guard = NET.lock();
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
        let mut guard = NET.lock();
        guard.as_mut().map(|net| net.poll_rx(&mut buf)).unwrap_or(0)
    };
    if n > 0 {
        println!("[net] RX {} bytes", n);  // 确认收到包
        // net/mod.rs poll() 里，把 size_of 换成：
        let hdr_len = core::mem::size_of::<VirtioNetHdr>() as usize;
        if n > hdr_len {
            eth::input(&buf[hdr_len..n]);
        }
    }
}
/// 供 UDP/IP 层发送时获取网卡引用（调用者已持有锁或确保单核执行）
pub fn with_net<F, R>(f: F) -> R
where
    F: FnOnce(&mut VirtioNet) -> R,
{
    let mut guard = NET.lock();
    f(guard.as_mut().expect("net not initialized"))
}