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
const INITIAL_RTO_MS: u64 = 1_000;
const RTO_MIN_MS: u64 = 200;
const RTO_MAX_MS: u64 = 16_000;
const RTT_G_MS: u64 = 10;
const MAX_RETRIES: u8 = 8;
const TIME_WAIT_MS: u64 = 10_000;
const MAX_PAYLOAD: usize = 1460;

// ---------- CUBIC 常量(RFC 9438) ----------
const CWND_SCALE: u32 = 8;                    // 定点:1 段 = 8,便于表示 β 等小数
const INIT_CWND: u32 = 4 * CWND_SCALE;        // 初始拥塞窗口 4 段(RFC 5681 上限)
const INIT_SSTHRESH: u32 = u32::MAX;          // 首次丢包前一直慢启动
const BETA_NUM: u32 = 7;                      // β = 0.7:丢包后 cwnd *= 0.7
const BETA_DEN: u32 = 10;
const C_NUM: u64 = 4;                         // C = 0.4,决定凸区探测激进程度(段/秒³)
const C_DEN: u64 = 10;
const FAST_CONV: bool = true;                 // 快速收敛开关(RFC 9438 §4.6)
const DUPACK_THRESH: u8 = 3;                  // 快速重传阈值(RFC 5681)

// ---- 实验装置:每发 LOSS_EVERY 个数据段丢 1 个;0 = 关闭 ----
const LOSS_EVERY: u32 = 0;

#[repr(C, packed)]
pub struct TcpHeader {
    pub src_port: u16,
    pub dst_port: u16,
    pub seq: u32,
    pub ack: u32,
    pub data_off: u8,
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

pub struct TxSeg {
    pub seq: u32,
    pub flags: u8,
    pub data: Vec<u8>,
    pub sent_ms: u64,
    pub retransmitted: bool,
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
    pub srtt: u64,
    pub rttvar: u64,
    pub has_rtt: bool,
    pub time_wait_deadline: u64,
    pub accept_queue: VecDeque<usize>,
    pub parent: Option<usize>,
    pub rst: bool,
    // ---------- 拥塞控制(CUBIC) ----------
    pub cwnd: u32,        // 拥塞窗口,单位:段 × CWND_SCALE(定点)
    pub ssthresh: u32,    // 慢启动阈值,同单位
    pub dup_acks: u8,     // 连续重复 ACK 计数(快速重传用)
    pub loss_seq: u32,    // 本 episode 已降窗标记:= 触发降窗时的 snd_una
    // --- CUBIC 状态机(RFC 9438 §4) ---
    pub epoch_start: u64, // 当前 epoch 起点(ms);0 = 丢包后尚未重启计时
    pub w_max: u32,       // 上次丢包时的 cwnd(段 × SCALE)
    pub last_max: u32,    // 上上次 w_max,快速收敛用
    pub k_ms: u64,        // K:W_cubic 凹区爬回 w_max 所需时长(ms)
    pub origin: u32,      // 曲线原点 = epoch 起点处的 max(w_max, cwnd)
    pub cwnd_cnt: u32,    // ACK 计数器:攒够 cnt 个 ACK 才 +1 段
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
            cwnd: INIT_CWND,
            ssthresh: INIT_SSTHRESH,
            dup_acks: 0,
            loss_seq: u32::MAX,
            epoch_start: 0,
            w_max: 0,
            last_max: 0,
            k_ms: 0,
            origin: 0,
            cwnd_cnt: 0,
        }
    }
}

// ---------- RTT 估计:Jacobson(RFC 6298) ----------

fn compute_rto(t: &TcpSocket) -> u64 {
    ((t.srtt >> 3) + core::cmp::max(RTT_G_MS, t.rttvar))
        .clamp(RTO_MIN_MS, RTO_MAX_MS)
}

fn rtt_update(t: &mut TcpSocket, r: u64) {
    if !t.has_rtt {
        t.has_rtt = true;
        t.srtt = r << 3;
        t.rttvar = r << 1;
    } else {
        let delta = (r as i64 - (t.srtt >> 3) as i64).unsigned_abs();
        t.rttvar = t.rttvar + delta - (t.rttvar >> 2);
        t.srtt = (t.srtt as i64 + (r as i64 - (t.srtt >> 3) as i64)) as u64;
    }
    t.rto_ms = compute_rto(t);
    log::info!(
        "[tcp] t={} rtt sample={}ms srtt={}ms rttvar={}ms rto={}ms",
        now_ms(), r, t.srtt >> 3, t.rttvar >> 2, t.rto_ms
    );
}

