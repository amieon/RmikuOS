//! DHCP 客户端（教学版）：DISCOVER → OFFER → REQUEST → ACK + 租约续期。
//! 针对 slirp 内置 DHCP 服务器。
//!
//! 续期模型(RFC 2131 §4.4.5):
//! - T1(默认 0.5*lease,可被 option 58 覆盖):单播 REQUEST 给原服务器;
//! - T2(默认 0.875*lease,可被 option 59 覆盖):广播 REQUEST,允许任意服务器应答;
//! - 到期仍未续上:地址作废(my_ip 清零),重新走完整握手。
//! 时钟驱动:net::poll() 每次跑完顺手调 dhcp::tick(),
//! maybe_poll 由定时器中断置位触发,所以 tick 实际由系统节拍驱动。

use alloc::vec::Vec;
use crate::drivers::net::ip::{my_ip, set_my_ip};
use crate::drivers::net::socket;
use crate::drivers::net::udp;
use crate::sync::spin::Mutex;
use crate::println;

#[cfg(target_arch = "riscv64")]
fn now_ms() -> u64 { (crate::timer::monotonic_time() / 10_000) as u64 }

#[cfg(target_arch = "loongarch64")]
fn now_ms() -> u64 { (crate::timer::monotonic_time() / 100_000) as u64 }

/// 调试旋钮:非 0 时忽略服务器给的租约,强制用这么多秒(测续租不用等 12 小时)。
/// 例:设 60 → 30s 时能在串口看到 T1 单播续租。
const DHCP_DEBUG_LEASE_SECS: u32 = 0;

/// 续租请求的重传间隔
const RENEW_RETRY_MS: u64 = 2000;

const SERVER_PORT: u16 = 67;
const CLIENT_PORT: u16 = 68;
const COOKIE: [u8; 4] = [99, 130, 83, 99];

const T_DISCOVER: u8 = 1;
const T_OFFER: u8 = 2;
const T_REQUEST: u8 = 3;
const T_ACK: u8 = 5;
const T_NAK: u8 = 6;

/// ciaddr: 续租/重绑 REQUEST 填当前租到的 IP;初次握手一律 0(此时还没有地址)。
fn build_packet(xid: u32, msg_type: u8, ciaddr: u32, req_ip: Option<u32>, server_id: Option<u32>) -> Vec<u8> {
    let mut pkt = alloc::vec![0u8; 236 + 64]; // BOOTP 头 + 64 字节 vend 区(slirp 硬性要求 ≥300)
    pkt[0] = 1;
    pkt[1] = 1;
    pkt[2] = 6;
    pkt[4..8].copy_from_slice(&xid.to_be_bytes());
    pkt[10..12].copy_from_slice(&0x8000u16.to_be_bytes());
    pkt[12..16].copy_from_slice(&ciaddr.to_be_bytes());
    pkt[28..34].copy_from_slice(&crate::drivers::net::eth::my_mac());
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
    t1: u32,   // option 58: 续租时间点,缺省 0.5*lease
    t2: u32,   // option 59: 重绑时间点,缺省 0.875*lease
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
        t1: 0,
        t2: 0,
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
            58 if len == 4 => r.t1 = u32::from_be_bytes(v.try_into().ok()?),
            59 if len == 4 => r.t2 = u32::from_be_bytes(v.try_into().ok()?),
            _ => {}
        }
        i += 2 + len;
    }
    Some(r)
}

fn fmt_ip(ip: u32) -> (u32, u32, u32, u32) {
    ((ip >> 24) & 0xff, (ip >> 16) & 0xff, (ip >> 8) & 0xff, ip & 0xff)
}
struct LeaseState {
    fd: Option<usize>,        // 绑定 68 的常驻 socket,boot 握手与续租共用
    xid: u32,                 // 教学版固定 xid
    ip: u32,                  // 当前租到的 IP(续租 REQUEST 的 ciaddr 用)
    server: u32,              // DHCP 服务器(T1 单播的目的地)
    t1_ms: u64,               // 续租时间点(单播)
    t2_ms: u64,               // 重绑时间点(广播)
    expire_ms: u64,           // 租约到期
    awaiting_ack_since: u64,  // 0 = 没在等续租 ACK;否则为上次发 REQUEST 的时刻
    leased: bool,
}

