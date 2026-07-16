use crate::drivers::net::virtio_net::VirtioNet;

pub const ETH_HDR_LEN: usize = 14;

#[repr(C, packed)]
pub struct EthHeader {
    pub dst: [u8; 6],
    pub src: [u8; 6],
    pub ethertype: u16,
}

pub static mut MY_MAC: [u8; 6] = [0x52, 0x54, 0x00, 0x12, 0x34, 0x56];

pub fn send(net: &mut VirtioNet, dst_mac: &[u8; 6], ethertype: u16, payload: &[u8]) {
    let mut pkt = alloc::vec::Vec::with_capacity(ETH_HDR_LEN + payload.len());
    pkt.extend_from_slice(dst_mac);
    unsafe { pkt.extend_from_slice(&MY_MAC); }
    pkt.extend_from_slice(&ethertype.to_be_bytes());
    pkt.extend_from_slice(payload);
    net.send(&pkt);
}

pub fn input(net: &mut VirtioNet, packet: &[u8]) {
    if packet.len() < ETH_HDR_LEN { return; }
    let eth = unsafe { &*(packet.as_ptr() as *const EthHeader) };
    let etype = u16::from_be(eth.ethertype);

    let is_broadcast = eth.dst.iter().all(|&b| b == 0xFF);
    let is_me = unsafe { eth.dst.iter().zip(MY_MAC.iter()).all(|(a, b)| a == b) };
    if !is_broadcast && !is_me { return; }

    let payload = &packet[ETH_HDR_LEN..];
    match etype {
        0x0806 => super::arp::input(payload),
        0x0800 => super::ip::input(payload),
        _ => {}
    }
}