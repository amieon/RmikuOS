use alloc::string::ToString;
use alloc::sync::Arc;
use alloc::vec::Vec;

use crate::drivers::net::socket::{self, SOCKET_TABLE, SocketAddr, SocketFile, release_slot};
use crate::drivers::net::socket::SlotKind;
use crate::drivers::net::{tcp, udp};
use crate::task::{get_fd_flags_current, read_current_user_bytes, write_current_user_bytes};
use super::errno::{EADDRINUSE, EAFNOSUPPORT, EAGAIN, EBADF, EDESTADDRREQ, EFAULT,
                   EINVAL, EMFILE, ENOPROTOOPT, ENOTCONN, ENOTSOCK, EMSGSIZE,
                   EOPNOTSUPP};

/// fd -> socket 槽位。失败返回 -errno（**不是裸 -1**）。
///
/// 裸 -1 在用户态被 rk_sys_ret 翻成 -EPERM（=1），于是所有 socket 错误都
/// 报"Operation not permitted"。libcurl 的状态机靠 errno 分流
/// （EINPROGRESS/ECONNREFUSED/EAGAIN），拿到 EPERM 只会中止并报一条
/// 与真实原因毫无关系的错误。
fn get_slot_from_fd(fd: usize) -> isize {
    let file = match crate::task::current_file(fd) {
        Some(f) => f,
        None => return -EBADF,
    };
    match file.socket_slot() {
        /* 槽位 0 是合法的，别写 `slot < 0` 那种死判断（usize 永远不会小于 0）。 */
        Some(slot) => slot as isize,
        None => -ENOTSOCK,
    }
}

/// 这个 fd 带 O_NONBLOCK 吗。
///
/// `fcntl(F_SETFL, O_NONBLOCK)` 只把位记在 process.fd_flags 里，**socket 层
/// 自己不来问就一点用都没有** —— 这正是之前那个"CONNECTTIMEOUT 形同虚设"的
/// 根因：flag 记下了，connect/recv/send 三处全都不看，全都按阻塞跑。
fn nonblock_flag(fd: usize) -> bool {
    (crate::task::get_fd_flags_current(fd) & crate::fs::O_NONBLOCK) != 0
}

pub fn sys_net_socket(stype: usize, protocol: usize) -> isize {
    match socket::socket_create(stype, protocol) {
        Some(slot) => {
            let fd = crate::task::alloc_fd_current(Arc::new(SocketFile { slot }));
            if fd < 0 {
                let mut table = SOCKET_TABLE.lock();
                release_slot(&mut table, slot);
                return -EMFILE;
            }
            fd
        }
        // socket_create 返回 None 只有两种可能：type 不认识 / fd 表满了。
        // 前者占绝大多数，统一按 EAFNOSUPPORT 报（POSIX 允许这么糊）。
        None => -EAFNOSUPPORT,
    }
}

/// bind(fd, port) -> 0 / -errno
pub fn sys_net_bind(fd: usize, port: usize) -> isize {
    let slot = get_slot_from_fd(fd);
    if slot < 0 { return slot; }
    if socket::socket_bind(slot as usize, port as u16) { 0 } else { -EADDRINUSE }
}

/// sendto(fd, buf, len, ip, port) -> 发送字节数 / -errno
pub fn sys_net_sendto(fd: usize, buf: usize, len: usize, ip: usize, port: usize) -> isize {
    let slot = get_slot_from_fd(fd);
    if slot < 0 { return slot; }
    if len == 0 {
        return 0;
    }
    if len > 1472 {
        // 1500 - 20(IP) - 8(UDP)，超了要分片，教学版不做
        return -EMSGSIZE;
    }
    let data = match read_current_user_bytes(buf, len) {
        Some(d) if d.len() == len => d,
        _ => return -EFAULT,
    };
    let dst = SocketAddr { ip: ip as u32, port: port as u16 };
    let slot = slot as usize;
    let ok = match socket::slot_kind(slot) {
        Some(SlotKind::Udp) => udp::sendto(slot, dst, &data),
        Some(SlotKind::Raw) => socket::socket_raw_sendto(slot, dst, &data),
        _ => return -EOPNOTSUPP,
    };
    if ok { len as isize } else { -EDESTADDRREQ }
}