impl LeaseState {
    const fn new() -> Self {
        Self {
            fd: None, xid: 0x3903_F326, ip: 0, server: 0,
            t1_ms: 0, t2_ms: 0, expire_ms: 0,
            awaiting_ack_since: 0, leased: false,
        }
    }
}

static LEASE: Mutex<LeaseState> = Mutex::new(LeaseState::new());

/// ACK 落地:写地址/DNS,按租约算 T1/T2/到期三个 deadline
fn apply_lease(ack: &Reply) {
    set_my_ip(ack.yiaddr);
    if ack.dns != 0 {
        crate::drivers::net::dns::set_dns_server(ack.dns); // 服务器没给 option 6 就保留默认 10.0.2.3
    }
    let lease_secs = if DHCP_DEBUG_LEASE_SECS != 0 {
        DHCP_DEBUG_LEASE_SECS
    } else if ack.lease == 0 {
        3600 // 服务器没给租约(option 51),兜底 1 小时
    } else {
        ack.lease
    };
    let lease_ms = lease_secs as u64 * 1000;
    // RFC 2131: T1/T2 可被 option 58/59 覆盖,缺省 0.5 / 0.875
    let t1_ms = if ack.t1 != 0 { ack.t1 as u64 * 1000 } else { lease_ms / 2 };
    let t2_ms = if ack.t2 != 0 { ack.t2 as u64 * 1000 } else { lease_ms * 7 / 8 };
    let now = now_ms();
    let mut st = LEASE.lock();
    st.ip = ack.yiaddr;
    st.server = ack.server_id;
    st.t1_ms = now + t1_ms;
    st.t2_ms = now + t2_ms;
    st.expire_ms = now + lease_ms;
    st.awaiting_ack_since = 0;
    st.leased = true;
    log::info!(
        "[dhcp] lease applied: T1={}s T2={}s expire={}s",
        t1_ms / 1000, t2_ms / 1000, lease_ms / 1000
    );
}

/// 地址作废 + 重新完整握手(到期 / 续租被 NAK 路径)。
fn reacquire() {
    {
        let mut st = LEASE.lock();
        st.leased = false;
        st.awaiting_ack_since = 0;
    }
    set_my_ip(0);
    handshake();
}

/// 租约续期时钟:由 net::poll() 每次调用(与 tcp::tick 同一钩子)。
/// 全程非阻塞:收 ACK 只是试弹队列,发 REQUEST 有 RENEW_RETRY_MS 节流。
pub fn tick() {
    // 快照一次状态,避免持锁做 IO
    let (fd, xid, awaiting, leased) = {
        let st = LEASE.lock();
        (st.fd, st.xid, st.awaiting_ack_since, st.leased)
    };
    let fd = match fd { Some(f) => f, None => return };
    if !leased { return; }

    // 只在等 ACK 时才弹队列,免得抢走 boot 握手 wait_reply 在等的包
    if awaiting != 0 {
        let mut buf = [0u8; 576];
        while let Some((_, n)) = udp::recvfrom(fd, &mut buf) {
            let r = match parse_reply(&buf[..n], xid) { Some(r) => r, None => continue };
            match r.msg_type {
                T_ACK => {
                    let (a, b, c, d) = fmt_ip(r.yiaddr);
                    log::info!("[dhcp] <<< ACK (renewal) {}.{}.{}.{},续租成功", a, b, c, d);
                    apply_lease(&r);
                    return;
                }
                T_NAK => {
                    log::warn!("[dhcp] <<< NAK (renewal),租约被拒,地址作废");
                    reacquire();
                    return;
                }
                _ => {}
            }
        }
    }

    // deadline 检查:过 T2 广播,过 T1 单播,都走重传节流
    let now = now_ms();
    let mut st = LEASE.lock();
    if !st.leased { return; } // 前面步骤可能刚改过状态
    if now >= st.expire_ms {
        log::warn!("[dhcp] lease expired! 地址作废,重新 DISCOVER");
        drop(st);
        reacquire();
        return;
    }
    let rebinding = now >= st.t2_ms;
    if !rebinding && now < st.t1_ms {
        return;
    }
    if st.awaiting_ack_since != 0 && now - st.awaiting_ack_since < RENEW_RETRY_MS {
        return; // 距上次发送不到重传间隔
    }
    // 续租 REQUEST:ciaddr=当前 IP,不带 requested-ip / server-id(RFC 2131 §4.3.6)
    let pkt = build_packet(st.xid, T_REQUEST, st.ip, None, None);
    if rebinding {
        log::info!("[dhcp] >>> REQUEST (rebinding, broadcast)");
        udp::send_broadcast(my_ip(), CLIENT_PORT, SERVER_PORT, &pkt);
    } else {
        let (a, b, c, d) = fmt_ip(st.server);
        log::info!("[dhcp] >>> REQUEST (renew, unicast to {}.{}.{}.{})", a, b, c, d);
        udp::send(st.server, CLIENT_PORT, SERVER_PORT, &pkt);
    }
    st.awaiting_ack_since = now;
}

