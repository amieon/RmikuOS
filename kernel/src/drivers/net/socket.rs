use alloc::collections::vec_deque::VecDeque;
use alloc::vec::Vec;
use crate::fs::stat::STAT_TYPE_SOCKET;
use crate::fs::{File, Stat};
use crate::sync::spin::Mutex;
use crate::drivers::net::{socket, tcp, udp};
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
    pub reuse_addr: bool,
}

impl UdpSocket {
    pub fn new(local_port: u16) -> Self {
        Self { local_port, remote: None, rx_queue: VecDeque::new(), reuse_addr: false }
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
        SOCKET_TYPE_TCP => Some(Socket::Tcp(TcpSocket::new())),
        SOCKET_TYPE_UDP => Some(Socket::Udp(UdpSocket::new(0))),
        SOCKET_TYPE_RAW if protocol == 1 => Some(Socket::Raw(RawSocket::new(1))),
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
    let reuse = match &table.slots[fd] {
        Some(Socket::Tcp(t)) => t.reuse_addr,
        Some(Socket::Udp(u)) => u.reuse_addr,
        _ => false,
    };
    let used = |p: u16| {
        table.slots.iter().flatten().any(|s| match s {
            Socket::Udp(u) => u.local_port == p,
            Socket::Tcp(t) => {
                t.local_port == p
                    && !(reuse && t.state == crate::drivers::net::tcp::TcpState::TimeWait)
            }
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

fn slot_kind(slot: usize) -> Option<SlotKind> {
    let table = SOCKET_TABLE.lock();
    match table.slots.get(slot) {
        Some(Some(Socket::Tcp(_))) => Some(SlotKind::Tcp),
        Some(Some(Socket::Udp(_))) => Some(SlotKind::Udp),
        Some(Some(Socket::Raw(_))) => Some(SlotKind::Raw),
        _ => None,
    }
}

/// read 语义附带:把刚收到的对端记为默认对端(教学语义,非 POSIX)。
fn udp_set_remote(slot: usize, addr: SocketAddr) {
    let mut table = SOCKET_TABLE.lock();
    if let Some(Some(Socket::Udp(u))) = table.slots.get_mut(slot) {
        u.remote = Some(addr);
    }
}

fn raw_set_remote(slot: usize, ip: u32) {
    let mut table = SOCKET_TABLE.lock();
    if let Some(Some(Socket::Raw(r))) = table.slots.get_mut(slot) {
        r.remote = Some(ip);
    }
}

/// write 的前置:读默认对端。未设置(None)时 write 返回 -1(类 ENOTCONN)。
fn udp_get_remote(slot: usize) -> Option<SocketAddr> {
    let table = SOCKET_TABLE.lock();
    match table.slots.get(slot) {
        Some(Some(Socket::Udp(u))) => u.remote,
        _ => None,
    }
}

fn raw_get_remote(slot: usize) -> Option<u32> {
    let table = SOCKET_TABLE.lock();
    match table.slots.get(slot) {
        Some(Some(Socket::Raw(r))) => r.remote,
        _ => None,
    }
}

/// fd 表中的 socket 表项:只持槽号,状态仍在全局 SOCKET_TABLE。
/// 进程 fd 生命周期与协议状态机生命周期由此解耦。
pub struct SocketFile {
    pub slot: usize,
}
enum SlotKind { Tcp, Udp, Raw }

impl File for SocketFile {
    fn readable(&self) -> bool { true }
    fn writable(&self) -> bool { true }

    fn stat(&self) -> Stat {
        Stat::new(STAT_TYPE_SOCKET, 0, 0o666, 0, 0)
    }

    fn read(&self, buf: &mut [u8]) -> isize {
        match slot_kind(self.slot) {
            Some(SlotKind::Tcp) => tcp::recv_data(self.slot, buf),
            Some(SlotKind::Udp) => match socket_recvfrom(self.slot, buf) {
                Some((addr, n)) => { udp_set_remote(self.slot, addr); n as isize }
                None => 0, 
            },
            Some(SlotKind::Raw) => match socket_recvfrom(self.slot, buf) {
                Some((addr, n)) => { raw_set_remote(self.slot, addr.ip); n as isize }
                None => 0,
            },
            None => -1,
        }
    }

    fn write(&self, buf: &[u8]) -> isize {
        match slot_kind(self.slot) {
            Some(SlotKind::Tcp) => tcp::send_data(self.slot, buf),
            Some(SlotKind::Udp) => match udp_get_remote(self.slot) {
                Some(remote) if socket_sendto(self.slot, remote, buf) => buf.len() as isize,
                _ => -1,  
            },
            Some(SlotKind::Raw) => match raw_get_remote(self.slot) {
                Some(ip) if socket_sendto(self.slot, SocketAddr { ip, port: 0 }, buf) => {
                    buf.len() as isize
                }
                _ => -1,
            },
            None => -1,
        }
    }
    fn socket_slot(&self) -> Option<usize> { Some(self.slot) }
}

impl Drop for SocketFile {
    fn drop(&mut self) {
        match slot_kind(self.slot) {
            Some(SlotKind::Tcp) => { tcp::close(self.slot); }
            Some(SlotKind::Udp) | Some(SlotKind::Raw) => {
                let mut table = SOCKET_TABLE.lock();
                release_slot(&mut table, self.slot);
            }
            None => {}
        }
    }
}


/// getsockname: 本端地址 = MY_IP + local_port(未绑定端口为 0)
pub fn socket_getsockname(slot: usize) -> Option<SocketAddr> {
    let table = SOCKET_TABLE.lock();
    let port = match table.slots.get(slot) {
        Some(Some(Socket::Tcp(t))) => t.local_port,
        Some(Some(Socket::Udp(u))) => u.local_port,
        Some(Some(Socket::Raw(_))) => 0,          // RAW 无端口
        _ => return None,
    };
    Some(SocketAddr { ip: crate::drivers::net::ip::my_ip(), port })
}

/// getpeername: 对端地址;TCP 取建连四元组,UDP/RAW 取 connect/read 记的 remote
pub fn socket_getpeername(slot: usize) -> Option<SocketAddr> {
    let table = SOCKET_TABLE.lock();
    match table.slots.get(slot) {
        Some(Some(Socket::Tcp(t))) => t.remote,
        Some(Some(Socket::Udp(u))) => u.remote,
        Some(Some(Socket::Raw(r))) => r.remote.map(|ip| SocketAddr { ip, port: 0 }),
        _ => None,
    }
}

