use alloc::collections::vec_deque::VecDeque;
use alloc::vec::Vec;
use core::sync::atomic::{AtomicU16, AtomicU32, Ordering};
use crate::drivers::net::ip::{checksum as ip_checksum, send as ip_send, my_ip};
use crate::drivers::net::socket::{Socket, SocketAddr, SOCKET_TABLE};
use crate::println;
use crate::sync::spin::Mutex;

#[cfg(target_arch = "riscv64")]
fn now_ms() -> u64 { (crate::timer::monotonic_time() / 10_000) as u64 }

#[cfg(target_arch = "loongarch64")]
fn now_ms() -> u64 { (crate::timer::monotonic_time() / 100_000) as u64 }

pub const FIN: u8 = 0x01;
pub const SYN: u8 = 0x02;
pub const RST: u8 = 0x04;
pub const PSH: u8 = 0x08;
pub const ACK: u8 = 0x10;

// ---------- RTO 常量(RFC 6298 / Jacobson) ----------
const INITIAL_RTO_MS: u64 = 1_000; // RFC 6298 §2.1:无样本时的初始 RTO
const RTO_MIN_MS: u64 = 200;       // RFC 建议 1s;QEMU 近零 RTT 场景取 200ms
const RTO_MAX_MS: u64 = 16_000;    // 退避上限(RFC 为 60s,教学版收紧)
const RTT_G_MS: u64 = 10;          // G:时钟粒度余量
const MAX_RETRIES: u8 = 8;
const TIME_WAIT_MS: u64 = 10_000; // 标准是 2MSL=60s,教学版取 10s
const MAX_PAYLOAD: usize = 1460;

// ---- 实验装置:每发 LOSS_EVERY 个数据段丢 1 个;0 = 关闭 ----
// 确定性丢包(非随机),保证实验可复现;A/B 两组用同一个值。
const LOSS_EVERY: u32 = 0;

#[repr(C, packed)]
pub struct TcpHeader {
    pub src_port: u16,
    pub dst_port: u16,
    pub seq: u32,
    pub ack: u32,
    pub data_off: u8, // 高 4 位：头长（32 位字）
    pub flags: u8,
    pub window: u16,
    pub checksum: u16,
    pub urgent: u16,
}

#[derive(Clone, Copy, PartialEq, Debug)]
pub enum TcpState {
    Closed,
    Listen,
    SynSent,
    SynReceived,
    Established,
    FinWait1,
    FinWait2,
    CloseWait,
    Closing,
    LastAck,
    TimeWait,
}

/// 已发未确认段（重传队列元素）。flags 按「重传时要用的」存。
pub struct TxSeg {
    pub seq: u32,
    pub flags: u8,
    pub data: Vec<u8>,
    pub sent_ms: u64,        // 最近一次发出的时间戳(Karn:仅未重传时用于采样)
    pub retransmitted: bool, // Karn 规则一:重传过的段不采样
}

impl TxSeg {
    fn end_seq(&self) -> u32 {
        let cost = self.data.len() as u32
            + if self.flags & (SYN | FIN) != 0 { 1 } else { 0 };
        self.seq.wrapping_add(cost)
    }
    fn new(seq: u32, flags: u8, data: Vec<u8>) -> Self {
        Self { seq, flags, data, sent_ms: now_ms(), retransmitted: false }
    }
}

pub struct TcpSocket {
    pub state: TcpState,
    pub local_port: u16,
    pub remote: Option<SocketAddr>,
    pub snd_una: u32,
    pub snd_nxt: u32,
    pub rcv_nxt: u32,
    pub snd_wnd: u16,
    pub tx_unacked: VecDeque<TxSeg>,
    pub rx_queue: VecDeque<Vec<u8>>,
    pub rto_deadline: u64,
    pub rto_ms: u64,
    pub retries: u8,
    // --- Jacobson 估值器(定点):srtt 放大 8 倍、rttvar 放大 4 倍存储 ---
    pub srtt: u64,
    pub rttvar: u64,
    pub has_rtt: bool,
    pub time_wait_deadline: u64,
    pub accept_queue: VecDeque<usize>, // listen 用：已 established 的子连接 fd
    pub parent: Option<usize>,         // 子连接用：listener 的 fd
    pub rst: bool,                     // 被 RST/超时击毙标记，recv 返回 -1
}