/// recv/recvfrom 的 flags 解码。返回 Ok(peek) 或 Err(errno)。
///
/// 只认 MSG_PEEK，别的一位都不接受：`MSG_DONTWAIT` 若被静默忽略，
/// 调用方以为拿到了 EAGAIN，实际却在这里阻塞到超时返回（在 recv 语义里
/// 0 等于对端关闭），错误会表现成"连接莫名断开"。宁可让它一开始就失败。
fn recv_flags(flags: usize) -> Result<bool, isize> {
    if flags & !socket::MSG_PEEK != 0 {
        return Err(EOPNOTSUPP);
    }
    Ok(flags & socket::MSG_PEEK != 0)
}

/// recvfrom(fd, buf, maxlen, info, flags) -> 实际长度 / -EAGAIN / -errno
///
/// 超时按**时间**算，不按自旋圈数。原来那个 `spins > 50_000_000` 有两层错：
///   ① 圈数根本不等价于时间 —— 同一段代码在 TCG 上是几十秒，在真机上可能是
///      几毫秒，一个常数不可能同时对上；
///   ② 等的时候是**空转**，一个 vCPU 被 100% 占满。
/// 改成睡一个 tick 再回来看，等待期间 CPU 占用接近 0。
///
/// 等不到数据返回 -EAGAIN 而**不是 0**：recvfrom 里 0 的意思是"收到一个长度
/// 为 0 的报文"，跟"这次没收到"是两件事。原来返回 0 会让调用方把超时当成
/// 空包处理，UDP 客户端会陷入"永远收到空包"的循环却查不出原因。

pub fn sys_net_recvfrom(fd: usize, buf: usize, maxlen: usize, info: usize, flags: usize) -> isize {
    let peek = match recv_flags(flags) {
        Ok(p) => p,
        Err(e) => return -e,
    };
    let slot = get_slot_from_fd(fd);
    if slot < 0 { return slot; }
    let slot = slot as usize;

    /* POSIX 允许在 TCP socket 上调 recvfrom（等同 recv，src 填对端）。
     * 语义完全相同，直接转过去，不重复实现一遍。 */
    if matches!(socket::slot_kind(slot), Some(SlotKind::Tcp)) {
        return sys_net_recv(fd, buf, maxlen, flags);
    }

    let nonblock = nonblock_flag(fd);
    let cap = maxlen.min(2048);
    let mut kbuf = alloc::vec![0u8; cap];
    /* 2 秒：本地回环是毫秒级，局域网一个来回也远不到；再长没意义，
     * 用户态自己还有重试。 */
    let deadline = crate::timer::monotonic_us() + 2_000_000;
    loop {
        crate::drivers::net::maybe_poll();
        let got = match socket::slot_kind(slot) {
            Some(SlotKind::Udp) => udp::recvfrom_ex(slot, &mut kbuf, peek),
            Some(SlotKind::Raw) => socket::socket_raw_recvfrom(slot, &mut kbuf),
            _ => return -EOPNOTSUPP,
        };
        if let Some((src, n)) = got {
            if write_current_user_bytes(buf, &kbuf[..n]).is_none() {
                return -EFAULT;
            }
            if info != 0 {
                let mut raw = [0u8; 8];
                raw[0..4].copy_from_slice(&src.ip.to_ne_bytes());    // was: to_be_bytes
                raw[4..6].copy_from_slice(&src.port.to_ne_bytes());  // was: to_be_bytes
                if write_current_user_bytes(info, &raw).is_none() {
                    return -EFAULT;
                }
            }
            return n as isize;
        }
        if nonblock {
            return -EAGAIN;
        }
        if crate::timer::monotonic_us() >= deadline {
            return -EAGAIN; // 等不到了；不是"空包"，让用户态自己决定重试
        }
        /* 睡而不是让出：让出等于"换个人继续空转"，整机还是满载。 */
        let _ = crate::task::sleep_current_and_run_next(1);
    }
}


