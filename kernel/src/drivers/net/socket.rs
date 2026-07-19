use alloc::collections::vec_deque::VecDeque;
use alloc::vec::Vec;
use crate::sync::spin::Mutex;
use crate::drivers::net::udp;
use crate::drivers::net::tcp::TcpSocket;
use crate::drivers::net::ip;

pub const SOCKET_TYPE_TCP: usize = 1;
pub const SOCKET_TYPE_UDP: usize = 2;
pub const SOCKET_TYPE_RAW: usize = 3;
pub const SOCKET_TABLE_SIZE: usize = 8;


#[derive(Clone, Copy, Debug, PartialEq, Eq)]   // ← 加了 PartialEq/Eq，TCP 四元组匹配要用
pub struct SocketAddr {
    pub ip: u32,
    pub port: u16,
}

pub struct UdpSocket {
    pub local_port: u16,
    pub remote: Option<SocketAddr>,
    pub rx_queue: VecDeque<Vec<u8>>,
}

impl UdpSocket {
    pub fn new(local_port: u16) -> Self {
        Self { local_port, remote: None, rx_queue: VecDeque::new() }
    }
}



/// RAW 原始套接字:用户给 ICMP 报文,内核负责套 IP 头
pub struct RawSocket {
    pub protocol: u8,                     
    pub remote: Option<u32>,               // 只收指定源的包
    pub rx_queue: VecDeque<Vec<u8>>,       // 帧格式: [src_ip(4B)] + ICMP 报文
}

impl RawSocket {
    pub fn new(protocol: u8) -> Self {
        Self { protocol, remote: None, rx_queue: VecDeque::new() }
    }
}

pub enum Socket {
    Udp(UdpSocket),
    Tcp(TcpSocket),
    Raw(RawSocket),
}

/// socket 表，固定 8 槽；stype: 1 = TCP(STREAM)，2 = UDP(DGRAM)
pub static SOCKET_TABLE: Mutex<[Option<Socket>; SOCKET_TABLE_SIZE]> = Mutex::new([
    None; SOCKET_TABLE_SIZE
]);

/// stype: 1 = TCP, 2 = UDP, 3 = RAW(protocol 目前只支持 1=ICMP)
pub fn socket_create(stype: usize, protocol: usize) -> Option<usize> {
    let mut table = SOCKET_TABLE.lock();
    for (i, slot) in table.iter_mut().enumerate() {
        if slot.is_none() {
            *slot = Some(match stype {
                SOCKET_TYPE_TCP => Socket::Tcp(TcpSocket::new()),
                SOCKET_TYPE_UDP => Socket::Udp(UdpSocket::new(0)),
                SOCKET_TYPE_RAW if protocol == 1 => Socket::Raw(RawSocket::new(1)),
                _ => return None,
            });
            return Some(i);
        }
    }
    None
}

pub fn socket_bind(fd: usize, port: u16) -> bool {
    let mut table = SOCKET_TABLE.lock();
    if fd >= table.len() || table[fd].is_none() {
        return false;
    }
    for s in table.iter().flatten() {
        let p = match s {
            Socket::Udp(u) => u.local_port,
            Socket::Tcp(t) => t.local_port,
            Socket::Raw(r) => r.local_port,
        };
        if p == port {
            return false;
        }
    }
    match &mut table[fd] {
        Some(Socket::Udp(u)) => { u.local_port = port; true }
        Some(Socket::Tcp(t)) => { t.local_port = port; true }
        Some(Socket::Raw(_)) => true,   // RAW 无端口,bind 视为成功 no-op
        _ => false,
    }
}
/// UDP 发送（TCP fd 传进来返回 false）
pub fn socket_sendto(fd: usize, dst: SocketAddr, data: &[u8]) -> bool {
    let table = SOCKET_TABLE.lock();
    match table.get(fd) {
        Some(Some(Socket::Udp(sock))) => {
            let src_port = sock.local_port;
            drop(table);
            udp::send(dst.ip, src_port, dst.port, data);
            true
        }
        Some(Some(Socket::Raw(sock))) => {
            let proto = sock.protocol;
            drop(table);
            ip::send(dst.ip, proto, data);   // 用户已组好 ICMP,内核只套 IP 头
            true
        }
        _ => false,
    }
}
/// UDP 接收，返回 (src, len)
pub fn socket_recvfrom(fd: usize, buf: &mut [u8]) -> Option<(SocketAddr, usize)> {
    let mut table = SOCKET_TABLE.lock();
    if let Some(Some(Socket::Raw(sock))) = table.get_mut(fd) {
        if let Some(frame) = sock.rx_queue.pop_front() {
            if frame.len() < 4 { return None; }
            let src_ip = u32::from_be_bytes([frame[0], frame[1], frame[2], frame[3]]);
            let data = &frame[4..];
            let len = data.len().min(buf.len());
            buf[..len].copy_from_slice(&data[..len]);
            return Some((SocketAddr { ip: src_ip, port: 0 }, len));
        }
    }
    None
}

/// 把收到的报文副本分发给所有匹配的 RAW socket(在协议层收包路径调用)
pub fn deliver_raw(protocol: u8, src_ip: u32, data: &[u8]) {
    let mut table = SOCKET_TABLE.lock();
    for slot in table.iter_mut().flatten() {
        let Socket::Raw(r) = slot else { continue };
        if r.protocol != protocol { continue; }
        if let Some(only) = r.remote {
            if only != src_ip { continue; }
        }
        let mut frame = Vec::with_capacity(4 + data.len());
        frame.extend_from_slice(&src_ip.to_be_bytes());
        frame.extend_from_slice(data);
        r.rx_queue.push_back(frame);
    }
}