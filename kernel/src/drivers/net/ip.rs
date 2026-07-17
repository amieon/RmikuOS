use crate::drivers::net::eth::{send as eth_send, MY_MAC};
use crate::drivers::net::arp;
use crate::drivers::net::with_net;
use crate::println;
use crate::sync::spin::Mutex;


use core::sync::atomic::{AtomicU32, Ordering};

pub static MY_IP: AtomicU32 = AtomicU32::new(0x0A00020F);
pub fn my_ip() -> u32 { MY_IP.load(Ordering::Relaxed) }
pub fn set_my_ip(ip: u32) { MY_IP.store(ip, Ordering::Relaxed); }

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


/// 存的是「已经组好的完整 IP 包」，补发时直接 eth_send，不需要重新组包
struct PendingPkt {
    dst_ip: u32,
    len: usize,
    data: [u8; 1514],
}

const EMPTY: Option<PendingPkt> = None;
static PENDING: Mutex<[Option<PendingPkt>; 4]> = Mutex::new([None, None, None, None]);

fn enqueue_pending(dst_ip: u32, pkt: &[u8]) {
    if pkt.len() > 1514 {
        return; // 教学版不做 IP 分片
    }
    let mut guard = PENDING.lock();
    let q: &mut [Option<PendingPkt>; 4] = &mut *guard;

    // 同目标的旧包直接覆盖，省一个槽
    for i in 0..q.len() {
        if let Some(p) = q[i].as_mut() {
            if p.dst_ip == dst_ip {
                p.data[..pkt.len()].copy_from_slice(pkt);
                p.len = pkt.len();
                return;
            }
        }
    }
    // 找空槽
    for i in 0..q.len() {
        if q[i].is_none() {
            let mut p = PendingPkt { dst_ip, len: pkt.len(), data: [0; 1514] };
            p.data[..pkt.len()].copy_from_slice(pkt);
            q[i] = Some(p);
            return;
        }
    }
   // println!("[ip] pending queue full, drop"); // 4 槽全满才真丢
}

/// ARP 学到新表项后由 arp 模块回调。
/// 锁纪律：调用点不能持有 ARP_CACHE 锁；发包时 PENDING 锁已释放，无环。
pub fn on_arp_learned(ip: u32, mac: [u8; 6]) {
    loop {
        // 每次摘一个匹配包，摘不到就结束；锁在发包前已释放
        let pkt: Option<PendingPkt> = {
            let mut guard = PENDING.lock();
            let q: &mut [Option<PendingPkt>; 4] = &mut *guard;
            let mut found: Option<PendingPkt> = None;
            for i in 0..q.len() {
                if let Some(p) = q[i].as_ref() {
                    if p.dst_ip == ip {
                        found = q[i].take();
                        break;
                    }
                }
            }
            found
        };
        match pkt {
            Some(p) => {
                //println!("[ip] ARP resolved, flush pending {} bytes", p.len);
                eth_send(&mac, 0x0800, &p.data[..p.len]);
            }
            None => break,
        }
    }
}

pub fn send(dst_ip: u32, protocol: u8, payload: &[u8]) {
    let ip_len = core::mem::size_of::<IpHeader>() + payload.len();
    let mut pkt = alloc::vec::Vec::with_capacity(ip_len);
    unsafe { pkt.set_len(core::mem::size_of::<IpHeader>()) };
    let ip = unsafe { &mut *(pkt.as_mut_ptr() as *mut IpHeader) };
    ip.ver_ihl = 0x45;
    ip.tos = 0;
    ip.tot_len = (ip_len as u16).to_be();
    ip.id = 0;
    ip.frag_off = 0x4000u16.to_be(); // DF
    ip.ttl = 64;
    ip.protocol = protocol;
    ip.check = 0;
    ip.saddr = my_ip().to_be();
    ip.daddr = dst_ip.to_be();
    pkt.extend_from_slice(payload);

    let csum = checksum(&pkt[..core::mem::size_of::<IpHeader>()]);
    unsafe {
        core::ptr::write_unaligned(
            core::ptr::addr_of_mut!((*ip).check),
            csum.to_be(),
        );
    }

    let mut dst_mac = [0u8; 6];
    if !arp::lookup(dst_ip, &mut dst_mac) {
        // ARP 未命中：挂起完整 IP 包，reply 到了由 on_arp_learned 补发（原来在这里直接丢包）
        enqueue_pending(dst_ip, &pkt);
        arp::request(dst_ip);
        return;
    }
    eth_send(&dst_mac, 0x0800, &pkt);
}

pub fn input(packet: &[u8]) {
    if packet.len() < core::mem::size_of::<IpHeader>() {
        return;
    }
    let ip = unsafe { packet.as_ptr().cast::<IpHeader>().read_unaligned() };
    let hdr_len = ((ip.ver_ihl & 0x0F) * 4) as usize;
    if hdr_len < 20 {
        return;
    }
    if checksum(&packet[..hdr_len]) != 0 {
        return;
    }

    let dst = u32::from_be(ip.daddr);
    if dst != my_ip() && dst != 0xFFFFFFFF {
        return;
    }

    let payload = &packet[hdr_len..];
    match ip.protocol {
        1 => super::icmp::input(payload, u32::from_be(ip.saddr)),
        17 => super::udp::input(payload, u32::from_be(ip.saddr), dst),
        _ => {}
    }
}