use crate::drivers::net::ip::{IpHeader, my_ip, checksum, send as ip_send};
use crate::drivers::net::socket::{SOCKET_TABLE, Socket, SocketAddr};
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
    let data = &packet[core::mem::size_of::<UdpHeader>()..len];

    // 投递到 socket 接收队列
    let mut table = SOCKET_TABLE.lock();
    for s in table.iter_mut().flatten() {
        if let Socket::Udp(sock) = s {
            if sock.local_port == dst_port {
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