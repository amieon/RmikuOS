use crate::arch::MEMORY_START;
use crate::mm::{
    kernel_phys_to_virt, MemorySet, VirtAddr,
};

pub fn memory_set_test() {
    let ms = MemorySet::new_kernel();

    let va = VirtAddr(kernel_phys_to_virt(MEMORY_START));

    #[cfg(target_arch = "riscv64")]
    {
        assert!(
            ms.contains_va(va),
            "kernel huge mapping does not cover MEMORY_START"
        );
    }

    #[cfg(target_arch = "loongarch64")]
    {
        assert!(
            ms.translate(va.floor()).is_none(),
            "LoongArch kernel direct map is synthesized by TLB refill, not page-table PTEs"
        );
    }

    log::info!("[mm] MemorySet test passed");
}