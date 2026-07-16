use crate::drivers::net::eth::{send as eth_send, MY_MAC};
use crate::drivers::net::virtio_net::VirtioNet;

#[repr(C, packed)]
struct ArpHeader {
    hw_type: u16,
    proto_type: u16,
    hw_len: u8,
    proto_len: u8,
    opcode: u16,
    sender_mac: [u8; 6],
    sender_ip: u32,
    target_mac: [u8; 6],
    target_ip: u32,
}

struct ArpEntry {
    ip: u32,
    mac: [u8; 6],
    valid: bool,
}

static mut CACHE: [ArpEntry; 8] = [
    ArpEntry { ip: 0, mac: [0; 6], valid: false },
    ArpEntry { ip: 0, mac: [0; 6], valid: false },
    ArpEntry { ip: 0, mac: [0; 6], valid: false },
    ArpEntry { ip: 0, mac: [0; 6], valid: false },
    ArpEntry { ip: 0, mac: [0; 6], valid: false },
    ArpEntry { ip: 0, mac: [0; 6], valid: false },
    ArpEntry { ip: 0, mac: [0; 6], valid: false },
    ArpEntry { ip: 0, mac: [0; 6], valid: false },
];

pub fn insert(ip: u32, mac: &[u8; 6]) {
    unsafe {
        for e in CACHE.iter_mut() {
            if !e.valid {
                e.ip = ip; e.mac.copy_from_slice(mac); e.valid = true;
                return;
            }
        }
        CACHE[0].ip = ip; CACHE[0].mac.copy_from_slice(mac);
    }
}

pub fn lookup(ip: u32, out: &mut [u8; 6]) -> bool {
    unsafe {
        for e in CACHE.iter() {
            if e.valid && e.ip == ip {
                out.copy_from_slice(&e.mac);
                return true;
            }
        }
    }
    false
}

pub fn input(packet: &[u8]) {
    if packet.len() < 28 { return; }
    let arp = unsafe { &*(packet.as_ptr() as *const ArpHeader) };
    let opcode = u16::from_be(arp.opcode);
    let sender_ip = u32::from_be(arp.sender_ip);
    let target_ip = u32::from_be(arp.target_ip);

    insert(sender_ip, &arp.sender_mac);

    if opcode == 1 && target_ip == super::ip::MY_IP {
        let mut reply = [0u8; 42];
        let eth = unsafe { &mut *(reply.as_mut_ptr() as *mut super::eth::EthHeader) };
        eth.dst.copy_from_slice(&arp.sender_mac);
        unsafe { eth.src.copy_from_slice(&MY_MAC); }
        eth.ethertype = 0x0806u16.to_be();

        let arp_r = unsafe { &mut *(reply.as_mut_ptr().add(14) as *mut ArpHeader) };
        arp_r.hw_type = 1u16.to_be();
        arp_r.proto_type = 0x0800u16.to_be();
        arp_r.hw_len = 6; arp_r.proto_len = 4;
        arp_r.opcode = 2u16.to_be();
        unsafe { arp_r.sender_mac.copy_from_slice(&MY_MAC); }
        arp_r.sender_ip = super::ip::MY_IP.to_be();
        arp_r.target_mac.copy_from_slice(&arp.sender_mac);
        arp_r.target_ip = sender_ip.to_be();

        unsafe {
            if let Some(ref mut net) = super::NET {
                net.send(&reply);
            }
        }
    }
}