impl TcpSocket {
    pub fn new() -> Self {
        Self {
            state: TcpState::Closed,
            local_port: 0,
            remote: None,
            snd_una: 0,
            snd_nxt: 0,
            rcv_nxt: 0,
            snd_wnd: 65535,
            tx_unacked: VecDeque::new(),
            rx_queue: VecDeque::new(),
            rto_deadline: 0,
            rto_ms: INITIAL_RTO_MS,
            retries: 0,
            srtt: 0,
            rttvar: 0,
            has_rtt: false,
            time_wait_deadline: 0,
            accept_queue: VecDeque::new(),
            parent: None,
            rst: false,
        }
    }
}

// ---------- RTT 估计:Jacobson 算法(RFC 6298),定点整数实现 ----------
//
// SRTT   = (1-α)·SRTT + α·R,        α = 1/8  → 除法即右移 3 位
// RTTVAR = (1-β)·RTTVAR + β·|SRTT−R|, β = 1/4 → 右移 2 位
// RTO    = SRTT + max(G, K·RTTVAR), K = 4(rttvar 已 ×4 存储,天然满足)
//
// Karn 算法两条:
//   1. 重传过的段不产生 RTT 样本(ACK 歧义:无法区分是对原发还是重传的确认);
//   2. 退避后的 RTO 要保持到下一个「干净样本」出现才允许回落。

/// 由当前估值器算 RTO:SRTT + max(G, 4·RTTVAR),钳位到 [MIN, MAX]
fn compute_rto(t: &TcpSocket) -> u64 {
    ((t.srtt >> 3) + core::cmp::max(RTT_G_MS, t.rttvar))
        .clamp(RTO_MIN_MS, RTO_MAX_MS)
}

fn rtt_update(t: &mut TcpSocket, r: u64) {
    if !t.has_rtt {
        // RFC 6298 §2.2:首个样本 SRTT=R, RTTVAR=R/2
        t.has_rtt = true;
        t.srtt = r << 3;   // ×8
        t.rttvar = r << 1; // R/2 再 ×4 = 2R
    } else {
        // RFC 6298 §2.3
        let delta = (r as i64 - (t.srtt >> 3) as i64).unsigned_abs();
        t.rttvar = t.rttvar + delta - (t.rttvar >> 2);
        t.srtt = (t.srtt as i64 + (r as i64 - (t.srtt >> 3) as i64)) as u64;
    }
    t.rto_ms = compute_rto(t);
    log::warn!(
        "[tcp] t={} rtt sample={}ms srtt={}ms rttvar={}ms rto={}ms",
        now_ms(), r, t.srtt >> 3, t.rttvar >> 2, t.rto_ms
    );
}

fn as_tcp(s: &Socket) -> Option<&TcpSocket> {
    if let Socket::Tcp(t) = s { Some(t) } else { None }
}
fn as_tcp_mut(s: &mut Socket) -> Option<&mut TcpSocket> {
    if let Socket::Tcp(t) = s { Some(t) } else { None }
}

static NEXT_EPHEM: AtomicU16 = AtomicU16::new(49152);
static ISN_SEQ: AtomicU32 = AtomicU32::new(0x1234_5678);

fn next_ephem_port() -> u16 { NEXT_EPHEM.fetch_add(1, Ordering::Relaxed) }
fn next_isn() -> u32 { ISN_SEQ.fetch_add(0x10001, Ordering::Relaxed) ^ (now_ms() as u32) }

// ---------- 线上发送 ----------

