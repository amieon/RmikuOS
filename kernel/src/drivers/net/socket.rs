use alloc::collections::vec_deque::VecDeque;
use alloc::vec::Vec;
use crate::sync::spin::Mutex;
use crate::drivers::net::udp;
use crate::drivers::net::tcp::TcpSocket;

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

pub enum Socket {
    Udp(UdpSocket),
    Tcp(TcpSocket),
}

/// socket 表，固定 8 槽；stype: 1 = TCP(STREAM)，2 = UDP(DGRAM)
pub static SOCKET_TABLE: Mutex<[Option<Socket>; 8]> = Mutex::new([
    None, None, None, None, None, None, None, None,
]);

pub fn socket_create(stype: usize) -> Option<usize> {
    let mut table = SOCKET_TABLE.lock();
    for (i, slot) in table.iter_mut().enumerate() {
        if slot.is_none() {
            *slot = Some(match stype {
                1 => Socket::Tcp(TcpSocket::new()),
                2 => Socket::Udp(UdpSocket::new(0)),
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
        };
        if p == port {
            return false;
        }
    }
    match &mut table[fd] {
        Some(Socket::Udp(u)) => { u.local_port = port; true }
        Some(Socket::Tcp(t)) => { t.local_port = port; true }
        _ => false,
    }
}

/// UDP 发送（TCP fd 传进来返回 false）
pub fn socket_sendto(fd: usize, dst: SocketAddr, data: &[u8]) -> bool {
    let table = SOCKET_TABLE.lock();
    if let Some(Some(Socket::Udp(sock))) = table.get(fd) {
        let src_port = sock.local_port;
        drop(table); // 释放锁后再进网络层
        udp::send(dst.ip, src_port, dst.port, data);
        return true;
    }
    false
}

/// UDP 接收，返回 (src, len)
pub fn socket_recvfrom(fd: usize, buf: &mut [u8]) -> Option<(SocketAddr, usize)> {
    let mut table = SOCKET_TABLE.lock();
    if let Some(Some(Socket::Udp(sock))) = table.get_mut(fd) {
        if let Some(frame) = sock.rx_queue.pop_front() {
            if frame.len() < 6 {
                return None;
            }
            let src_ip = u32::from_be_bytes([frame[0], frame[1], frame[2], frame[3]]);
            let src_port = u16::from_be_bytes([frame[4], frame[5]]);
            let data = &frame[6..];
            let len = data.len().min(buf.len());
            buf[..len].copy_from_slice(&data[..len]);
            return Some((SocketAddr { ip: src_ip, port: src_port }, len));
        }
    }
    None
}
