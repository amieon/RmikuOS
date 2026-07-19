use crate::drivers::net::virtio_net::VirtioNet;
use crate::drivers::net::{with_net, NET};
use crate::sync::spin::Mutex;

pub const ETH_HDR_LEN: usize = 14;

#[repr(C, packed)]
pub struct EthHeader {
    pub dst: [u8; 6],
    pub src: [u8; 6],
    pub ethertype: u16,
}

pub static MY_MAC: Mutex<[u8; 6]> = Mutex::new([0x52, 0x54, 0x00, 0x12, 0x34, 0x56]); // 初值只是兜底

pub fn set_my_mac(mac: [u8; 6]) {
    *MY_MAC.lock() = mac;
    log::info!("[eth] MAC = {:02x}:{:02x}:{:02x}:{:02x}:{:02x}:{:02x}",
        mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

/// 按值返回,替代旧的 my_mac_slice()
pub fn my_mac() -> [u8; 6] {
    *MY_MAC.lock()
}

pub fn send(dst_mac: &[u8; 6], ethertype: u16, payload: &[u8]) {
    let mut pkt = alloc::vec::Vec::with_capacity(ETH_HDR_LEN + payload.len());
    pkt.extend_from_slice(dst_mac);
    {
        let my_mac = MY_MAC.lock();
        pkt.extend_from_slice(&*my_mac);
    }
    pkt.extend_from_slice(&ethertype.to_be_bytes());
    pkt.extend_from_slice(payload);

    with_net(|net| net.send(&pkt));
}

pub fn input(packet: &[u8]) {
    if packet.len() < ETH_HDR_LEN {
        return;
    }
    // 使用 unaligned read 避免 UB
    let eth = unsafe { packet.as_ptr().cast::<EthHeader>().read_unaligned() };
    let etype = u16::from_be(eth.ethertype);

    let is_broadcast = eth.dst.iter().all(|&b| b == 0xFF);
    let is_me = {
        let my_mac = MY_MAC.lock();
        eth.dst.iter().zip(my_mac.iter()).all(|(a, b)| a == b)
    };
    if !is_broadcast && !is_me {
        return;
    }

    let payload = &packet[ETH_HDR_LEN..];
    match etype {
        0x0806 => super::arp::input(payload),
        0x0800 => super::ip::input(payload),
        _ => {}
    }
}