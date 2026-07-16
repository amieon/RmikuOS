 // src/arch/riscv64/mod.rs
pub const NAME: &str = "RISC-V 64";
pub const UART_PADDR: usize = 0x1000_0000;
pub const UART_BASE: usize = crate::mm::config::KERNEL_OFFSET + UART_PADDR;
pub const MAX_HARTS: usize = 8;


/// QEMU virt DRAM starts at 0x8000_0000.
pub const MEMORY_START: usize = 0x8000_0000;

/// run.sh uses `-m 128M` for RISC-V.
pub const MEMORY_SIZE: usize = 512 * 1024 * 1024;
pub const KERNEL_DIRECT_MAP_SIZE: usize = 512 * 1024 * 1024;

pub const MEMORY_END: usize = MEMORY_START + MEMORY_SIZE;



pub const VIRTIO_MMIO_BASE: usize = 0x1000_1000;
pub const VIRTIO_MMIO_STRIDE: usize = 0x1000;
pub const VIRTIO_MMIO_COUNT: usize = 8;
pub const VIRTIO_MMIO_SIZE: usize = VIRTIO_MMIO_STRIDE * VIRTIO_MMIO_COUNT;


pub const PCI_ECAM_BASE: usize = 0x3000_0000;   // ECAM，256MB
pub const PCI_MMIO_BASE: usize = 0x4000_0000;   // MMIO 窗口，1GB
pub const PCI_MMIO_END:  usize = 0x8000_0000;
pub const PCI_ECAM_SIZE: usize = 0x1000_0000;   // 256MB
pub const PCI_MMIO_SIZE: usize = 0x4000_0000;   // 1GB


pub mod shutdown;
pub use shutdown::shutdown;
pub mod ipi;      
pub use ipi::tlb_shootdown_broadcast;
pub use ipi::tlb_shootdown_sync;

/// 读取当前核的 hartid
/// 在 boot.S 里已经把 hartid 存到了 tp 寄存器
#[inline]
pub fn hartid() -> usize {
    let id: usize;
    unsafe {
        core::arch::asm!("mv {}, tp", out(reg) id);
    }
    id
}

pub fn enable_interrupt() {
    unsafe {
        //sstatus.SIE = bit 1
        core::arch::asm!(
            "csrs sstatus, {0}",
            in(reg) 1usize << 1,
            options(nostack)
        );
    }
}

pub fn disable_interrupt() {
    unsafe {
        //clear sstatus.SIE
        core::arch::asm!(
            "csrc sstatus, {0}",
            in(reg) 1usize << 1,
            options(nostack)
        );
    }
}

pub fn wait_for_interrupt() {
    unsafe {
        core::arch::asm!("wfi", options(nostack));
    }
}

#[inline]
pub fn flush_tlb() {
    unsafe {
        core::arch::asm!(
            "sfence.vma",
            options(nostack)
        );
    }
}

pub fn current_hart_id() -> usize {
    let hartid: usize;
    unsafe { core::arch::asm!("mv {}, tp", out(reg) hartid, options(nostack)) };
    hartid
}


/// 读取 sstatus.SIE 位
#[inline]
pub fn intr_get() -> bool {
    let sstatus: usize;
    unsafe {
        core::arch::asm!("csrr {}, sstatus", out(reg) sstatus);
    }
    sstatus & (1 << 1) != 0  // SIE 是 bit 1
}

/// 开启 S-mode 中断（设置 SIE 位）
#[inline]
pub fn intr_enable() {
    unsafe {
        // csrs: CSR Set (用寄存器指定要设置的位)
        core::arch::asm!("csrs sstatus, {}", in(reg) 1 << 1);
    }
}

/// 关闭 S-mode 中断（清除 SIE 位）
#[inline]
pub fn intr_disable() {
    unsafe {
        // csrc: CSR Clear (用寄存器指定要清除的位)
        core::arch::asm!("csrc sstatus, {}", in(reg) 1 << 1);
    }
}