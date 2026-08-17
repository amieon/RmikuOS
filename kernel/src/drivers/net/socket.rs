use alloc::collections::vec_deque::VecDeque;
use alloc::vec::Vec;
use crate::sync::spin::Mutex;
use crate::drivers::net::udp;
use crate::drivers::net::tcp::TcpSocket;
use crate::drivers::net::ip;

pub const SOCKET_TYPE_TCP: usize = 1;
pub const SOCKET_TYPE_UDP: usize = 2;
pub const SOCKET_TYPE_RAW: usize = 3;
/// socket 表上限:满了返回失败而不是无限增长(对应 Linux ulimit 语义)
pub const MAX_FD: usize = 4096;

/// socket 表:动态增长 + 空闲列表(free list)复用。
/// fd 即 slots 下标;close 的槽位进 free 队,下次 create 优先复用(低值 fd 保持紧凑)。
pub struct SocketTable {
    pub slots: Vec<Option<Socket>>,
    pub free: VecDeque<usize>,
}

impl SocketTable {
    pub const fn new() -> Self {
        Self { slots: Vec::new(), free: VecDeque::new() }
    }
}

/// 分配一个槽位:free list 有货先复用,否则扩容;超 MAX_FD 返回 None。
pub fn alloc_slot(table: &mut SocketTable) -> Option<usize> {
    if let Some(fd) = table.free.pop_front() {
        return Some(fd);
    }
    if table.slots.len() >= MAX_FD {
        return None;
    }
    table.slots.push(None);
    Some(table.slots.len() - 1)
}

/// 释放一个槽位:清空 + 归还 free list。不 shrink,槽位留给下次复用。
pub fn release_slot(table: &mut SocketTable, fd: usize) {
    if fd < table.slots.len() {
        table.slots[fd] = None;
        table.free.push_back(fd);
    }
}


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

/// socket 表,动态 + free list;fd 即 slots 下标
pub static SOCKET_TABLE: Mutex<SocketTable> = Mutex::new(SocketTable::new());

/// stype: 1 = TCP, 2 = UDP, 3 = RAW(protocol 目前只支持 1=ICMP)
pub fn socket_create(stype: usize, protocol: usize) -> Option<usize> {
    let mut table = SOCKET_TABLE.lock();
    let fd = alloc_slot(&mut table)?;
    table.slots[fd] = match stype {
        SOCKET_TYPE_TCP => Socket::Tcp(TcpSocket::new()),
        SOCKET_TYPE_UDP => Socket::Udp(UdpSocket::new(0)),
        SOCKET_TYPE_RAW if protocol == 1 => Socket::Raw(RawSocket::new(1)),
        _ => {
            release_slot(&mut table, fd); // 类型不支持,退还槽位
            return None;
        }
    };
    Some(fd)
}

pub fn socket_bind(fd: usize, port: u16) -> bool {
    let mut table = SOCKET_TABLE.lock();
    if fd >= table.slots.len() || table.slots[fd].is_none() {
        return false;
    }
    let used = |p: u16| {
        table.slots.iter().flatten().any(|s| match s {
            Socket::Udp(u) => u.local_port == p,
            Socket::Tcp(t) => t.local_port == p,
            Socket::Raw(_) => false,
        })
    };
    /* 端口 0 = 请求内核自动分配（POSIX bind 语义）。
     * 注意不能直接赋 0: 未绑定 socket 的默认 local_port 也是 0, 会撞冲突检查。 */
    let actual = if port == 0 {
        let mut p = 20000u16;
        while p < 65535 && used(p) {
            p += 1;
        }
        p
    } else {
        if used(port) {
            return false;
        }
        port
    };
    match &mut table.slots[fd] {
        Some(Socket::Udp(u)) => { u.local_port = actual; true }
        Some(Socket::Tcp(t)) => { t.local_port = actual; true }
        Some(Socket::Raw(_)) => true,   // RAW 无端口,bind 视为成功 no-op
        _ => false,
    }
}
/// UDP 发送（TCP fd 传进来返回 false）
pub fn socket_sendto(fd: usize, dst: SocketAddr, data: &[u8]) -> bool {
    let table = SOCKET_TABLE.lock();
    match table.slots.get(fd) {
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
/// 接收,返回 (src, len);UDP 帧头 6 字节(ip+port),RAW 帧头 4 字节(仅 ip)
pub fn socket_recvfrom(fd: usize, buf: &mut [u8]) -> Option<(SocketAddr, usize)> {
    let mut table = SOCKET_TABLE.lock();
    match table.slots.get_mut(fd) {
        Some(Some(Socket::Udp(sock))) => {
            let frame = sock.rx_queue.pop_front()?;
            if frame.len() < 6 {
                return None;
            }
            let src_ip = u32::from_be_bytes([frame[0], frame[1], frame[2], frame[3]]);
            let src_port = u16::from_be_bytes([frame[4], frame[5]]);
            let data = &frame[6..];
            let len = data.len().min(buf.len());
            buf[..len].copy_from_slice(&data[..len]);
            Some((SocketAddr { ip: src_ip, port: src_port }, len))
        }
        Some(Some(Socket::Raw(sock))) => {
            let frame = sock.rx_queue.pop_front()?;
            if frame.len() < 4 {
                return None;
            }
            let src_ip = u32::from_be_bytes([frame[0], frame[1], frame[2], frame[3]]);
            let data = &frame[4..];
            let len = data.len().min(buf.len());
            buf[..len].copy_from_slice(&data[..len]);
            Some((SocketAddr { ip: src_ip, port: 0 }, len))
        }
        _ => None,
    }
}
/// 把收到的报文副本分发给所有匹配的 RAW socket(在协议层收包路径调用)
pub fn deliver_raw(protocol: u8, src_ip: u32, data: &[u8]) {
    let mut table = SOCKET_TABLE.lock();
    for slot in table.slots.iter_mut().flatten() {
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