/// connect(fd, ip, port) -> 0 / -EINPROGRESS（非阻塞）/ -errno
///
/// 是否非阻塞由 fd 的 O_NONBLOCK 决定，不额外占领一个 syscall 参数：
/// POSIX 的 connect 本来就只有三个参数，语义全压在 fcntl 上。
pub fn sys_net_connect(fd: usize, ip: usize, port: usize) -> isize {
    let slot = get_slot_from_fd(fd);
    if slot < 0 { return slot; }
    let slot = slot as usize;
    let nonblock = nonblock_flag(fd);
    match socket::slot_kind(slot) {
        Some(SlotKind::Tcp) => tcp::connect(slot, ip as u32, port as u16, nonblock),
        // UDP: 无握手,只记默认对端 + 接收过滤;全 0 地址(0.0.0.0:0)表示断开
        Some(SlotKind::Udp) => udp::connect(slot, ip as u32, port as u16),
        _ => -EOPNOTSUPP,
    }
}

pub fn sys_net_listen(fd: usize, backlog: usize) -> isize {
    let slot = get_slot_from_fd(fd);
    if slot < 0 { return slot; }
    if tcp::listen(slot as usize, backlog) == 0 { 0 } else { -EINVAL }
}

/// accept(fd, info) -> child_fd / -EAGAIN / -errno；info 同 recvfrom 的 8 字节格式
pub fn sys_net_accept(fd: usize, info: usize) -> isize {
    let slot = get_slot_from_fd(fd);
    if slot < 0 { return slot; }
    match tcp::accept(slot as usize) {
        Some((child, remote)) => {
            let cfd = crate::task::alloc_fd_current(Arc::new(SocketFile { slot: child }));
            if cfd < 0 {
                tcp::close(child);
                return -EMFILE;
            }
            /* accept 出来的新 fd 要继承监听 socket 的描述符标志（POSIX 只要求
             * 继承 FD_CLOEXEC 的"不继承"语义；O_NONBLOCK 不继承）。
             * 这里不复制 fd_flags，是有意的。 */
            if info != 0 {
                let mut raw = [0u8; 8];
                raw[0..4].copy_from_slice(&remote.ip.to_ne_bytes());    // was: to_be_bytes
                raw[4..6].copy_from_slice(&remote.port.to_ne_bytes());  // was: to_be_bytes
                if write_current_user_bytes(info, &raw).is_none() {
                    return -EFAULT;
                }
            }
            cfd
        }
        /* tcp::accept 内部会一直等到有连接进来（阻塞），所以这里拿到 None
         * 只可能是"这个槽位不是 Listen 状态"，不是"暂时没有连接"。
         * 等真要做非阻塞 accept 时，得先把 tcp::accept 改成不循环。 */
        None => -EINVAL,
    }
}

pub fn sys_net_send(fd: usize, buf: usize, len: usize) -> isize {
    let slot = get_slot_from_fd(fd);
    if slot < 0 { return slot; }
    if len == 0 {
        return 0;
    }
    if len > 1460 {
        return -EMSGSIZE;
    }
    let data = match read_current_user_bytes(buf, len) {
        Some(d) if d.len() == len => d,
        _ => return -EFAULT,
    };
    let slot = slot as usize;
    let nonblock = nonblock_flag(fd);
    match socket::slot_kind(slot) {
        Some(SlotKind::Tcp) => tcp::send_data(slot, &data, nonblock),
        // UDP: 向 connect 记下的默认对端发送,未 connect 返回 -EDESTADDRREQ
        Some(SlotKind::Udp) => udp::send_data(slot, &data),
        _ => -EOPNOTSUPP,
    }
}

