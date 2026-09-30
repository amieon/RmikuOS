use crate::drivers::net::ip::{IpHeader, my_ip, checksum, send as ip_send};
use crate::drivers::net::socket::{self, SOCKET_TABLE, Socket, SocketAddr};
use crate::fs::{POLLIN, POLLNVAL, POLLOUT};
use alloc::vec::Vec;

#[repr(C, packed)]
pub struct UdpHeader {
    pub src_port: u16,
    pub dst_port: u16,
    pub len: u16,
    pub checksum: u16,
}

/// UDP 伪头部，用于校验和
#[repr(C, packed)]
struct UdpPseudoHeader {
    saddr: u32,
    daddr: u32,
    zero: u8,
    protocol: u8,
    udp_len: u16,
}

fn udp_checksum(src_ip: u32, dst_ip: u32, payload: &[u8]) -> u16 {
    let udp_len = payload.len() as u16;
    let pseudo = UdpPseudoHeader {
        saddr: src_ip.to_be(),
        daddr: dst_ip.to_be(),
        zero: 0,
        protocol: 17,
        udp_len: udp_len.to_be(),
    };
    let pseudo_bytes = unsafe {
        core::slice::from_raw_parts(
            &pseudo as *const _ as *const u8,
            core::mem::size_of::<UdpPseudoHeader>(),
        )
    };
    let mut sum: u32 = 0;
    for i in (0..pseudo_bytes.len()).step_by(2) {
        if i + 1 < pseudo_bytes.len() {
            sum += ((pseudo_bytes[i] as u32) << 8) | (pseudo_bytes[i + 1] as u32);
        } else {
            sum += (pseudo_bytes[i] as u32) << 8;
        }
    }
    for i in (0..payload.len()).step_by(2) {
        if i + 1 < payload.len() {
            sum += ((payload[i] as u32) << 8) | (payload[i + 1] as u32);
        } else {
            sum += (payload[i] as u32) << 8;
        }
    }
    while (sum >> 16) != 0 {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }
    
    !(sum as u16)
}

pub fn send(dst_ip: u32, src_port: u16, dst_port: u16, data: &[u8]) {
    let udp_len = core::mem::size_of::<UdpHeader>() + data.len();
    let mut pkt = alloc::vec::Vec::with_capacity(udp_len);
    unsafe { pkt.set_len(core::mem::size_of::<UdpHeader>()) };
    let hdr = unsafe { &mut *(pkt.as_mut_ptr() as *mut UdpHeader) };
    hdr.src_port = src_port.to_be();
    hdr.dst_port = dst_port.to_be();
    hdr.len = (udp_len as u16).to_be();
    hdr.checksum = 0;
    pkt.extend_from_slice(data);
    let len = pkt.len();
    let csum = udp_checksum(my_ip(), dst_ip, &pkt);
    unsafe {
        core::ptr::write_unaligned(core::ptr::addr_of_mut!((*hdr).checksum), csum.to_be());
    }
    

    ip_send(dst_ip, 17, &pkt);
    debug_assert_eq!(udp_checksum(my_ip(), dst_ip, &pkt), 0); 
}

pub fn input(packet: &[u8], src_ip: u32, dst_ip: u32) {
    if packet.len() < core::mem::size_of::<UdpHeader>() {
        return;
    }
    let hdr = unsafe { packet.as_ptr().cast::<UdpHeader>().read_unaligned() };
    let src_port = u16::from_be(hdr.src_port);
    let dst_port = u16::from_be(hdr.dst_port);
    let len = u16::from_be(hdr.len) as usize;
    if len < core::mem::size_of::<UdpHeader>() || len > packet.len() {
        return;
    }
    if hdr.checksum != 0 && udp_checksum(src_ip, dst_ip, &packet[..len]) != 0 {
        log::info!("[udp] bad checksum from {:#x}, drop", src_ip);
        return;
    }
    let data = &packet[core::mem::size_of::<UdpHeader>()..len];

    // 投递到 socket 接收队列
    let mut table = SOCKET_TABLE.lock();
    for s in table.slots.iter_mut().flatten() {
        if let Socket::Udp(sock) = s {
            if sock.local_port == dst_port {
                // connect() 过的 socket 只收默认对端的包(POSIX 接收过滤):
                // 来源不匹配就跳过,继续找下一个绑了同端口的 socket
                if let Some(r) = sock.remote {
                    if r.ip != src_ip || r.port != src_port {
                        continue;
                    }
                }
                if sock.rx_queue.len() < 64 {
                    // 保存 (src_ip, src_port, data)
                    let mut frame = Vec::with_capacity(8 + 2 + data.len());
                    frame.extend_from_slice(&src_ip.to_be_bytes());
                    frame.extend_from_slice(&src_port.to_be_bytes());
                    frame.extend_from_slice(data);
                    sock.rx_queue.push_back(frame);
                }
                break;
            }
        }
    }
    }
    