fn tcp_checksum(src_ip: u32, dst_ip: u32, pkt: &[u8]) -> u16 {
    // 伪头部：saddr(4) daddr(4) zero(1) proto=6(1) tcp_len(2)，网络字节序
    let mut pseudo = Vec::with_capacity(12);
    pseudo.extend_from_slice(&src_ip.to_be_bytes());
    pseudo.extend_from_slice(&dst_ip.to_be_bytes());
    pseudo.push(0);
    pseudo.push(6);
    pseudo.extend_from_slice(&(pkt.len() as u16).to_be_bytes());
    let mut sum: u32 = ip_checksum(&pseudo) as u32 ^ 0xFFFF; // 先求伪头部和（不取反）
    // ip_checksum 返回的是取反后的值，异或 0xFFFF 还原成和
    sum += ip_checksum(pkt) as u32 ^ 0xFFFF;
    while (sum >> 16) != 0 {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }
    !(sum as u16)
}

fn send_segment(local_port: u16, remote: SocketAddr, seq: u32, ack: u32, flags: u8, payload: &[u8]) {
    // --- 实验装置:只丢数据段,不动 SYN/FIN(避免干扰建连与挥手) ---
    if LOSS_EVERY > 0 && flags & (SYN | FIN) == 0 && !payload.is_empty() {
        static SEND_CNT: AtomicU32 = AtomicU32::new(0);
        if SEND_CNT.fetch_add(1, Ordering::Relaxed) % LOSS_EVERY == LOSS_EVERY - 1 {
            log::warn!("[tcp] t={} drop! seq={}", now_ms(), seq);
            return; // 段不上线,模拟链路丢包
        }
    }
    let tcp_len = 20 + payload.len();
    let mut pkt = Vec::with_capacity(tcp_len);
    unsafe { pkt.set_len(20) };
    let h = unsafe { &mut *(pkt.as_mut_ptr() as *mut TcpHeader) };
    h.src_port = local_port.to_be();
    h.dst_port = remote.port.to_be();
    h.seq = seq.to_be();
    h.ack = ack.to_be();
    h.data_off = 5 << 4;
    h.flags = flags;
    h.window = 65535u16.to_be(); // 教学版：接收窗口固定拉满
    h.checksum = 0;
    h.urgent = 0;
    pkt.extend_from_slice(payload);
    let csum = tcp_checksum(my_ip(), remote.ip, &pkt);
    unsafe { core::ptr::write_unaligned(core::ptr::addr_of_mut!((*h).checksum), csum.to_be()) };
    ip_send(remote.ip, 6, &pkt);
}

fn alloc_slot(table: &[Option<Socket>; 8]) -> Option<usize> {
    table.iter().position(|s| s.is_none())
}

fn process_ack(t: &mut TcpSocket, ack: u32) {
    let now = now_ms();
    let mut progress = false;
    let mut clean_sample: Option<u64> = None;
    let mut first = true;
    while let Some(seg) = t.tx_unacked.front() {
        if ack >= seg.end_seq() {   // 教学简化：不考虑序号绕回
            t.snd_una = seg.end_seq();
            let seg = t.tx_unacked.pop_front().unwrap();
            // 防陈旧样本:只有「ACK 到达时的队首段」才允许采样。
            // 孔洞修复后累积 ACK 会连跳多段,后续段的 now-sent_ms 混入了
            // 修洞等待,不是真实 RTT;队首段若是干净的,说明它发出至今未
            // 满一个 RTO(否则已被重传标记),样本必然新鲜。
            if first && !seg.retransmitted {
                clean_sample = Some(now.saturating_sub(seg.sent_ms));
            }
            first = false;
            progress = true;
        } else {
            break;
        }
    }
    if progress {
        t.retries = 0;
        if let Some(r) = clean_sample {
            rtt_update(t, r); // 干净样本:更新估值并重算 RTO
        } else {
            // 无干净样本但有前向进展:把 RTO 恢复到估值(参考 Linux RTO-restore)。
            // 严格 Karn 的「退避保持到干净样本」在排水阶段(数据已发完、只剩重传)
            // 会让 RTO 棘轮式翻倍——连续孔洞各翻一倍直达封顶,实验日志已证实。
            t.rto_ms = compute_rto(t);
        }
        t.rto_deadline = if t.tx_unacked.is_empty() { 0 } else { now + t.rto_ms };
    }
}

