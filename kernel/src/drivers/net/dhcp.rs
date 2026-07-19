//! DHCP 客户端（教学版）：DISCOVER → OFFER → REQUEST → ACK
//! 针对 slirp 内置 DHCP 服务器，不做续期/ rebinding。

use alloc::vec::Vec;
use crate::drivers::net::eth::my_mac_slice;
use crate::drivers::net::ip::{my_ip, set_my_ip};
use crate::drivers::net::socket;
use crate::drivers::net::udp;
use crate::println;

const SERVER_PORT: u16 = 67;
const CLIENT_PORT: u16 = 68;
const COOKIE: [u8; 4] = [99, 130, 83, 99];

const T_DISCOVER: u8 = 1;
const T_OFFER: u8 = 2;
const T_REQUEST: u8 = 3;
const T_ACK: u8 = 5;
const T_NAK: u8 = 6;

fn build_packet(xid: u32, msg_type: u8, req_ip: Option<u32>, server_id: Option<u32>) -> Vec<u8> {
    let mut pkt = alloc::vec![0u8; 236 + 4]; // BOOTP 头 + magic cookie
    pkt[0] = 1; // op: BOOTREQUEST
    pkt[1] = 1; // htype: Ethernet
    pkt[2] = 6; // hlen
    pkt[4..8].copy_from_slice(&xid.to_be_bytes());
    pkt[10..12].copy_from_slice(&0x8000u16.to_be_bytes()); // flags: 广播位，要求回复走广播
    pkt[28..34].copy_from_slice(my_mac_slice());
    pkt[236..240].copy_from_slice(&COOKIE);

    pkt.extend_from_slice(&[53, 1, msg_type]); // DHCP message type
    if let Some(ip) = req_ip {
        pkt.extend_from_slice(&[50, 4]);
        pkt.extend_from_slice(&ip.to_be_bytes()); // requested IP
    }
    if let Some(sid) = server_id {
        pkt.extend_from_slice(&[54, 4]);
        pkt.extend_from_slice(&sid.to_be_bytes()); // server identifier
    }
    pkt.extend_from_slice(&[55, 4, 1, 3, 6, 51]); // 请求参数：掩码/网关/DNS/租期
    pkt.push(255);
    pkt
}

struct Reply {
    msg_type: u8,
    yiaddr: u32,
    server_id: u32,
    router: u32,
    dns: u32,
    lease: u32,
}

fn parse_reply(data: &[u8], xid: u32) -> Option<Reply> {
    if data.len() < 240 || data[0] != 2 {
        return None; // 太短或不是 BOOTREPLY
    }
    if u32::from_be_bytes(data[4..8].try_into().ok()?) != xid {
        return None;
    }
    if data[236..240] != COOKIE {
        return None;
    }
    let mut r = Reply {
        msg_type: 0,
        yiaddr: u32::from_be_bytes(data[16..20].try_into().ok()?),
        server_id: 0,
        router: 0,
        dns: 0,
        lease: 0,
    };
    let mut i = 240;
    while i < data.len() {
        let tag = data[i];
        if tag == 255 {
            break;
        }
        if tag == 0 {
            i += 1;
            continue;
        }
        if i + 1 >= data.len() {
            return None;
        }
        let len = data[i + 1] as usize;
        if i + 2 + len > data.len() {
            return None;
        }
        let v = &data[i + 2..i + 2 + len];
        match tag {
            53 if len == 1 => r.msg_type = v[0],
            54 if len == 4 => r.server_id = u32::from_be_bytes(v.try_into().ok()?),
            3 if len >= 4 => r.router = u32::from_be_bytes(v[..4].try_into().ok()?),
            6 if len >= 4 => r.dns = u32::from_be_bytes(v[..4].try_into().ok()?),
            51 if len == 4 => r.lease = u32::from_be_bytes(v.try_into().ok()?),
            _ => {}
        }
        i += 2 + len;
    }
    Some(r)
}

fn fmt_ip(ip: u32) -> (u32, u32, u32, u32) {
    ((ip >> 24) & 0xff, (ip >> 16) & 0xff, (ip >> 8) & 0xff, ip & 0xff)
}

/// 等一个特定类型的 DHCP 回复；期间周期性重发请求包
fn wait_reply(fd: usize, xid: u32, want: &[u8], resend: &dyn Fn()) -> Option<Reply> {
    let mut buf = [0u8; 1024];
    let mut spins = 0usize;
    loop {
        crate::drivers::net::poll();
        if let Some((_, n)) = socket::socket_recvfrom(fd, &mut buf) {
            if let Some(r) = parse_reply(&buf[..n], xid) {
                if want.contains(&r.msg_type) {
                    return Some(r);
                }
            }
        }
        spins += 1;
        if spins % 10_000_000 == 0 {
            println!("[dhcp] still waiting, resending...");
            resend();
        }
        if spins >= 50_000_000 {          // ← 新增:总超时,放弃
            log::warn!("[dhcp] timeout, give up");
            return None;
        }
    }
}

pub fn dhcp_test() {
    let fd = match socket::socket_create(2,0) { // 2 = UDP
        Some(f) => f,
        None => {
            log::warn!("[dhcp] socket table full");
            return;
        }
    };
    if !socket::socket_bind(fd, CLIENT_PORT) {
        log::warn!("[dhcp] bind 68 failed");
        return;
    }

    let xid = 0x3903_F326; // 教学版固定 xid
    let discover = build_packet(xid, T_DISCOVER, None, None);
    log::info!("[dhcp] >>> DISCOVER");
    udp::send_broadcast(CLIENT_PORT, SERVER_PORT, &discover);

    let offer = match wait_reply(fd, xid, &[T_OFFER], &|| {
        udp::send_broadcast(CLIENT_PORT, SERVER_PORT, &build_packet(xid, T_DISCOVER, None, None));
    }) {
        Some(o) => o,
        None => return,
    };
    let (a, b, c, d) = fmt_ip(offer.yiaddr);
    let (sa, sb, sc, sd) = fmt_ip(offer.server_id);
    log::info!("[dhcp] <<< OFFER: ip={}.{}.{}.{} server={}.{}.{}.{}", a, b, c, d, sa, sb, sc, sd);

    let request = build_packet(xid, T_REQUEST, Some(offer.yiaddr), Some(offer.server_id));
    log::info!("[dhcp] >>> REQUEST");
    udp::send_broadcast(CLIENT_PORT, SERVER_PORT, &request);

    let ack = match wait_reply(fd, xid, &[T_ACK, T_NAK], &|| {
        udp::send_broadcast(CLIENT_PORT, SERVER_PORT, &request);
    }) {
        Some(r) => r,
        None => return,
    };
    if ack.msg_type == T_NAK {
        log::warn!("[dhcp] <<< NAK，服务器拒绝，放弃");
        return;
    }

    set_my_ip(ack.yiaddr);
    let (ga, gb, gc, gd) = fmt_ip(ack.router);
    let (na, nb, nc, nd) = fmt_ip(ack.dns);
    log::info!(
        "[dhcp] <<< ACK! leased {}.{}.{}.{}, gw={}.{}.{}.{}, dns={}.{}.{}.{}, lease={}s",
        a, b, c, d, ga, gb, gc, gd, na, nb, nc, nd, ack.lease
    );
    let (ma, mb, mc, md) = fmt_ip(my_ip());
    log::info!("[dhcp] my_ip() 现在 = {}.{}.{}.{}", ma, mb, mc, md);
}