/// 通用 UDP 广播。src_ip 为广播时的源地址:
/// DHCP 无 IP 阶段传 0(0.0.0.0);已有 IP 的广播(如 mDNS/服务发现)传 my_ip()。
pub fn send_broadcast(src_ip: u32, src_port: u16, dst_port: u16, data: &[u8]) {
    let udp_len = core::mem::size_of::<UdpHeader>() + data.len();
    let mut pkt = alloc::vec::Vec::with_capacity(udp_len);
    unsafe { pkt.set_len(core::mem::size_of::<UdpHeader>()) };
    let hdr = unsafe { &mut *(pkt.as_mut_ptr() as *mut UdpHeader) };
    hdr.src_port = src_port.to_be();
    hdr.dst_port = dst_port.to_be();
    hdr.len = (udp_len as u16).to_be();
    hdr.checksum = 0;
    pkt.extend_from_slice(data);
    let csum = udp_checksum(src_ip, 0xFFFFFFFF, &pkt); // 伪头部源地址跟随调用方
    unsafe {
        core::ptr::write_unaligned(core::ptr::addr_of_mut!((*hdr).checksum), csum.to_be());
    }
    crate::drivers::net::ip::send_broadcast(src_ip, 17, &pkt);
}
/// ip==0 且 port==0(0.0.0.0:0) 表示断开,清掉 remote(对应 POSIX connect(AF_UNSPEC));
/// 允许重复调用改换对端(POSIX 允许 UDP socket 反复 connect);
/// 无握手、无网络流量,纯本地记账。成功 0,失败 -1(与 tcp::connect 对齐)。
pub fn connect(slot: usize, ip: u32, port: u16) -> isize {
    let mut table = SOCKET_TABLE.lock();
    // 先确认槽位确实是 UDP
    match table.slots.get(slot) {
        Some(Some(Socket::Udp(_))) => {}
        _ => return -1,
    }
    if ip == 0 && port == 0 {
        if let Some(Some(Socket::Udp(u))) = table.slots.get_mut(slot) {
            u.remote = None;
        }
        log::info!("[udp] slot={} disconnect (remote cleared)", slot);
        return 0;
    }
    if socket::ensure_udp_port(&mut table, slot).is_none() {
        return -1;
    }
    if let Some(Some(Socket::Udp(u))) = table.slots.get_mut(slot) {
        u.remote = Some(SocketAddr { ip, port });
        let lp = u.local_port;
        log::info!(
            "[udp] slot={} connect {}.{}.{}.{}:{}, local_port={}",
            slot,
            (ip >> 24) & 0xff, (ip >> 16) & 0xff, (ip >> 8) & 0xff, ip & 0xff,
            port, lp
        );
        0
    } else {
        -1
    }
}

/// sendto 语义:经 socket 表发送 UDP 数据报。
/// 未 bind 的 socket 先自动分配临时端口(POSIX autobind),
pub fn sendto(slot: usize, dst: SocketAddr, data: &[u8]) -> bool {
    let mut table = SOCKET_TABLE.lock();
    let src_port = match socket::ensure_udp_port(&mut table, slot) {
        Some(p) => p,
        None => return false,
    };
    drop(table);
    send(dst.ip, src_port, dst.port, data);
    true
}

/// send() 语义:向 connect 记下的默认对端发送(对应 tcp::send_data)。
/// 未 connect 返回 -1(类 EDESTADDRREQ)。
pub fn send_data(slot: usize, data: &[u8]) -> isize {
    let remote = {
        let table = SOCKET_TABLE.lock();
        match table.slots.get(slot) {
            Some(Some(Socket::Udp(u))) => u.remote,
            _ => None,
        }
    };
    match remote {
        Some(r) if r.port != 0 => {
            if sendto(slot, r, data) { data.len() as isize } else { -1 }
        }
        _ => -1,
    }
}

/// recvfrom 语义:弹一帧数据报并回传来源地址。
/// 帧格式 [src_ip(4B) + src_port(2B) + data] 由上面的 input() 定义,
/// 生产者/消费者同文件,改格式不用跨文件对账。
pub fn recvfrom(slot: usize, buf: &mut [u8]) -> Option<(SocketAddr, usize)> {
    let mut table = SOCKET_TABLE.lock();
    match table.slots.get_mut(slot) {
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
        _ => None,
    }
}

/// recv() 语义:与 recvfrom 同一数据源,只是不回填对端地址(对应 tcp::recv_data)。
/// 已 connect 的 socket 由 input() 负责来源过滤。返回 n / 0(超时) / -1。
pub fn recv_data(slot: usize, out: &mut [u8]) -> isize {
    let mut spins = 0usize;
    loop {
        crate::drivers::net::maybe_poll();
        if let Some((_src, n)) = recvfrom(slot, out) {
            return n as isize;
        }
        spins += 1;
        if spins > 50_000_000 {
            return 0; // 超时,语义同 recvfrom
        }
    }
}

/// poll 就绪:接收队列非空可读;UDP 无发送缓冲/窗口,恒可写。
pub fn poll_ready(slot: usize, events: i16) -> i16 {
    let table = SOCKET_TABLE.lock();
    match table.slots.get(slot) {
        Some(Some(Socket::Udp(sock))) => {
            let mut revents = 0;
            if events & POLLIN != 0 && !sock.rx_queue.is_empty() {
                revents |= POLLIN;
            }
            if events & POLLOUT != 0 {
                revents |= POLLOUT;
            }
            revents
        }
        _ => POLLNVAL,
    }
}
