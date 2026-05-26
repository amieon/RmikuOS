pub fn riscv64_page_table_test() {
    use crate::mm::page_table::{PageTable,PteFlags};
    use crate::mm::{PhysAddr,VirtAddr};

    let mut pt = PageTable::new();

    let va = VirtAddr(0x8020_0000);
    let pa = PhysAddr(0x8020_0000);

    pt.map(
        va.floor(),
        pa.floor(),
        PteFlags::R
            .union(PteFlags::W)
            .union(PteFlags::X)
            .union(PteFlags::A)
            .union(PteFlags::D),
    );

    let pte = pt.translate(va.floor()).expect("translate failed");
    assert_eq!(pte.ppn(), pa.floor());

    log::info!("[mm] page table test passed");
}