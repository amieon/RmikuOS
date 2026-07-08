// src/arch/riscv64/ipi.rs

use core::sync::atomic::{AtomicBool, AtomicU64, Ordering};
use crate::arch::MAX_HARTS;
use crate::HART_LOCALS;

/// IPI 类型（与 LoongArch 统一名称）
#[derive(Clone, Copy, PartialEq, Eq)]
pub enum IpiKind {
    /// 有新线程就绪，重新调度
    Reschedule = 0,
    /// 强制当前线程退出（kill）
    ForceExit = 1,
    /// 信号到达
    Signal = 2,
}

/// per‑hart IPI 信箱，每一位代表一种等待处理的 IPI 类型
pub static IPI_MAILBOX: [AtomicU64; MAX_HARTS] = [
    AtomicU64::new(0), AtomicU64::new(0), AtomicU64::new(0), AtomicU64::new(0),
    AtomicU64::new(0), AtomicU64::new(0), AtomicU64::new(0), AtomicU64::new(0),
];

// ---------------------------------------------------------------------------
// SBI 调用私有实现
// ---------------------------------------------------------------------------

/// SBI "IPI" 扩展 ID（EID）
const SBI_EXT_IPI: usize = 0x735049;
/// `sbi_send_ipi` 功能 ID（FID）
const SBI_SEND_IPI_FID: usize = 0;

/// 向一组 hart 发送软件中断
fn sbi_send_ipi(hart_mask: usize, hart_mask_base: usize) -> isize {
    let (error, _): (isize, usize);
    unsafe {
        core::arch::asm!(
            "ecall",
            inlateout("a0") 0isize => error,
            in("a1") hart_mask,       // 实际上参数顺序: a0=hart_mask, a1=hart_mask_base
            in("a2") 0usize,          // 未使用
            in("a6") SBI_SEND_IPI_FID,
            in("a7") SBI_EXT_IPI,
            options(nostack),
        );
    }
    error
}

/// SBI "RFENCE" 扩展 ID（EID）
const SBI_EXT_RFENCE: usize = 0x52464E43;
/// `sbi_remote_sfence_vma` 功能 ID（FID）
const SBI_REMOTE_SFENCE_VMA_FID: usize = 0;

/// 远程 sfence.vma
fn sbi_remote_sfence_vma(hart_mask: usize, hart_mask_base: usize) -> isize {
    let (error, _): (isize, usize);
    unsafe {
        core::arch::asm!(
            "ecall",
            inlateout("a0") 0isize => error,
            in("a1") hart_mask,
            in("a6") SBI_REMOTE_SFENCE_VMA_FID,
            in("a7") SBI_EXT_RFENCE,
            options(nostack),
        );
    }
    error
}

// ---------------------------------------------------------------------------
// 公用接口
// ---------------------------------------------------------------------------

/// 向目标 hart 发送 IPI（kind 标识类型，data 暂时保留）
pub fn send_ipi(hart: usize, kind: IpiKind, _data: usize) {
    if hart >= MAX_HARTS {
        return;
    }
    // 设置信箱 bit
    IPI_MAILBOX[hart].fetch_or(1u64 << (kind as u64), Ordering::Release);
    // 触发软件中断
    let mask = 1usize << hart;
    sbi_send_ipi(mask, 0);
}

/// 向所有已就绪的核（除了自己）发送 IPI
pub fn send_ipi_to_others(kind: IpiKind, data: usize) {
    let my_hart = crate::arch::hartid();
    for i in 0..MAX_HARTS {
        if i == my_hart {
            continue;
        }
        if HART_LOCALS[i].ready.load(Ordering::Acquire) {
            send_ipi(i, kind, data);
        }
    }
}

/// 处理当前 hart 的 IPI（在中断上下文调用）
/// 返回 true 表示需要重新调度
pub fn handle_ipi() -> bool {
    let hart = crate::arch::hartid();
    let bits = IPI_MAILBOX[hart].swap(0, Ordering::Acquire);
    if bits == 0 {
        return false;
    }
    let mut need_resched = false;

    if bits & (1u64 << IpiKind::Reschedule as u64) != 0 {
        need_resched = true;
    }
    if bits & (1u64 << IpiKind::ForceExit as u64) != 0 {
        crate::task::set_current_force_exit(true);
        need_resched = true;
    }
    if bits & (1u64 << IpiKind::Signal as u64) != 0 {
        need_resched = true;
    }
    need_resched
}

/// 开机时清空当前核信箱
pub fn clear_current_ipi() {
    let hart = crate::arch::hartid();
    IPI_MAILBOX[hart].store(0, Ordering::Release);
}

// ---------------------------------------------------------------------------
// TLB shootdown 接口（直接调用 SBI 远程 fence）
// ---------------------------------------------------------------------------

/// 无等待的 TLB 广播：自己 sfence.vma 并向其它核发起远程 sfence.vma（异步）
pub fn tlb_shootdown_broadcast() {
    // 刷新自己
    crate::arch::flush_tlb();
    let my_hart = crate::arch::hartid();
    let mut mask = 0usize;
    for i in 0..MAX_HARTS {
        if i == my_hart {
            continue;
        }
        if HART_LOCALS[i].ready.load(Ordering::Acquire) {
            mask |= 1 << i;
        }
    }
    if mask != 0 {
        sbi_remote_sfence_vma(mask, 0);
    }
}

/// 同步 TLB 广播：等待所有核完成后再返回（SBI 调用本身就阻塞）
pub fn tlb_shootdown_sync() {
    // 和广播一样，sbi_remote_sfence_vma 在 OpenSBI 里是同步的
    tlb_shootdown_broadcast();
}