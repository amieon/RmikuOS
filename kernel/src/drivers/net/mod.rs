pub mod virtio_net;
pub mod eth;
pub mod arp;
pub mod ip;
pub mod icmp;

use virtio_net::VirtioNet;

static mut NET: Option<VirtioNet> = None;

pub fn init() {
    unsafe {
        NET = VirtioNet::init();
        if NET.is_some() {
            log::info!("[net] virtio-net initialized");
        } else {
            log::warn!("[net] virtio-net init failed");
        }
    }
}

pub fn poll() {
    let mut buf = [0u8; 2048];
    let n = unsafe {
        NET.as_mut().map(|net| net.poll_rx(&mut buf)).unwrap_or(0)
    };
    if n > 0 {
        eth::input(unsafe { NET.as_mut().unwrap() }, &buf[..n]);
    }
}