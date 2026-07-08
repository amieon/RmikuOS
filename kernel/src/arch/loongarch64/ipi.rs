use core::sync::atomic::{AtomicBool, AtomicU64, Ordering};
use crate::arch::MAX_HARTS;
use crate::HART_LOCALS;

/// IPI 类型
#[derive(Clone, Copy, PartialEq, Eq)]
pub enum IpiKind {
    /// 紧急：有新线程就绪，请检查调度
    Reschedule = 0,
    /// 刷新整个 TLB（不等待 ACK，接收方直接刷）
    TlbShootdown = 1,
    /// 刷新 TLB 并等待所有核 ACK（用于释放物理页时）
    TlbShootdownAck = 2,
    /// 强制当前线程退出
    ForceExit = 3,
    /// 信号到达，需要处理
    Signal = 4,
}

/// 每个核的 IPI 信箱，每个 bit 代表一种未处理的 IPI 类型
pub static IPI_MAILBOX: [AtomicU64; MAX_HARTS] = [
    AtomicU64::new(0), AtomicU64::new(0), AtomicU64::new(0), AtomicU64::new(0),
    AtomicU64::new(0), AtomicU64::new(0), AtomicU64::new(0), AtomicU64::new(0),
];

/// TLB shootdown 的 ACK 标志（仅用于带 ACK 的 shootdown）
static TLB_ACK: [AtomicBool; MAX_HARTS] = [
    AtomicBool::new(false), AtomicBool::new(false), AtomicBool::new(false), AtomicBool::new(false),
    AtomicBool::new(false), AtomicBool::new(false), AtomicBool::new(false), AtomicBool::new(false),
];

/// 向目标 hart 发送 IPI
/// `kind` 表示 IPI 类型，`data` 为可选附加数据（如信号编号等）
pub fn send_ipi(hart: usize, kind: IpiKind, _data: usize) {
    if hart >= MAX_HARTS { return; }
    // 设置信箱 bit
    IPI_MAILBOX[hart].fetch_or(1u64 << (kind as u64), Ordering::Release);
    // 向目标核写入 MAILBOX0 触发中断
    unsafe {
        // LoongArch: 向 CSR 0x48 (MAILBOX0) 写入 (hart_id << 16) | 1 即可向该核发中断
        let val = (hart << 16) | 1;
        core::arch::asm!("csrwr {0}, 0x48", in(reg) val);
    }
}

/// 向所有已就绪的核（除了自己）发送 IPI
pub fn send_ipi_to_others(kind: IpiKind, data: usize) {
    let my_hart = super::current_hart_id();
    for i in 0..MAX_HARTS {
        if i == my_hart { continue; }
        if HART_LOCALS[i].ready.load(Ordering::Acquire) {
            send_ipi(i, kind, data);
        }
    }
}

/// 处理当前核的 IPI（在中断上下文调用）
/// 返回值：是否需要重新调度
pub fn handle_ipi() -> bool {
    let hart = super::current_hart_id();
    let bits = IPI_MAILBOX[hart].swap(0, Ordering::Acquire);
    if bits == 0 {
        return false;
    }
    let mut need_resched = false;
    // Reschedule
    if bits & (1u64 << IpiKind::Reschedule as u64) != 0 {
        need_resched = true;
    }
    // TLB Shootdown （无 ACK 版本，直接刷新）
    if bits & (1u64 << IpiKind::TlbShootdown as u64) != 0 {
        super::flush_tlb();
    }
    // TLB Shootdown with ACK
    if bits & (1u64 << IpiKind::TlbShootdownAck as u64) != 0 {
        super::flush_tlb();
        // 通知发送方已刷新
        TLB_ACK[hart].store(true, Ordering::Release);
    }
    // ForceExit
    if bits & (1u64 << IpiKind::ForceExit as u64) != 0 {
        // 由 trap 返回前检查
        crate::task::set_current_force_exit(true);
        need_resched = true;
    }
    // Signal
    if bits & (1u64 << IpiKind::Signal as u64) != 0 {
        // 信号 pending 由 per-thread 数据设置，这里只需标记需要重调度就行
        need_resched = true;
    }
    need_resched
}

/// 清空当前核 IPI（开机时调用）
pub fn clear_current_ipi() {
    let hart = super::current_hart_id();
    IPI_MAILBOX[hart].store(0, Ordering::Release);
}

// ========== TLB shootdown 接口 ==========

/// 无等待的 TLB 广播：自己刷，并通知其他所有核立即刷
pub fn tlb_shootdown_broadcast() {
    super::flush_tlb();
    send_ipi_to_others(IpiKind::TlbShootdown, 0);
}

/// 带 ACK 的 TLB 广播：等待其他核都刷完才返回
/// 用于释放物理页前，确保没有核还缓存着旧映射
pub fn tlb_shootdown_sync() {
    let my_hart = super::current_hart_id();
    // 先刷自己
    super::flush_tlb();
    // 重置所有核的 ACK
    for i in 0..MAX_HARTS {
        if i != my_hart && HART_LOCALS[i].ready.load(Ordering::Acquire) {
            TLB_ACK[i].store(false, Ordering::Release);
            send_ipi(i, IpiKind::TlbShootdownAck, 0);
        }
    }
    // 等待所有目标核完成
    for i in 0..MAX_HARTS {
        if i == my_hart { continue; }
        if HART_LOCALS[i].ready.load(Ordering::Acquire) {
            while !TLB_ACK[i].load(Ordering::Acquire) {
                core::hint::spin_loop();
            }
        }
    }
}