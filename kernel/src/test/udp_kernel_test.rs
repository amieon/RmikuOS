use crate::{println, syscall};

pub fn udp_kernel_test() {
    // use crate::drivers::net::socket::{self, SocketAddr};
    // use crate::syscall::net::sys_net_socket;

    // // 对应之后的 SYS_SOCKET (198)
    // let fd = sys_net_socket(0).expect("[udp] socket table full");
    // // 对应 SYS_BIND (200)
    // if !socket::socket_bind(fd, 12345) {
    //     panic!("[udp] bind 12345 failed");
    // }

    // let dst = SocketAddr {
    //     ip: u32::from_be_bytes([10, 0, 2, 2]), // 10.0.2.2，slirp 网关 = host
    //     port: 9999,
    // };
    // let msg = b"hello from RmikuOS kernel";
    // // 对应 SYS_SENDTO (206)
    // socket::socket_sendto(fd, dst, msg);
    // println!("[udp] sent {} bytes, waiting reply...", msg.len());

    // let mut buf = [0u8; 2048];
    // let mut spins = 0usize;
    // loop {
    //     crate::drivers::net::poll();
    //     // 对应 SYS_RECVFROM (207)
    //     if let Some((src, len)) = socket::socket_recvfrom(fd, &mut buf) {
    //         let a = src.ip.to_be_bytes();
    //         println!(
    //             "[udp] got {} bytes from {}.{}.{}.{}:{}",
    //             len, a[0], a[1], a[2], a[3], src.port
    //         );
    //         match core::str::from_utf8(&buf[..len]) {
    //             Ok(s) => println!("[udp] payload: {:?}", s),
    //             Err(_) => println!("[udp] payload: <binary {} bytes>", len),
    //         }
    //         break;
    //     }
    //     spins += 1;
    //     if spins % 5_000_000 == 0 {
    //         println!("[udp] still waiting... (type a line + Enter in host nc)");
    //         socket::socket_sendto(fd, dst, msg); // 定期补发，双保险
    //     }
    // }

    // syscall::net::sys_net_close(fd);
    println!("[udp] test done");
}