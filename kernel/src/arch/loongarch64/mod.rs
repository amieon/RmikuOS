// src/arch/loongarch64/mod.rs
pub mod boot; 
pub mod shutdown;
pub use shutdown::shutdown;
pub const NAME: &str = "LoongArch 64";
pub const MAX_HARTS: usize = 8;

/// The kernel is loaded at 0x0100_0000 by the QEMU loader in run.sh.
pub const MEMORY_START: usize = 0x0100_0000;

/// run.sh uses `-m 2G` for LoongArch.
pub const MEMORY_SIZE: usize = 2 * 1024 * 1024 * 1024;

pub const KERNEL_DIRECT_MAP_SIZE: usize = 2 * 1024 * 1024 * 1024;


pub const MEMORY_END: usize = MEMORY_START + MEMORY_SIZE;

pub const UART_PADDR: usize = 0x1fe0_01e0;
pub const UART_BASE: usize = crate::mm::config::KERNEL_OFFSET + UART_PADDR;


pub const PCI_ECAM_BASE: usize = 0x2000_0000;
pub const PCI_ECAM_SIZE: usize = 0x0800_0000;
pub const PCI_MMIO_BASE: usize = 0x4000_0000;
pub const PCI_MMIO_SIZE: usize = 0x4000_0000;
pub const PCI_IO_BASE: usize = 0x1804_0000;
pub const PCI_IO_SIZE: usize = 0x0001_0000;


pub mod ipi;           
pub use ipi::tlb_shootdown_broadcast;
pub use ipi::tlb_shootdown_sync;

/// 读取当前核的 CPUID
#[inline]
pub fn hartid() -> usize {
    let id: usize;
    unsafe {
        core::arch::asm!("csrrd {}, 0x20", out(reg) id);
    }
    id
}

pub fn enable_interrupt() {
    let mut crmd: usize;

    unsafe {

        core::arch::asm!(
            "csrrd {0}, 0x0",
            out(reg) crmd,
            options(nostack)
        );

        crmd |= 1usize << 2;

        core::arch::asm!(
            "csrwr {0}, 0x0",
            inout(reg) crmd => _,
            options(nostack)
        );
    }
}

pub fn disable_interrupt() {
    let mut crmd: usize;

    unsafe {
        core::arch::asm!(
            "csrrd {0}, 0x0",
            out(reg) crmd,
            options(nostack)
        );

        crmd &= !(1usize << 2);

        core::arch::asm!(
            "csrwr {0}, 0x0",
            inout(reg) crmd => _,
            options(nostack)
        );
    }
}

pub fn wait_for_interrupt() {
    unsafe {
        core::arch::asm!("idle 0", options(nostack));
    }
}

#[inline]
pub fn flush_tlb() {
    unsafe {
        core::arch::asm!(
            "invtlb 0x0, $zero, $zero",
            options(nostack)
        );
    }
}

pub fn current_hart_id() -> usize {
    let hartid: usize;
    unsafe {
        core::arch::asm!("csrrd {}, 0x20", out(reg) hartid, options(nostack));
    }
    hartid & 0x1FF  // 取 CoreID 低 9 位
}


/// 读取 CRMD.IE 位，判断中断当前是否开启
///
/// LoongArch 中，CRMD（Control Register for Mode）寄存器（CSR 0x0）：
///   bit 0: PG   (页表映射使能)
///   bit 1: DA   (直接地址翻译使能)
///   bit 2: IE   (全局中断使能)
#[inline]
pub fn intr_get() -> bool {
    let crmd: usize;
    unsafe {
        // csrrd: 读 CSR 寄存器到通用寄存器
        // 0x0 是 CRMD 的 CSR 编号
        core::arch::asm!("csrrd {}, 0x0", out(reg) crmd);
    }
    crmd & 0x4 != 0          // IE = bit 2，掩码 0x4
}

/// 关闭全局中断（清除 CRMD.IE 位）
///
/// 使用 bstrins.d 指令将 $zero（恒为 0）的 bit 0 插入到 CRMD 的 bit 2，
/// 即清零 bit 2，其余位保持不变。
#[inline]
pub fn intr_disable() {
    unsafe {
        core::arch::asm!(
            "csrrd    {tmp}, 0x0",       // tmp = CRMD
            "bstrins.d {tmp}, $zero, 2, 2", // tmp[2:2] = 0   (清零 IE)
            "csrwr    {tmp}, 0x0",       // CRMD = tmp
            tmp = out(reg) _,
        );
    }
}

/// 打开全局中断（置位 CRMD.IE 位）
///
/// 用 ori 指令将 bit 2 置 1，0x4 在 12 位立即数范围内，一条指令搞定。
#[inline]
pub fn intr_enable() {
    unsafe {
        core::arch::asm!(
            "csrrd    {tmp}, 0x0",       // tmp = CRMD
            "ori      {tmp}, {tmp}, 0x4", // tmp |= 0x4   (置位 IE)
            "csrwr    {tmp}, 0x0",       // CRMD = tmp
            tmp = out(reg) _,
        );
    }
}