/// 等一个特定类型的 DHCP 回复；期间周期性重发请求包
fn wait_reply(fd: usize, xid: u32, want: &[u8], resend: &dyn Fn()) -> Option<Reply> {
    let mut buf = [0u8; 1024];
    let mut spins = 0usize;
    loop {
        crate::drivers::net::poll();
        if let Some((_, n)) = udp::recvfrom(fd, &mut buf) {
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
        if spins >= 50_000_000 {        
            log::warn!("[dhcp] timeout, give up");
            return None;
        }
    }
}

fn ensure_socket() -> Option<usize> {
    {
        let st = LEASE.lock();
        if let Some(fd) = st.fd {
            return Some(fd);
        }
    }
    let fd = match socket::socket_create(2, 0) { // 2 = UDP
        Some(f) => f,
        None => {
            log::warn!("[dhcp] socket table full");
            return None;
        }
    };
    if !socket::socket_bind(fd, CLIENT_PORT) {
        log::warn!("[dhcp] bind 68 failed");
        let mut table = socket::SOCKET_TABLE.lock();
        socket::release_slot(&mut table, fd);
        return None;
    }
    LEASE.lock().fd = Some(fd);
    Some(fd)
}

/// 完整的 DISCOVER → OFFER → REQUEST → ACK 握手(阻塞,boot 与到期重握手共用)
pub fn handshake() {
    let fd = match ensure_socket() {
        Some(f) => f,
        None => return,
    };
    let xid = LEASE.lock().xid;

    let discover = build_packet(xid, T_DISCOVER, 0, None, None);
    log::info!("[dhcp] >>> DISCOVER");
    udp::send_broadcast(0, CLIENT_PORT, SERVER_PORT, &discover); // 无 IP 阶段:源 0.0.0.0

    let offer = match wait_reply(fd, xid, &[T_OFFER], &|| {
        udp::send_broadcast(0, CLIENT_PORT, SERVER_PORT, &build_packet(xid, T_DISCOVER, 0, None, None));
    }) {
        Some(o) => o,
        None => return,
    };
    let (a, b, c, d) = fmt_ip(offer.yiaddr);
    let (sa, sb, sc, sd) = fmt_ip(offer.server_id);
    log::info!("[dhcp] <<< OFFER: ip={}.{}.{}.{} server={}.{}.{}.{}", a, b, c, d, sa, sb, sc, sd);

    let request = build_packet(xid, T_REQUEST, 0, Some(offer.yiaddr), Some(offer.server_id));
    log::info!("[dhcp] >>> REQUEST");
    udp::send_broadcast(0, CLIENT_PORT, SERVER_PORT, &request);

    let ack = match wait_reply(fd, xid, &[T_ACK, T_NAK], &|| {
        udp::send_broadcast(0, CLIENT_PORT, SERVER_PORT, &request);
    }) {
        Some(r) => r,
        None => return,
    };
    if ack.msg_type == T_NAK {
        log::warn!("[dhcp] <<< NAK，服务器拒绝，放弃");
        return;
    }

    apply_lease(&ack);
    let (ga, gb, gc, gd) = fmt_ip(ack.router);
    let (na, nb, nc, nd) = fmt_ip(ack.dns);
    log::info!(
        "[dhcp] <<< ACK! leased {}.{}.{}.{}, gw={}.{}.{}.{}, dns={}.{}.{}.{}, lease={}s",
        a, b, c, d, ga, gb, gc, gd, na, nb, nc, nd, ack.lease
    );
    let (ma, mb, mc, md) = fmt_ip(my_ip());
    log::info!("[dhcp] my_ip() 现在 = {}.{}.{}.{}", ma, mb, mc, md);
}