// ---------- 接收路径（ip.rs 的 proto 6 分发到这里） ----------

pub fn input(segment: &[u8], src_ip: u32) {
    if segment.len() < 20 {
        return;
    }
    let h = unsafe { segment.as_ptr().cast::<TcpHeader>().read_unaligned() };
    let dst_port = u16::from_be(h.dst_port);
    let src_port = u16::from_be(h.src_port);
    let seq = u32::from_be(h.seq);
    let ack = u32::from_be(h.ack);
    let flags = h.flags;
    let wnd = u16::from_be(h.window);
    let hdr_len = ((h.data_off >> 4) as usize) * 4;
    if hdr_len < 20 || segment.len() < hdr_len {
        return;
    }
    let payload = &segment[hdr_len..];
    let remote = SocketAddr { ip: src_ip, port: src_port };

    let mut table = SOCKET_TABLE.lock();

    // 1) 四元组精确匹配已有连接
    let mut idx: Option<usize> = None;
    for (i, s) in table.iter().enumerate() {
        if let Some(Socket::Tcp(t)) = s {
            if t.state != TcpState::Listen && t.local_port == dst_port && t.remote == Some(remote) {
                idx = Some(i);
                break;
            }
        }
    }
    // 2) LISTEN 匹配新连接
    if idx.is_none() {
        for (i, s) in table.iter().enumerate() {
            if let Some(Socket::Tcp(t)) = s {
                if t.state == TcpState::Listen && t.local_port == dst_port {
                    idx = Some(i);
                    break;
                }
            }
        }
    }
    let i = match idx {
        Some(i) => i,
        None => return, // 无人监听，丢（教学版不回 RST）
    };
    // ---- LISTEN：SYN 建子连接 ----
    let is_listener = matches!(&table[i], Some(Socket::Tcp(t)) if t.state == TcpState::Listen);
    if is_listener {
        if flags & SYN == 0 {
            return;
        }
        let child_idx = match alloc_slot(&table) {
            Some(c) => c,
            None => return,
        };
        let isn = next_isn();
        let mut child = TcpSocket::new();
        child.state = TcpState::SynReceived;
        child.local_port = dst_port;
        child.remote = Some(remote);
        child.parent = Some(i);
        child.snd_una = isn;
        child.snd_nxt = isn.wrapping_add(1);
        child.rcv_nxt = seq.wrapping_add(1);
        child.tx_unacked.push_back(TxSeg::new(isn, SYN | ACK, Vec::new()));
        child.rto_deadline = now_ms() + child.rto_ms;
        table[child_idx] = Some(Socket::Tcp(child));
        send_segment(dst_port, remote, isn, seq.wrapping_add(1), SYN | ACK, &[]);
        return;
    }

    // ---- 已有连接的状态机 ----
    let mut push_accept: Option<(usize, usize)> = None; // (listener_fd, child_fd)
    let mut free_slot = false;
    {
        let t = match table[i].as_mut().and_then(as_tcp_mut) {
            Some(t) => t,
            None => return,
        };

        if flags & RST != 0 {
            t.state = TcpState::Closed;
            t.rst = true;
            t.rx_queue.push_back(Vec::new()); // 哨兵：唤醒阻塞中的 recv
        } else {
            t.snd_wnd = wnd;
            match t.state {
                TcpState::SynSent => {
                    if flags & (SYN | ACK) == SYN | ACK && ack == t.snd_nxt {
                        t.rcv_nxt = seq.wrapping_add(1);
                        process_ack(t, ack); // SYN 的 RTT 即首个样本
                        let (lp, r) = (t.local_port, t.remote.unwrap());
                        let (sn, rn) = (t.snd_nxt, t.rcv_nxt);
                        send_segment(lp, r, sn, rn, ACK, &[]);
                        t.state = TcpState::Established;
                        println!("[tcp] fd {} established", i);
                    }
                }
                TcpState::SynReceived => {
                    if flags & ACK != 0 && ack == t.snd_nxt {
                        process_ack(t, ack);
                        t.state = TcpState::Established;
                        push_accept = t.parent.map(|p| (p, i));
                        println!("[tcp] fd {} accepted (passive open)", i);
                    }
                }
                TcpState::Established | TcpState::FinWait1 | TcpState::FinWait2
                | TcpState::CloseWait | TcpState::Closing | TcpState::LastAck => {
                    let mut ack_now = false;
                    if flags & SYN == 0 {
                        if seq == t.rcv_nxt {
                            if !payload.is_empty() {
                                t.rcv_nxt = t.rcv_nxt.wrapping_add(payload.len() as u32);
                                t.rx_queue.push_back(payload.to_vec());
                                ack_now = true;
                            }
                            if flags & FIN != 0 {
                                t.rcv_nxt = t.rcv_nxt.wrapping_add(1);
                                ack_now = true;
                                t.state = match t.state {
                                    TcpState::Established => TcpState::CloseWait,
                                    TcpState::FinWait1 => TcpState::Closing,
                                    TcpState::FinWait2 => {
                                        t.time_wait_deadline = now_ms() + TIME_WAIT_MS;
                                        TcpState::TimeWait
                                    }
                                    s => s,
                                };
                            }
                        } else {
                            ack_now = true; // 乱序：丢弃 + 重 ACK（TODO: 重组缓存）
                        }
                    }
                    process_ack(t, ack);
                    // 我方 FIN 被确认后的状态推进
                    match t.state {
                        TcpState::FinWait1 if t.snd_una == t.snd_nxt => {
                            t.state = TcpState::FinWait2;
                        }
                        TcpState::Closing if t.snd_una == t.snd_nxt => {
                            t.time_wait_deadline = now_ms() + TIME_WAIT_MS;
                            t.state = TcpState::TimeWait;
                        }
                        TcpState::LastAck if t.snd_una == t.snd_nxt => {
                            free_slot = true; // 挥手完成，释放
                        }
                        _ => {}
                    }
                    if ack_now && !free_slot {
                        let (lp, r) = (t.local_port, t.remote.unwrap());
                        let (sn, rn) = (t.snd_nxt, t.rcv_nxt);
                        send_segment(lp, r, sn, rn, ACK, &[]);
                    }
                }
                _ => {}
            }
        }
    }
    if let Some((p, child)) = push_accept {
        if let Some(Socket::Tcp(l)) = &mut table[p] {
            l.accept_queue.push_back(child);
        }
    }
    if free_slot {
        table[i] = None;
    }
}