/// recv 返回 n / 0(真 EOF) / -EAGAIN(非阻塞暂无数据) / -errno
pub fn sys_net_recv(fd: usize, buf: usize, maxlen: usize, flags: usize) -> isize {
    let peek = match recv_flags(flags) {
        Ok(p) => p,
        Err(e) => return -e,
    };
    let slot = get_slot_from_fd(fd);
    if slot < 0 { return slot; }
    let nonblock = nonblock_flag(fd);
    let mut kbuf = alloc::vec![0u8; maxlen.min(2048)];
    let slot = slot as usize;
    let n = match socket::slot_kind(slot) {
        Some(SlotKind::Tcp) => tcp::recv_data_ex(slot, &mut kbuf, peek, nonblock),
        // UDP 已 connect 的由 udp::input 负责来源过滤
        Some(SlotKind::Udp) => udp::recv_data_ex(slot, &mut kbuf, peek),
        _ => -EOPNOTSUPP,
    };
    if n <= 0 {
        return n;   /* -EAGAIN 原样传回，别在这里被压成 0 */
    }
    if write_current_user_bytes(buf, &kbuf[..n as usize]).is_none() {
        return -EFAULT;
    }
    n
}


pub fn sys_net_close(fd: usize) -> isize {
    // 可选的严格性:只许关 socket,不许拿 net_close 关文件
    match crate::task::current_file(fd) {
        Some(f) if f.socket_slot().is_some() => crate::task::close_fd_current(fd),
        Some(_) => -ENOTSOCK,
        None => -EBADF,
    }
}

/// SYS_NET_SET_IP: 设置本机 IPv4(参数为主机序 u32)
pub fn sys_net_set_ip(ip: usize) -> isize {
    let old = crate::drivers::net::ip::my_ip();
    crate::drivers::net::ip::set_my_ip(ip as u32);
    let (a, b, c, d) = (ip >> 24 & 0xff, ip >> 16 & 0xff, ip >> 8 & 0xff, ip & 0xff);
    log::info!("[ip] MY_IP {:#010x} -> {}.{}.{}.{}", old, a, b, c, d);
    0
}

pub fn sys_net_get_ip() -> isize {
    crate::drivers::net::ip::my_ip() as isize
}

/// resolve(name_ptr, name_len) -> 主机序 IPv4 / -ENOENT / -errno
/// 阻塞式 DNS 查询。
pub fn sys_net_resolve(name_ptr: usize, name_len: usize) -> isize {
    if name_len == 0 || name_len > 253 {
        return -EINVAL;
    }
    let data = match read_current_user_bytes(name_ptr, name_len) {
        Some(d) if d.len() == name_len => d,
        _ => return -EFAULT,
    };
    let name = match core::str::from_utf8(&data) {
        Ok(n) => n,
        // 含 NUL 或非 UTF-8：不是合法域名，比"解析失败"更具体
        Err(_) => return -EINVAL,
    };
    match crate::drivers::net::dns::resolve(name) {
        Some(ip) => ip as isize,
        None => -super::errno::ENOENT,
    }
}

pub fn sys_net_resolve_many(names_ptr: usize, lens_ptr: usize,
                            out_ptr: usize, count: usize) -> isize {
    if count == 0 || count > 64 {
        return -EINVAL; // 上限防滥用
    }
    // ① 拷入指针数组 + 长度数组(各 count 个 usize)
    let nptrs = match read_current_user_bytes(names_ptr, count * 8) {
        Some(v) if v.len() == count * 8 => v,
        _ => return -EFAULT,
    };
    let lens = match read_current_user_bytes(lens_ptr, count * 8) {
        Some(v) if v.len() == count * 8 => v,
        _ => return -EFAULT,
    };

    let mut names: alloc::vec::Vec<alloc::string::String> = alloc::vec::Vec::with_capacity(count);
    for i in 0..count {
        let p = usize::from_ne_bytes(nptrs[i * 8..i * 8 + 8].try_into().unwrap());
        let l = usize::from_ne_bytes(lens[i * 8..i * 8 + 8].try_into().unwrap());
        if l == 0 || l > 253 {
            return -EINVAL;
        }
        let b = match read_current_user_bytes(p, l) {
            Some(v) if v.len() == l => v,
            _ => return -EFAULT,
        };
        match core::str::from_utf8(&b) {
            Ok(s) => names.push(s.to_string()),
            Err(_) => return -EINVAL,
        }
    }

    let refs: alloc::vec::Vec<&str> = names.iter().map(|s| s.as_str()).collect();
    let results = crate::drivers::net::dns::resolve_many(&refs);

    let mut out = alloc::vec::Vec::with_capacity(count * 4);
    for r in results {
        out.extend_from_slice(&r.unwrap_or(0).to_ne_bytes());
    }
    if write_current_user_bytes(out_ptr, &out).is_none() {
        return -EFAULT;
    }
    count as isize
}

