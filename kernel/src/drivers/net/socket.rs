use alloc::collections::vec_deque::VecDeque;
use alloc::vec::Vec;
use crate::sync::spin::Mutex;
use crate::drivers::net::udp;

#[derive(Clone, Copy, Debug)]
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
        Self {
            local_port,
            remote: None,
            rx_queue: VecDeque::new(),
        }
    }
}

/// 简单 socket 表，固定 8 个槽位
pub static SOCKET_TABLE: Mutex<[Option<UdpSocket>; 8]> = Mutex::new([
    None, None, None, None, None, None, None, None,
]);

/// 创建 UDP socket，返回 fd（即表索引）
pub fn socket_create() -> Option<usize> {
    let mut table = SOCKET_TABLE.lock();
    for (i, slot) in table.iter_mut().enumerate() {
        if slot.is_none() {
            *slot = Some(UdpSocket::new(0)); // port 先为 0，bind 时设置
            return Some(i);
        }
    }
    None
}

/// 绑定本地端口
pub fn socket_bind(fd: usize, port: u16) -> bool {
    let mut table = SOCKET_TABLE.lock();
    
    // 先检查 fd 是否有效
    if fd >= table.len() || table[fd].is_none() {
        return false;
    }
    
    // 检查端口冲突（只读遍历）
    for other in table.iter().flatten() {
        if other.local_port == port {
            return false;
        }
    }
    
    // 上面遍历结束，不可变借用已释放，现在可以可变借用
    if let Some(ref mut sock) = table[fd] {
        sock.local_port = port;
        true
    } else {
        false
    }
}
/// 发送数据
pub fn socket_sendto(fd: usize, dst: SocketAddr, data: &[u8]) -> bool {
    let table = SOCKET_TABLE.lock();
    if let Some(Some(sock)) = table.get(fd) {
        let src_port = sock.local_port;
        drop(table); // 释放锁后再调用网络层（避免死锁）
        udp::send(dst.ip, src_port, dst.port, data);
        return true;
    }
    false
}

/// 接收数据，返回 (src_ip, src_port, data)
pub fn socket_recvfrom(fd: usize, buf: &mut [u8]) -> Option<(SocketAddr, usize)> {
    let mut table = SOCKET_TABLE.lock();
    if let Some(Some(sock)) = table.get_mut(fd) {
        if let Some(frame) = sock.rx_queue.pop_front() {
            if frame.len() < 10 {
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

/// 关闭 socket
pub fn socket_close(fd: usize) {
    let mut table = SOCKET_TABLE.lock();
    if let Some(slot) = table.get_mut(fd) {
        *slot = None;
    }
}

