use crate::println;

pub fn udp_kernel_test() {
    use crate::drivers::net::socket::*;

    let fd = socket_create().expect("socket_create");
    assert!(socket_bind(fd, 12345));

    let dst = SocketAddr { ip: 0x0A00_0202, port: 9999 };  // 10.0.2.2
    socket_sendto(fd, dst, b"hello from RmikuOS kernel");
    println!("[udp] sent, waiting reply...");

    let mut buf = [0u8; 512];
    for _ in 0..2_000_000u32 {
        crate::drivers::net::poll();
        if let Some((addr, n)) = socket_recvfrom(fd, &mut buf) {
            println!("[udp] recv {} bytes from {}.{}.{}.{}:{}",
                n,
                (addr.ip >> 24) & 0xFF, (addr.ip >> 16) & 0xFF,
                (addr.ip >> 8) & 0xFF, addr.ip & 0xFF,
                addr.port);
            println!("[udp] payload: {}", core::str::from_utf8(&buf[..n]).unwrap_or("<binary>"));
            return;
        }
        core::hint::spin_loop();
    }
    println!("[udp] no reply (timeout)");
}