// ---------- tick：超时重传 + TIME_WAIT 回收（poll() 里调用） ----------

pub fn tick() {
    let now = now_ms();
    let mut table = SOCKET_TABLE.lock();
    for i in 0..table.len() {
        let mut free = false;
        if let Some(t) = table[i].as_mut().and_then(as_tcp_mut) {
            if t.state == TcpState::TimeWait {
                if now >= t.time_wait_deadline {
                    free = true;
                }
            } else if t.state != TcpState::Closed && t.rto_deadline != 0 && now >= t.rto_deadline {
                if t.remote.is_some() && !t.tx_unacked.is_empty() {
                    let remote = t.remote.unwrap();
                    let seg = t.tx_unacked.front_mut().unwrap();
                    seg.retransmitted = true; // Karn:此段此后不再贡献 RTT 样本
                    seg.sent_ms = now;
                    // clone 一份避免借用冲突，段很小，教学可接受
                    let (seq, flags, data) = (seg.seq, seg.flags, seg.data.clone());
                    let (lp, rn) = (t.local_port, t.rcv_nxt);
                    send_segment(lp, remote, seq, rn, flags, &data);
                    t.retries = t.retries.saturating_add(1);
                    log::warn!("[tcp] t={} rtx seq={} retry={} rto={}", now, seq, t.retries, t.rto_ms);
                    t.rto_ms = (t.rto_ms * 2).min(RTO_MAX_MS); // 指数退避
                    t.rto_deadline = now + t.rto_ms;
                    if t.retries > MAX_RETRIES {
                        t.rto_deadline = 0;
                        if t.state == TcpState::SynReceived {
                            free = true; // 半开子连接直接回收
                        } else {
                            t.state = TcpState::Closed;
                            t.rst = true;
                            t.rx_queue.push_back(Vec::new());
                        }
                    }
                } else {
                    t.rto_deadline = 0;
                }
            }
        }
        if free {
            table[i] = None;
        }
    }
}

