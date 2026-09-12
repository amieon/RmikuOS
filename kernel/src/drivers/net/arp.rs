use crate::drivers::net::eth::{send as eth_send, MY_MAC};
use crate::drivers::net::ip::my_ip;
use crate::sync::spin::Mutex;

// 与 tcp.rs 同款的毫秒时钟(按架构分频)
#[cfg(target_arch = "riscv64")]
fn now_ms() -> u64 { (crate::timer::monotonic_time() / 10_000) as u64 }
#[cfg(target_arch = "loongarch64")]
fn now_ms() -> u64 { (crate::timer::monotonic_time() / 100_000) as u64 }

/// ARP 条目保质期: 60s。映射关系会漂移(IP 换网卡/DHCP 重租),
/// 过期后按未命中处理,触发重新解析——宁可重问,不吃陈尸答案。
const ARP_TTL_MS: u64 = 60_000;

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
    learned_ms: u64,   // 学到时刻:老化判据。命中不刷新(否则热条目永不过期)
    ref_cnt: u32,      // 命中计数:淘汰时挑最小的(Second-Chance 的计数版,类 LFU)
}

static ARP_CACHE: Mutex<[ArpEntry; 8]> = Mutex::new([
    ArpEntry { ip: 0, mac: [0; 6], valid: false, learned_ms: 0, ref_cnt: 0 };
    8
]);

/// 条目是否过期(过期等价于不存在)
fn expired(e: &ArpEntry) -> bool {
    e.valid && now_ms().wrapping_sub(e.learned_ms) > ARP_TTL_MS
}

pub fn insert(ip: u32, mac: &[u8; 6]) {
    let mut cache = ARP_CACHE.lock();
    let now = now_ms();
    // 更新已存在条目(重新学习:刷新时间戳,计数清零)
    for e in cache.iter_mut() {
        if e.valid && e.ip == ip {
            e.mac.copy_from_slice(mac);
            e.learned_ms = now;
            e.ref_cnt = 0;
            return;
        }
    }
    // 找空位或过期的尸体
    for e in cache.iter_mut() {
        if !e.valid || expired(e) {
            *e = ArpEntry { ip, mac: *mac, valid: true, learned_ms: now, ref_cnt: 1 };
            return;
        }
    }
    // 满了:挑命中计数最小的踢掉(新条目计数 1,避免刚进门就被下一轮踢)
    let mut victim = 0;
    for i in 1..cache.len() {
        if cache[i].ref_cnt < cache[victim].ref_cnt {
            victim = i;
        }
    }
    log::info!("[arp] cache full, evict ip={:#x} (ref_cnt={})",
               cache[victim].ip, cache[victim].ref_cnt);
    cache[victim] = ArpEntry { ip, mac: *mac, valid: true, learned_ms: now, ref_cnt: 1 };
}

pub fn lookup(ip: u32, out: &mut [u8; 6]) -> bool {
    let mut cache = ARP_CACHE.lock();
    for e in cache.iter_mut() {
        if e.valid && e.ip == ip {
            if expired(e) {
                // 过期: 作废并按未命中处理,上层走挂起队列重新解析
                log::info!("[arp] entry expired, re-resolve {:#x}", ip);
                e.valid = false;
                return false;
            }
            e.ref_cnt = e.ref_cnt.saturating_add(1);   // 命中计数(淘汰依据)
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
    let opcode: u16 = u16::from_be(arp.opcode);
    let sender_ip = u32::from_be(arp.sender_ip);
    let target_ip = u32::from_be(arp.target_ip);

    insert(sender_ip, &arp.sender_mac);
    crate::drivers::net::ip::on_arp_learned(sender_ip,  arp.sender_mac);
    log::info!("[arp] learned {:#x}", sender_ip);

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
    log::info!("[arp] who-has {:#x}", dst_ip);
    with_net(|net| net.send(&pkt));
}

use crate::drivers::net::with_net;