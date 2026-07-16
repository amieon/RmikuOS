use crate::drivers::net::ip::{send as ip_send, MY_IP};

#[repr(C, packed)]
struct IcmpHeader {
    typ: u8,
    code: u8,
    checksum: u16,
    id: u16,
    seq: u16,
}

pub fn input(packet: &[u8], src_ip: u32) {
    if packet.len() < core::mem::size_of::<IcmpHeader>() { return; }
    let icmp = unsafe { &*(packet.as_ptr() as *const IcmpHeader) };

    if icmp.typ == 8 { // Echo Request
        let mut reply = alloc::vec::Vec::with_capacity(packet.len());
        reply.extend_from_slice(packet);
        let ricmp = unsafe { &mut *(reply.as_mut_ptr() as *mut IcmpHeader) };
        ricmp.typ = 0; ricmp.code = 0; ricmp.checksum = 0;
        ricmp.checksum = super::ip::checksum(&reply);

        unsafe {
            if let Some(ref mut net) = super::NET {
                ip_send(net, src_ip, 1, &reply);
            }
        }
    }
}