// ---------- 对外 API（syscall 层调用） ----------

pub fn connect(fd: usize, ip: u32, port: u16) -> isize {
    {
        let mut table = SOCKET_TABLE.lock();
        let t = match table.get_mut(fd).and_then(|s| s.as_mut()).and_then(as_tcp_mut) {
            Some(t) => t,
            None => return -1,
        };
        if t.state != TcpState::Closed {
            return -1;
        }
        if t.local_port == 0 {
            t.local_port = next_ephem_port();
        }
        let isn = next_isn();
        t.remote = Some(SocketAddr { ip, port });
        t.snd_una = isn;
        t.snd_nxt = isn.wrapping_add(1);
        t.rcv_nxt = 0;
        t.state = TcpState::SynSent;
        t.tx_unacked.push_back(TxSeg::new(isn, SYN, Vec::new()));
        t.rto_deadline = now_ms() + t.rto_ms;
        let lp = t.local_port;
        drop(table);
        send_segment(lp, SocketAddr { ip, port }, isn, 0, SYN, &[]);
        // println!("[tcp] SYN sent");
    }
    // 阻塞等握手完成
    let mut spins = 0usize;
    loop {
        crate::drivers::net::poll();
        {
            let table = SOCKET_TABLE.lock();
            match table.get(fd).and_then(|s| s.as_ref()).and_then(as_tcp) {
                Some(t) if t.state == TcpState::Established => return 0,
                Some(t) if t.state == TcpState::Closed => return -1,
                None => return -1,
                _ => {}
            }
        }
        spins += 1;
        if spins > 30_000_000 {
            close(fd);
            log::warn!("[tcp] connect timeout");
            return -1;
        }

    }

}

pub fn listen(fd: usize) -> isize {
    let mut table = SOCKET_TABLE.lock();
    match table.get_mut(fd).and_then(|s| s.as_mut()).and_then(as_tcp_mut) {
        Some(t) if t.state == TcpState::Closed && t.local_port != 0 => {
            t.state = TcpState::Listen;
            0
        }
        _ => -1,
    }
}

/// 阻塞等一个已 established 的子连接，返回 (child_fd, remote)
pub fn accept(fd: usize) -> Option<(usize, SocketAddr)> {
    let mut spins = 0usize;
    loop {
        crate::drivers::net::poll();
        {
            let mut table = SOCKET_TABLE.lock();
            match table.get_mut(fd).and_then(|s| s.as_mut()).and_then(as_tcp_mut) {
                Some(l) if l.state == TcpState::Listen => {
                    if let Some(child) = l.accept_queue.pop_front() {
                        if let Some(Socket::Tcp(c)) = &table[child] {
                            if let Some(r) = c.remote {
                                return Some((child, r));
                            }
                        }
                    }
                }
                _ => return None,
            }
        }
        spins += 1;
        if spins > 100_000_000 {
            return None;
        }
    }
}

