use crate::drivers::net::socket::{self, SocketAddr};
use crate::drivers::net::tcp;
use crate::task::{read_current_user_bytes, write_current_user_bytes};


pub fn sys_net_socket(stype: usize,protocol:usize) -> isize {
    match socket::socket_create(stype,protocol) {
        Some(fd) => fd as isize,
        None => -1,
    }
}

/// bind(fd, port) -> 0 / -1
pub fn sys_net_bind(fd: usize, port: usize) -> isize {
    if socket::socket_bind(fd, port as u16) { 0 } else { -1 }
}

/// sendto(fd, buf, len, ip, port) -> 发送字节数 / -1
pub fn sys_net_sendto(fd: usize, buf: usize, len: usize, ip: usize, port: usize) -> isize {
    if len == 0 || len > 1472 {
        return -1; // 1472 = 1500 - 20(IP) - 8(UDP)，超了要分片，教学版不做
    }
    let data = match read_current_user_bytes(buf, len) {
        Some(d) if d.len() == len => d,
        _ => return -1,
    };
    let dst = SocketAddr { ip: ip as u32, port: port as u16 };
    if socket::socket_sendto(fd, dst, &data) {
        len as isize
    } else {
        -1
    }
}

/// recvfrom(fd, buf, maxlen, info) -> 实际长度 / 0(超时) / -1
pub fn sys_net_recvfrom(fd: usize, buf: usize, maxlen: usize, info: usize) -> isize {
    let cap = maxlen.min(2048);
    let mut kbuf = alloc::vec![0u8; cap];
    let mut spins = 0usize;
    loop {
        crate::drivers::net::poll();
        if let Some((src, n)) = socket::socket_recvfrom(fd, &mut kbuf) {
            if write_current_user_bytes(buf, &kbuf[..n]).is_none() {
                return -1;
            }
            if info != 0 {
                let mut raw = [0u8; 8];
                raw[0..4].copy_from_slice(&src.ip.to_ne_bytes());    // was: to_be_bytes
                raw[4..6].copy_from_slice(&src.port.to_ne_bytes());  // was: to_be_bytes
                if write_current_user_bytes(info, &raw).is_none() {
                    return -1;
                }
            }
            return n as isize;
        }
        spins += 1;
        if spins > 50_000_000 {
            return 0; // 超时不是错误，用户态自己决定重试
        }
    }
}


pub fn sys_net_connect(fd: usize, ip: usize, port: usize) -> isize {
    tcp::connect(fd, ip as u32, port as u16)
}

pub fn sys_net_listen(fd: usize, _backlog: usize) -> isize {
    tcp::listen(fd)
}

/// accept(fd, info) -> child_fd / -1；info 同 recvfrom 的 8 字节格式
pub fn sys_net_accept(fd: usize, info: usize) -> isize {
    match tcp::accept(fd) {
        Some((child, remote)) => {
            if info != 0 {
                let mut raw = [0u8; 8];
                raw[0..4].copy_from_slice(&remote.ip.to_ne_bytes());    // was: to_be_bytes
                raw[4..6].copy_from_slice(&remote.port.to_ne_bytes());  // was: to_be_bytes
                if write_current_user_bytes(info, &raw).is_none() {
                    return -1;
                }
            }
            child as isize
        }
        None => -1,
    }
}

pub fn sys_net_send(fd: usize, buf: usize, len: usize) -> isize {
    if len == 0 || len > 1460 {
        return -1;
    }
    let data = match read_current_user_bytes(buf, len) {
        Some(d) if d.len() == len => d,
        _ => return -1,
    };
    tcp::send_data(fd, &data)
}

/// recv 返回 n / 0(EOF) / -1
pub fn sys_net_recv(fd: usize, buf: usize, maxlen: usize) -> isize {
    let mut kbuf = alloc::vec![0u8; maxlen.min(2048)];
    let n = tcp::recv_data(fd, &mut kbuf);
    if n <= 0 {
        return n;
    }
    if write_current_user_bytes(buf, &kbuf[..n as usize]).is_none() {
        return -1;
    }
    n
}


pub fn sys_net_close(fd: usize) -> isize {
    let is_tcp = {
        let table = socket::SOCKET_TABLE.lock();
        matches!(table.get(fd), Some(Some(socket::Socket::Tcp(_))))
    };
    if is_tcp {
        tcp::close(fd)
    } else {
        let mut table = socket::SOCKET_TABLE.lock();
        match table.get_mut(fd) {
            Some(slot) if slot.is_some() => { *slot = None; 0 }
            _ => -1,
        }
    }
}