// ---------- CUBIC 核心(RFC 9438) ----------
//
// W_cubic(t) = C·(t − K)³ + W_max
//   t   : 距上次丢包(epoch 起点)的真实时间 —— 注意 CUBIC 基于时间而非 RTT,
//         这正是它相对 Reno 的 RTT 公平性卖点(高 RTT 流不再吃亏)。
//   K   : ∛((W_max − cwnd_epoch)/C),凹区爬回 W_max 所需时间。
//   凹区(t < K):快速逼近 W_max —— 上次丢包点附近大概率仍有容量,先冲;
//   凸区(t > K):越过 W_max 后缓慢立方探测 —— 前面是未知领域,谨慎。
//
// TCP 友好区(§4.2):W_est = W_max·β + [3(1−β)/(1+β)]·t/RTT
//   若 W_est 比 W_cubic 涨得快(低带宽场景),按 W_est 涨,避免被 Reno 流饿死。

/// 整数立方根:最大的 r 使 r³ ≤ v(二分)
fn icbrt(v: u64) -> u64 {
    let (mut lo, mut hi) = (0u64, 1u64);
    while hi * hi * hi <= v && hi < (1 << 21) { hi <<= 1; }
    while lo + 1 < hi {
        let mid = (lo + hi) / 2;
        if mid * mid * mid <= v { lo = mid; } else { hi = mid; }
    }
    lo
}

/// C·(d_ms/1000)³,返回 段×CWND_SCALE;d 限幅防爆
fn cubic_term(d_ms: u64) -> u64 {
    let d = d_ms.min(30_000);
    (C_NUM * d * d * d * CWND_SCALE as u64 / C_DEN) / 1_000_000_000
}

/// 每收到 acked 个新确认的段,推进一次拥塞窗口
fn cubic_update(t: &mut TcpSocket, acked: u32) {
    let now = now_ms();

    // 慢启动(RFC 5681):cwnd < ssthresh 时每 ACK +1 段,指数爬升
    if t.cwnd < t.ssthresh {
        t.cwnd = t.cwnd.saturating_add(acked * CWND_SCALE);
        return;
    }

    // epoch 起点:丢包降窗后的第一个 ACK 才正式开始 CUBIC 计时,
    // 此时根据「cwnd 与 w_max 的差距」确定 K 和原点
    if t.epoch_start == 0 {
        t.epoch_start = now;
        if t.cwnd < t.w_max {
            // K_s = ∛((w_max − cwnd)/C) 秒 → 换算 ms:∛(x·C_DEN·1e9/C_NUM)
            let delta_segs = ((t.w_max - t.cwnd) / CWND_SCALE) as u64;
            t.k_ms = icbrt(delta_segs * C_DEN * 1_000_000_000 / C_NUM);
            t.origin = t.w_max;
        } else {
            t.k_ms = 0;
            t.origin = t.cwnd;
        }
        t.cwnd_cnt = 0;
        log::info!(
            "[cubic] t={} epoch start cwnd={} w_max={} K={}ms",
            now, t.cwnd / CWND_SCALE, t.w_max / CWND_SCALE, t.k_ms
        );
    }

    // W_cubic:凹区 = origin − C·(K−t)³,凸区 = origin + C·(t−K)³
    let t_since = now - t.epoch_start;
    let w_cubic = if t_since < t.k_ms {
        t.origin.saturating_sub(cubic_term(t.k_ms - t_since) as u32)
    } else {
        t.origin.saturating_add(cubic_term(t_since - t.k_ms) as u32)
    };
    let mut target = core::cmp::max(w_cubic, t.cwnd);

    // TCP 友好区:W_est = w_max·β + (9/17)·(t/RTT) 段
    let rtt = core::cmp::max(t.srtt >> 3, 1); // QEMU RTT≈0,钳到 1ms
    let w_est = t.w_max * BETA_NUM / BETA_DEN
        + (9 * (t_since / rtt) * CWND_SCALE as u64 / 17) as u32;
    if t.cwnd < w_est {
        target = core::cmp::max(target, w_est);
    }

    // 把「目标窗口 − 当前窗口」摊薄到 ACK 流上:每 cnt 个 ACK +1 段,
    // 其中 cnt = cwnd/(target − cwnd),实现逐 ACK 平滑逼近目标曲线
    if target == t.cwnd {
        // 平台期(刚起步):极慢探测
        t.cwnd_cnt += acked;
        if t.cwnd_cnt >= 100 {
            t.cwnd += CWND_SCALE;
            t.cwnd_cnt = 0;
        }
    } else {
        let cnt = (t.cwnd / (target - t.cwnd)).clamp(1, 1000);
        t.cwnd_cnt += acked;
        if t.cwnd_cnt >= cnt {
            t.cwnd += CWND_SCALE;
            t.cwnd_cnt = 0;
        }
    }
}

