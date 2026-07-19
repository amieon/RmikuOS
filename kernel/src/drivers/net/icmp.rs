use crate::drivers::net::ip::{send as ip_send, MY_IP};
use crate::drivers::net::with_net;

#[repr(C, packed)]
struct IcmpHeader {
    typ: u8,
    code: u8,
    checksum: u16,
    id: u16,
    seq: u16,
}

pub fn input(packet: &[u8], src_ip: u32) {
    crate::socket::deliver_raw(1, src_ip, packet);
    if packet.len() < core::mem::size_of::<IcmpHeader>() {
        return;
    }
    let icmp = unsafe { packet.as_ptr().cast::<IcmpHeader>().read_unaligned() };

    if icmp.typ == 8 {
        // Echo Request -> Reply
        let mut reply = alloc::vec::Vec::with_capacity(packet.len());
        reply.extend_from_slice(packet);
        let ricmp = unsafe { &mut *(reply.as_mut_ptr() as *mut IcmpHeader) };
        ricmp.typ = 0; // Echo Reply
        ricmp.code = 0;
        ricmp.checksum = 0;
        ricmp.checksum = super::ip::checksum(&reply);

        ip_send(src_ip, 1, &reply);
    }
}