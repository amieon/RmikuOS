use crate::drivers::net::eth::{send as eth_send, MY_MAC};
use crate::drivers::net::ip::my_ip;
use crate::sync::spin::Mutex;

#[repr(C, packed)]
struct ArpHeader {
    hw_type: u16,
    proto_type: u16,
    hw_len: u8,
    proto_len: u8,
    opcode: u16,
    sender_mac: [u8; 6],
    sender_ip: u32,
    target_mac: [u8; 6],
    target_ip: u32,
}

#[derive(Clone, Copy)]
struct ArpEntry {
    ip: u32,
    mac: [u8; 6],
    valid: bool,
}

static ARP_CACHE: Mutex<[ArpEntry; 8]> = Mutex::new([
    ArpEntry { ip: 0, mac: [0; 6], valid: false };
    8
]);

pub fn insert(ip: u32, mac: &[u8; 6]) {
    let mut cache = ARP_CACHE.lock();
    // 更新已存在条目
    for e in cache.iter_mut() {
        if e.valid && e.ip == ip {
            e.mac.copy_from_slice(mac);
            return;
        }
    }
    // 找空位
    for e in cache.iter_mut() {
        if !e.valid {
            e.ip = ip;
            e.mac.copy_from_slice(mac);
            e.valid = true;
            return;
        }
    }
    // 满了则替换第一个（可改进为 LRU）
    cache[0].ip = ip;
    cache[0].mac.copy_from_slice(mac);
    cache[0].valid = true;
}

pub fn lookup(ip: u32, out: &mut [u8; 6]) -> bool {
    let cache = ARP_CACHE.lock();
    for e in cache.iter() {
        if e.valid && e.ip == ip {
            out.copy_from_slice(&e.mac);
            return true;
        }
    }
    false
}

pub fn input(packet: &[u8]) {
    if packet.len() < 28 {
        return;
    }
    let arp = unsafe { packet.as_ptr().cast::<ArpHeader>().read_unaligned() };
    let opcode = u16::from_be(arp.opcode);
    let sender_ip = u32::from_be(arp.sender_ip);
    let target_ip = u32::from_be(arp.target_ip);

    insert(sender_ip, &arp.sender_mac);
    crate::drivers::net::ip::on_arp_learned(sender_ip,  arp.sender_mac);

    if opcode == 1 && target_ip == my_ip() {
        let mut reply = [0u8; 42];
        // 填充以太网头
        {
            let eth = unsafe { &mut *(reply.as_mut_ptr() as *mut super::eth::EthHeader) };
            eth.dst.copy_from_slice(&arp.sender_mac);
            let my_mac = MY_MAC.lock();
            eth.src.copy_from_slice(&*my_mac);
            eth.ethertype = 0x0806u16.to_be();
        }
        // 填充 ARP 头
        let arp_r = unsafe { &mut *(reply.as_mut_ptr().add(14) as *mut ArpHeader) };
        arp_r.hw_type = 1u16.to_be();
        arp_r.proto_type = 0x0800u16.to_be();
        arp_r.hw_len = 6;
        arp_r.proto_len = 4;
        arp_r.opcode = 2u16.to_be();
        {
            let my_mac = MY_MAC.lock();
            arp_r.sender_mac.copy_from_slice(&*my_mac);
        }
        arp_r.sender_ip = my_ip().to_be();
        arp_r.target_mac.copy_from_slice(&arp.sender_mac);
        arp_r.target_ip = sender_ip.to_be();

        with_net(|net| net.send(&reply));
    }
}

/// 发送 ARP 请求查询目标 IP 的 MAC
pub fn request(dst_ip: u32) {
    let mut pkt = [0u8; 42];
    // 以太网头
    {
        let eth = unsafe { &mut *(pkt.as_mut_ptr() as *mut super::eth::EthHeader) };
        eth.dst = [0xFF; 6]; // broadcast
        let my_mac = MY_MAC.lock();
        eth.src.copy_from_slice(&*my_mac);
        eth.ethertype = 0x0806u16.to_be();
    }
    // ARP 头
    let arp = unsafe { &mut *(pkt.as_mut_ptr().add(14) as *mut ArpHeader) };
    arp.hw_type = 1u16.to_be();
    arp.proto_type = 0x0800u16.to_be();
    arp.hw_len = 6;
    arp.proto_len = 4;
    arp.opcode = 1u16.to_be(); // REQUEST
    {
        let my_mac = MY_MAC.lock();
        arp.sender_mac.copy_from_slice(&*my_mac);
    }
    arp.sender_ip = my_ip().to_be();
    arp.target_mac = [0; 6];
    arp.target_ip = dst_ip.to_be();

    with_net(|net| net.send(&pkt));
}

use crate::drivers::net::with_net;