use crate::mm::{VirtAddr,PhysAddr,MemorySet,MEMORY_START,phys_to_virt};

pub fn memory_set_test() {
    let ms = MemorySet::new_kernel();

    let va = VirtAddr(phys_to_virt(MEMORY_START));
    let pte = ms
        .translate(va.floor())
        .expect("kernel direct map translate failed");

    assert_eq!(pte.ppn(), PhysAddr::from(MEMORY_START).floor());

    log::info!("[mm] MemorySet test passed");
}