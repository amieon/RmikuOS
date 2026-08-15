//DNS 客户端（RFC 1035 教学子集）：A 记录查询
//复用 UDP socket 层，向 DNS 服务器发查询、解析响应中的 IPv4 地址。
//不做缓存、不做 IPv6(AAAA)、不做 CNAME 展开——只取响应里的第一条 A 记录。
use alloc::string::String;
use alloc::vec::Vec;
use core::sync::atomic::{AtomicU16, AtomicU32, Ordering};

use crate::drivers::net::socket::{self, SocketAddr, SOCKET_TABLE};

/// DNS 服务器地址，默认 slirp 内置 DNS(10.0.2.3)；DHCP 租约落地后 set_dns_server 热切换。
static DNS_SERVER: AtomicU32 = AtomicU32::new(0x0A00_0203);

pub fn dns_server() -> u32 {
    DNS_SERVER.load(Ordering::Relaxed)
}
pub fn set_dns_server(ip: u32) {
    DNS_SERVER.store(ip, Ordering::Relaxed);
}

const DNS_PORT: u16 = 53;

/// 每个查询分配一个递增的 transaction ID，响应按此匹配。
static QUERY_ID: AtomicU16 = AtomicU16::new(1);
fn next_id() -> u16 {
    QUERY_ID.fetch_add(1, Ordering::Relaxed)
}

fn fmt_ip(ip: u32) -> (u32, u32, u32, u32) {
    ((ip >> 24) & 0xff, (ip >> 16) & 0xff, (ip >> 8) & 0xff, ip & 0xff)
}

/// 构造查询报文：12 字节头 + QNAME(逐 label 编码) + QTYPE(A) + QCLASS(IN)
fn build_query(id: u16, name: &str) -> Vec<u8> {
    let mut pkt = Vec::with_capacity(12 + name.len() + 2 + 4);
    pkt.extend_from_slice(&id.to_be_bytes());         // ID
    pkt.extend_from_slice(&0x0100u16.to_be_bytes());  // flags: RD=1(期望递归)
    pkt.extend_from_slice(&1u16.to_be_bytes());       // QDCOUNT = 1
    pkt.extend_from_slice(&0u16.to_be_bytes());       // ANCOUNT
    pkt.extend_from_slice(&0u16.to_be_bytes());       // NSCOUNT
    pkt.extend_from_slice(&0u16.to_be_bytes());       // ARCOUNT
    for label in name.split('.') {
        if label.is_empty() || label.len() > 63 {
            continue; // 教学版跳过非法 label
        }
        pkt.push(label.len() as u8);
        pkt.extend_from_slice(label.as_bytes());
    }
    pkt.push(0); // 名字结束
    pkt.extend_from_slice(&1u16.to_be_bytes()); // QTYPE = A
    pkt.extend_from_slice(&1u16.to_be_bytes()); // QCLASS = IN
    pkt
}

/// 解析报文里的域名（处理压缩指针 0xC0）。
/// 返回「下一次该读的偏移」：若经历过指针跳转，返回第一次跳转之后的位置；否则返回名字结束位置。
fn parse_name(data: &[u8], mut off: usize, out: &mut String) -> Option<usize> {
    let mut jumped = false;
    let mut end = off;
    let mut jumps = 0usize;
    loop {
        if off >= data.len() {
            return None;
        }
        let len = data[off];
        if len & 0xC0 == 0xC0 {
            // 压缩指针：后 14 位是报文内偏移
            if off + 1 >= data.len() {
                return None;
            }
            let target = (((len & 0x3F) as usize) << 8) | (data[off + 1] as usize);
            if !jumped {
                end = off + 2; // 记下返回点，供调用方继续读后续字段
            }
            jumped = true;
            off = target;
            jumps += 1;
            if jumps > 8 {
                return None; // 防指针环
            }
            continue;
        }
        if len == 0 {
            off += 1;
            break;
        }
        if len > 63 {
            return None; // 0x40-0xBF 是 RFC 1035 的保留/扩展值，非指针非 label，非法
        }
        if off + 1 + len as usize > data.len() {
            return None;
        }
        if !out.is_empty() {
            out.push('.');
        }
        for &b in &data[off + 1..off + 1 + len as usize] {
            out.push(b as char);
        }
        off += 1 + len as usize;
    }
    Some(if jumped { end } else { off })
}

/// 响应解析结果。
enum Reply {
    Answer(u32), // 拿到 A 记录
    Nx(u8),      // 明确失败:rcode 非 0(NXDOMAIN/SERVFAIL)或 rcode=0 但无 A 记录,别再重发
    Truncated,   // TC=1:响应超 512B 被截断,教学版 UDP-only 不支持 TCP 重试
    Skip,        // 与本查询无关的包(ID 不匹配/非响应),继续等
}

