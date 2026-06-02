// src/arch/loongarch64/mod.rs
pub mod boot; 
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