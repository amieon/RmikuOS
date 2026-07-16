pub mod virtio_net;

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

