use crate::drivers::net::eth::{send as eth_send, MY_MAC};
use crate::drivers::net::virtio_net::VirtioNet;
use crate::drivers::net::arp;

pub const MY_IP: u32 = 0x0A00020F; // 10.0.2.15

#[repr(C, packed)]
pub struct IpHeader {
    pub ver_ihl: u8,
    pub tos: u8,
    pub tot_len: u16,
    pub id: u16,
    pub frag_off: u16,
    pub ttl: u8,
    pub protocol: u8,
    pub check: u16,
    pub saddr: u32,
    pub daddr: u32,
}

pub fn checksum(data: &[u8]) -> u16 {
    let mut sum: u32 = 0;
    let mut i = 0;
    while i + 1 < data.len() {
        sum += ((data[i] as u32) << 8) | (data[i + 1] as u32);
        i += 2;
    }
    if i < data.len() {
        sum += (data[i] as u32) << 8;
    }
    while (sum >> 16) != 0 {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }
    !(sum as u16)
}

pub fn send(net: &mut VirtioNet, dst_ip: u32, protocol: u8, payload: &[u8]) {
    let ip_len = core::mem::size_of::<IpHeader>() + payload.len();
    let mut pkt = alloc::vec::Vec::with_capacity(ip_len);
    unsafe { pkt.set_len(core::mem::size_of::<IpHeader>()) };
    let ip = unsafe { &mut *(pkt.as_mut_ptr() as *mut IpHeader) };
    ip.ver_ihl = 0x45; ip.tos = 0;
    ip.tot_len = (ip_len as u16).to_be();
    ip.id = 0; ip.frag_off = 0x4000u16.to_be();
    ip.ttl = 64; ip.protocol = protocol;
    ip.check = 0; ip.saddr = MY_IP.to_be(); ip.daddr = dst_ip.to_be();
    pkt.extend_from_slice(payload);

    let csum = checksum(&pkt[..core::mem::size_of::<IpHeader>()]);
    unsafe { core::ptr::write_unaligned(core::ptr::addr_of_mut!((*ip).check), csum) };

    let mut dst_mac = [0u8; 6];
    if !arp::lookup(dst_ip, &mut dst_mac) { return; }
    eth_send(net, &dst_mac, 0x0800, &pkt);
}

pub fn input(packet: &[u8]) {
    if packet.len() < core::mem::size_of::<IpHeader>() { return; }
    let ip = unsafe { &*(packet.as_ptr() as *const IpHeader) };
    let hdr_len = ((ip.ver_ihl & 0x0F) * 4) as usize;
    if hdr_len < 20 { return; }
    if checksum(&packet[..hdr_len]) != 0 { return; }

    let dst = u32::from_be(ip.daddr);
    if dst != MY_IP && dst != 0xFFFFFFFF { return; }

    let payload = &packet[hdr_len..];
    match ip.protocol {
        1 => super::icmp::input(payload, u32::from_be(ip.saddr)),
        _ => {}
    }
}