/// 解析响应，校验 transaction ID 与 rcode，提取第一条 A 记录的 IPv4。
/// rcode 语义:0=NoError 1=FormErr 2=ServFail 3=NXDomain 4=NotImp 5=Refused
fn parse_response(data: &[u8], id: u16) -> Reply {
    if data.len() < 12 {
        return Reply::Skip;
    }
    if u16::from_be_bytes([data[0], data[1]]) != id {
        return Reply::Skip;
    }
    let flags = u16::from_be_bytes([data[2], data[3]]);
    if flags & 0x8000 == 0 {
        return Reply::Skip; // QR=0，不是响应
    }
    let rcode = flags & 0xF;
    if rcode != 0 {
        return Reply::Nx(rcode as u8); // 服务器已明确答复(如 NXDOMAIN)，立即失败
    }
    if flags & 0x0200 != 0 {
        return Reply::Truncated; // TC=1:UDP 响应被截断
    }
    let qdcount = u16::from_be_bytes([data[4], data[5]]) as usize;
    let ancount = u16::from_be_bytes([data[6], data[7]]) as usize;

    let mut off = 12;
    // 跳过 question 段（QNAME + QTYPE + QCLASS）
    let mut _tmp = String::new();
    for _ in 0..qdcount {
        off = match parse_name(data, off, &mut _tmp) {
            Some(o) => o,
            None => return Reply::Skip,
        };
        if off + 4 > data.len() {
            return Reply::Skip;
        }
        off += 4;
    }
    // 遍历 answer 段，找 A 记录
    for _ in 0..ancount {
        let mut _name = String::new();
        off = match parse_name(data, off, &mut _name) {
            Some(o) => o,
            None => return Reply::Skip,
        };
        if off + 10 > data.len() {
            return Reply::Skip;
        }
        let atype = u16::from_be_bytes([data[off], data[off + 1]]);
        let aclass = u16::from_be_bytes([data[off + 2], data[off + 3]]);
        // data[off+4..off+8] 是 TTL，跳过
        let rdlen = u16::from_be_bytes([data[off + 8], data[off + 9]]) as usize;
        off += 10;
        if off + rdlen > data.len() {
            return Reply::Skip;
        }
        if atype == 1 && aclass == 1 && rdlen == 4 {
            return Reply::Answer(u32::from_be_bytes([
                data[off],
                data[off + 1],
                data[off + 2],
                data[off + 3],
            ]));
        }
        off += rdlen;
    }
    Reply::Nx(0) // rcode=0 但没有 A 记录(如只有 CNAME/AAAA)
}

/// 释放一个 UDP 临时 socket（内核态直接操作 socket 表，无 fd 表可走）
fn close_socket(fd: usize) {
    let mut table = SOCKET_TABLE.lock();
    if fd < table.len() {
        table[fd] = None;
    }
}

/// 解析域名，返回主机序 IPv4；失败返回 None。
/// 阻塞式：内部 poll 驱动收包，超时重发，最多 4 次发送后放弃。
pub fn resolve(name: &str) -> Option<u32> {
    if name.is_empty() || name.len() > 253 {
        return None;
    }
    let server = dns_server();
    if server == 0 {
        log::warn!("[dns] no DNS server configured, skip '{}'", name);
        return None;
    }
    let fd = socket::socket_create(socket::SOCKET_TYPE_UDP, 0)?;
    if !socket::socket_bind(fd, 0) {
        close_socket(fd); // 端口 0 = 自动分配，失败即释放
        return None;
    }

    let id = next_id();
    let query = build_query(id, name);
    let (a, b, c, d) = fmt_ip(server);
    log::info!("[dns] >>> query '{}' -> dns={}.{}.{}.{}", name, a, b, c, d);

    let mut buf = [0u8; 512];
    let mut spins = 0usize;
    let mut sent = 1u32;
    socket::socket_sendto(fd, SocketAddr { ip: server, port: DNS_PORT }, &query);

    let result = loop {
        crate::drivers::net::poll();
        if let Some((src, n)) = socket::socket_recvfrom(fd, &mut buf) {
            // 只认 DNS 服务器发回的响应，其余来源的包忽略
            if src.ip == server && src.port == DNS_PORT {
                match parse_response(&buf[..n], id) {
                    Reply::Answer(ip) => break Some(ip),
                    Reply::Nx(rc) => {
                        log::warn!("[dns] '{}' resolve failed, rcode={}", name, rc);
                        break None;
                    }
                    Reply::Truncated => {
                        log::warn!("[dns] '{}' response truncated (>512B)", name);
                        break None;
                    }
                    Reply::Skip => {}
                }
            }
        }
        spins += 1;
        if spins % 5_000_000 == 0 && sent < 4 {
            log::info!("[dns] resend #{} for '{}'", sent, name);
            socket::socket_sendto(fd, SocketAddr { ip: server, port: DNS_PORT }, &query);
            sent += 1;
        }
        if spins >= 20_000_000 {
            log::warn!("[dns] timeout, give up on '{}'", name);
            break None;
        }
    };

    close_socket(fd);
    result
}