/// 丢包事件:乘性减窗 β=0.7 + 快速收敛,重启 epoch
/// 教学版不区分 RTO 与快速重传的降窗;真实 Linux 在 RTO 后会把
/// cwnd 打到 1 段重走慢启动,想更真实可在 tick 的 RTO 分支里加:
///     t.cwnd = CWND_SCALE; t.ssthresh 保持 cubic_on_loss 的结果。
fn cubic_on_loss(t: &mut TcpSocket, why: &str) {
    let old = t.cwnd;
    // 快速收敛:这次低谷比上次还低,说明瓶颈容量在缩,last_max 再压一档
    if FAST_CONV && t.cwnd < t.last_max {
        t.last_max = t.cwnd * (BETA_DEN + BETA_NUM) / (2 * BETA_DEN); // ×0.85
    } else {
        t.last_max = t.cwnd;
    }
    t.w_max = t.cwnd;
    t.ssthresh = core::cmp::max(t.cwnd * BETA_NUM / BETA_DEN, 2 * CWND_SCALE);
    t.cwnd = t.ssthresh;
    t.epoch_start = 0; // 下一个 ACK 重启 epoch 并算 K
    t.cwnd_cnt = 0;
    log::info!(
        "[cubic] t={} loss({}) cwnd {} -> {} (w_max={}, K on next ack)",
        now_ms(), why, old / CWND_SCALE, t.cwnd / CWND_SCALE, t.w_max / CWND_SCALE
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
    let mut pseudo = Vec::with_capacity(12);
    pseudo.extend_from_slice(&src_ip.to_be_bytes());
    pseudo.extend_from_slice(&dst_ip.to_be_bytes());
    pseudo.push(0);
    pseudo.push(6);
    pseudo.extend_from_slice(&(pkt.len() as u16).to_be_bytes());
    let mut sum: u32 = ip_checksum(&pseudo) as u32 ^ 0xFFFF;
    sum += ip_checksum(pkt) as u32 ^ 0xFFFF;
    while (sum >> 16) != 0 {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }
    !(sum as u16)
}

fn send_segment(local_port: u16, remote: SocketAddr, seq: u32, ack: u32, flags: u8, payload: &[u8]) {
    if LOSS_EVERY > 0 && flags & (SYN | FIN) == 0 && !payload.is_empty() {
        static SEND_CNT: AtomicU32 = AtomicU32::new(0);
        if SEND_CNT.fetch_add(1, Ordering::Relaxed) % LOSS_EVERY == LOSS_EVERY - 1 {
            log::info!("[tcp] t={} drop! seq={}", now_ms(), seq);
            return;
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
    h.window = 65535u16.to_be();
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

/// 处理累积 ACK,返回本次新确认的段数(供 CUBIC 增窗)
fn process_ack(t: &mut TcpSocket, ack: u32) -> u32 {
    let now = now_ms();
    let mut progress = false;
    let mut clean_sample: Option<u64> = None;
    let mut acked = 0u32;
    let mut first = true;
    while let Some(seg) = t.tx_unacked.front() {
        if ack >= seg.end_seq() {
            t.snd_una = seg.end_seq();
            let seg = t.tx_unacked.pop_front().unwrap();
            if first && !seg.retransmitted {
                clean_sample = Some(now.saturating_sub(seg.sent_ms));
            }
            first = false;
            progress = true;
            acked += 1;
        } else {
            break;
        }
    }
    if progress {
        t.retries = 0;
        t.dup_acks = 0; // 前向进展:重复 ACK 计数清零
        if let Some(r) = clean_sample {
            rtt_update(t, r);
        } else {
            t.rto_ms = compute_rto(t);
        }
        t.rto_deadline = if t.tx_unacked.is_empty() { 0 } else { now + t.rto_ms };
    }
    acked
}

// ---------- 接收路径 ----------

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

    let mut idx: Option<usize> = None;
    for (i, s) in table.iter().enumerate() {
        if let Some(Socket::Tcp(t)) = s {
            if t.state != TcpState::Listen && t.local_port == dst_port && t.remote == Some(remote) {
                idx = Some(i);
                break;
            }
        }
    }
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
        None => return,
    };
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

    let mut push_accept: Option<(usize, usize)> = None;
    let mut free_slot = false;
    {
        let t = match table[i].as_mut().and_then(as_tcp_mut) {
            Some(t) => t,
            None => return,
        };

        if flags & RST != 0 {
            t.state = TcpState::Closed;
            t.rst = true;
            t.rx_queue.push_back(Vec::new());
        } else {
            t.snd_wnd = wnd;
            match t.state {
                TcpState::SynSent => {
                    if flags & (SYN | ACK) == SYN | ACK && ack == t.snd_nxt {
                        t.rcv_nxt = seq.wrapping_add(1);
                        process_ack(t, ack);
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
                            ack_now = true; // 乱序:丢弃 + 重 ACK(即对端的 dup ACK 源)
                        }
                    }
                    let acked = process_ack(t, ack);
                    if acked > 0 {
                        cubic_update(t, acked); // CUBIC:按新确认的段数增窗
                    } else if flags & ACK != 0
                        && ack == t.snd_una
                        && payload.is_empty()
                        && flags & FIN == 0
                        && !t.tx_unacked.is_empty()
                    {
                        // 重复 ACK(RFC 5681):第 3 个触发快速重传
                        t.dup_acks = t.dup_acks.saturating_add(1);
                        if t.dup_acks == DUPACK_THRESH {
                            let seg = t.tx_unacked.front_mut().unwrap();
                            seg.retransmitted = true;
                            seg.sent_ms = now_ms();
                            let (sq, fl, data) = (seg.seq, seg.flags, seg.data.clone());
                            let (lp, r, rn) = (t.local_port, t.remote.unwrap(), t.rcv_nxt);
                            // 一个丢包 episode 只降一次窗
                            if t.loss_seq != t.snd_una {
                                cubic_on_loss(t, "3dupACK");
                                t.loss_seq = t.snd_una;
                            }
                            send_segment(lp, r, sq, rn, fl, &data);
                        }
                    }
                    match t.state {
                        TcpState::FinWait1 if t.snd_una == t.snd_nxt => {
                            t.state = TcpState::FinWait2;
                        }
                        TcpState::Closing if t.snd_una == t.snd_nxt => {
                            t.time_wait_deadline = now_ms() + TIME_WAIT_MS;
                            t.state = TcpState::TimeWait;
                        }
                        TcpState::LastAck if t.snd_una == t.snd_nxt => {
                            free_slot = true;
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

// ---------- tick ----------

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
                    // RTO 也是丢包事件:同一个 episode 只降一次窗
                    if t.loss_seq != t.snd_una {
                        cubic_on_loss(t, "RTO");
                        t.loss_seq = t.snd_una;
                    }
                    let seg = t.tx_unacked.front_mut().unwrap();
                    seg.retransmitted = true;
                    seg.sent_ms = now;
                    let (seq, flags, data) = (seg.seq, seg.flags, seg.data.clone());
                    let (lp, rn) = (t.local_port, t.rcv_nxt);
                    send_segment(lp, remote, seq, rn, flags, &data);
                    t.retries = t.retries.saturating_add(1);
                    log::info!("[tcp] t={} rtx seq={} retry={} rto={}", now, seq, t.retries, t.rto_ms);
                    t.rto_ms = (t.rto_ms * 2).min(RTO_MAX_MS);
                    t.rto_deadline = now + t.rto_ms;
                    if t.retries > MAX_RETRIES {
                        t.rto_deadline = 0;
                        if t.state == TcpState::SynReceived {
                            free = true;
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

// ---------- 对外 API ----------

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
    }
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
            log::info!("[tcp] connect timeout");
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
            // 有效发送窗口 = min(对端接收窗口, 拥塞窗口)。
            // cwnd 以段为单位管理,换算成字节再与在途量比较。
            let cwnd_bytes = (t.cwnd / CWND_SCALE) as usize * MAX_PAYLOAD;
            let eff_wnd = (t.snd_wnd as usize).min(cwnd_bytes);
            let in_flight = t.snd_nxt.wrapping_sub(t.snd_una) as usize;
            if in_flight < eff_wnd {
                let len = data.len()
                    .min(MAX_PAYLOAD)
                    .min(eff_wnd - in_flight);
                if len == 0 {
                    return -1;
                }
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
            // 窗口满:解锁 poll,等 ACK 推进或 CUBIC 开窗
        }
        crate::drivers::net::poll();
        spins += 1;
        if spins > 50_000_000 {
            return -1;
        }
    }
}

pub fn recv_data(fd: usize, out: &mut [u8]) -> isize {
    loop {
        crate::drivers::net::poll();
        {
            let mut table = SOCKET_TABLE.lock();
            match table.get_mut(fd).and_then(|s| s.as_mut()).and_then(as_tcp_mut) {
                Some(t) => {
                    if let Some(chunk) = t.rx_queue.pop_front() {
                        if chunk.is_empty() && t.rst {
                            return -1;
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