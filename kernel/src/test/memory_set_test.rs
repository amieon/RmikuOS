use crate::arch::MEMORY_START;
use crate::mm::{
    kernel_phys_to_virt, MemorySet, PhysAddr, VirtAddr,
};

pub fn memory_set_test() {
    let ms = MemorySet::new_kernel();

    let va = VirtAddr(kernel_phys_to_virt(MEMORY_START));
    let pte = ms
        .translate(va.floor())
        .expect("kernel mapping translate failed");

    assert_eq!(pte.ppn(), PhysAddr::from(MEMORY_START).floor());

    log::info!("[mm] MemorySet test passed");
}