pub fn send_data(fd: usize, data: &[u8]) -> isize {
    let mut spins = 0usize;
    loop {
        {
            let mut table = SOCKET_TABLE.lock();
            let t = match table.get_mut(fd).and_then(|s| s.as_mut()).and_then(as_tcp_mut) {
                Some(t) => t,
                None => return -1,
            };
            match t.state {
                TcpState::Established | TcpState::CloseWait => {}
                _ => return -1,
            }
            // 流量控制:在途字节数不得越过对端通告的接收窗口。
            // 不跟踪在途就灌 1MB,宿主机的 64KB rcvbuf 会被打爆造成真实丢包。
            let in_flight = t.snd_nxt.wrapping_sub(t.snd_una) as usize;
            if in_flight < t.snd_wnd as usize {
                let len = data.len()
                    .min(MAX_PAYLOAD)
                    .min(t.snd_wnd as usize - in_flight);
                let seq = t.snd_nxt;
                t.snd_nxt = t.snd_nxt.wrapping_add(len as u32);
                t.tx_unacked.push_back(TxSeg::new(seq, ACK | PSH, data[..len].to_vec()));
                if t.rto_deadline == 0 {
                    t.rto_deadline = now_ms() + t.rto_ms;
                }
                let (lp, r, rn) = (t.local_port, t.remote.unwrap(), t.rcv_nxt);
                send_segment(lp, r, seq, rn, ACK | PSH, &data[..len]);
                return len as isize;
            }
            // 窗口满:解锁 poll,等 ACK 推进 snd_una 后再试
        }
        crate::drivers::net::poll();
        spins += 1;
        if spins > 50_000_000 {
            return -1; // 对端长久不开窗,放弃
        }
    }
}

/// 返回 n / 0(EOF，对端已关) / -1(RST 或错误)
pub fn recv_data(fd: usize, out: &mut [u8]) -> isize {
    loop {
        crate::drivers::net::poll();
        {
            let mut table = SOCKET_TABLE.lock();
            match table.get_mut(fd).and_then(|s| s.as_mut()).and_then(as_tcp_mut) {
                Some(t) => {
                    if let Some(chunk) = t.rx_queue.pop_front() {
                        if chunk.is_empty() && t.rst {
                            return -1; // RST 哨兵
                        }
                        let n = chunk.len().min(out.len());
                        out[..n].copy_from_slice(&chunk[..n]);
                        return n as isize;
                    }
                    match t.state {
                        TcpState::Closed => return if t.rst { -1 } else { 0 },
                        TcpState::CloseWait | TcpState::Closing
                        | TcpState::LastAck | TcpState::TimeWait => return 0,
                        _ => {}
                    }
                }
                None => return -1,
            }
        }
    }
}

pub fn close(fd: usize) -> isize {
    let mut table = SOCKET_TABLE.lock();
    let mut free = false;
    let mut ok = true;
    if let Some(t) = table.get_mut(fd).and_then(|s| s.as_mut()).and_then(as_tcp_mut) {
        match t.state {
            TcpState::Established => {
                let seq = t.snd_nxt;
                t.snd_nxt = t.snd_nxt.wrapping_add(1);
                t.tx_unacked.push_back(TxSeg::new(seq, FIN | ACK, Vec::new()));
                if t.rto_deadline == 0 {
                    t.rto_deadline = now_ms() + t.rto_ms;
                }
                let (lp, r, rn) = (t.local_port, t.remote.unwrap(), t.rcv_nxt);
                send_segment(lp, r, seq, rn, FIN | ACK, &[]);
                t.state = TcpState::FinWait1;
            }
            TcpState::CloseWait => {
                let seq = t.snd_nxt;
                t.snd_nxt = t.snd_nxt.wrapping_add(1);
                t.tx_unacked.push_back(TxSeg::new(seq, FIN | ACK, Vec::new()));
                if t.rto_deadline == 0 {
                    t.rto_deadline = now_ms() + t.rto_ms;
                }
                let (lp, r, rn) = (t.local_port, t.remote.unwrap(), t.rcv_nxt);
                send_segment(lp, r, seq, rn, FIN | ACK, &[]);
                t.state = TcpState::LastAck;
            }
            // Listen / Closed / SynSent 放弃 / TimeWait 等：直接释放
            _ => free = true,
        }
    } else {
        ok = false;
    }
    if free {
        table[fd] = None;
    }
    if ok { 0 } else { -1 }
}