pub fn sys_net_shutdown(fd: usize, how: usize) -> isize {
    let slot = get_slot_from_fd(fd);
    if slot < 0 { return slot; }
    tcp::shutdown(slot as usize, how)
}

/// getsockname/getpeername:info 同 accept 的 8 字节格式(ip 4B + port 2B,ne)
fn write_sockaddr(slot: usize, info: usize, addr: Option<SocketAddr>) -> isize {
    let addr = match addr { Some(a) => a, None => return -ENOTCONN };
    if info == 0 { return -EFAULT; }
    let mut raw = [0u8; 8];
    raw[0..4].copy_from_slice(&addr.ip.to_ne_bytes());
    raw[4..6].copy_from_slice(&addr.port.to_ne_bytes());
    match write_current_user_bytes(info, &raw) {
        Some(_) => 0,
        None => -EFAULT,
    }
}

pub fn sys_net_getsockname(fd: usize, info: usize) -> isize {
    let slot = get_slot_from_fd(fd);
    if slot < 0 { return slot; }
    write_sockaddr(slot as usize, info, socket::socket_getsockname(slot as usize))
}

pub fn sys_net_getpeername(fd: usize, info: usize) -> isize {
    let slot = get_slot_from_fd(fd);
    if slot < 0 { return slot; }
    write_sockaddr(slot as usize, info, socket::socket_getpeername(slot as usize))
}

/// setsockopt(fd, optname, optval) -> 0 / -errno
pub fn sys_net_setsockopt(fd: usize, optname: usize, optval: usize) -> isize {
    let slot = get_slot_from_fd(fd);
    if slot < 0 { return slot; }
    if socket::socket_setsockopt(slot as usize, optname, optval) { 0 } else { -ENOPROTOOPT }
}

/// getsockopt(fd, level, optname, optval, optlen) -> 0 / -errno
///
/// 只写回一个 int，并把 *optlen 更新成 4（Linux 语义：返回实际写入的字节数）。
/// 调用方声明缓冲不足 4 字节时直接 EINVAL，不写。
///
/// 返回 -errno 而不是裸 -1：用户态 getsockopt 过 rk_sys_ret，能拿到真实 errno。
/// 旁边 setsockopt 那条链就是老问题（裸 -1 == -EPERM，errno 永远不设），新代码别再复制。
pub fn sys_net_getsockopt(fd: usize, level: usize, optname: usize,
                          optval: usize, optlen: usize) -> isize {
    const SOL_SOCKET: usize = 1;

    if level != SOL_SOCKET { return -ENOPROTOOPT; }
    if optval == 0 || optlen == 0 { return -EINVAL; }

    let len_raw = match read_current_user_bytes(optlen, 4) {
        Some(b) if b.len() == 4 => b,
        _ => return -EFAULT,
    };
    let cap = i32::from_ne_bytes([len_raw[0], len_raw[1], len_raw[2], len_raw[3]]);
    if cap < 4 { return -EINVAL; }

    let slot = get_slot_from_fd(fd);
    if slot < 0 { return -ENOTSOCK; }

    let val = match socket::socket_getsockopt(slot as usize, optname) {
        Some(v) => v,
        None => return -ENOPROTOOPT,
    };

    if write_current_user_bytes(optval, &val.to_ne_bytes()).is_none() { return -EFAULT; }
    if write_current_user_bytes(optlen, &4i32.to_ne_bytes()).is_none() { return -EFAULT; }
    0
}