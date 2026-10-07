#[cfg(target_arch = "riscv64")]
pub fn page_table_test() {
    use crate::mm::page_table::{PageTable, PteFlags};
    use crate::mm::{PhysAddr, VirtAddr};

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

    pt.unmap(va.floor());
    assert!(pt.translate(va.floor()).is_none());

    let mut range_pt = PageTable::new();
    let range_va = VirtAddr(0);
    let range_pa = PhysAddr(0);
    let range_size = 5usize << 29; // 2.5GiB
    let leaf_count = range_pt.map_range(
        range_va,
        range_pa,
        range_size,
        PteFlags::R
            .union(PteFlags::W)
            .union(PteFlags::A)
            .union(PteFlags::D),
    );

    assert_eq!(leaf_count, 2 + 256);
    assert!(range_pt.contains_va(range_va));
    assert!(range_pt.contains_va(VirtAddr((1usize << 30) - 1)));
    assert!(range_pt.contains_va(VirtAddr(1usize << 30)));
    assert!(range_pt.contains_va(VirtAddr(2usize << 30)));
    assert!(range_pt.contains_va(VirtAddr(range_size - 1)));
    assert!(!range_pt.contains_va(VirtAddr(range_size)));
    assert!(range_pt.translate(range_va.floor()).is_none());

    log::info!("[mm] RISC-V page table test passed");
}

#[cfg(target_arch = "loongarch64")]
pub fn page_table_test() {
    use crate::mm::page_table::{kernel_rwx_flags, PageTable};
    use crate::mm::{PhysAddr, VirtAddr};

    let mut pt = PageTable::new();

    let va = VirtAddr(crate::arch::MEMORY_START);
    let pa = PhysAddr(crate::arch::MEMORY_START);

    pt.map(
        va.floor(),
        pa.floor(),
        kernel_rwx_flags(),
    );

    let pte = pt.translate(va.floor()).expect("translate failed");
    assert_eq!(pte.ppn(), pa.floor());

    assert!(pte.is_valid());
    assert!(pte.is_present());
    assert!(pte.readable());
    assert!(pte.writable());
    assert!(pte.executable());

    pt.unmap(va.floor());
    assert!(pt.translate(va.floor()).is_none());

    let mut range_pt = PageTable::new();
    let range_va = VirtAddr(0);
    let range_pa = PhysAddr(0);
    let range_size = 5usize << 29; // 2.5GiB
    let leaf_count = range_pt.map_range(
        range_va,
        range_pa,
        range_size,
        kernel_rwx_flags(),
    );

    assert_eq!(leaf_count, 2 + 256);
    assert!(range_pt.contains_va(range_va));
    assert!(range_pt.contains_va(VirtAddr((1usize << 30) - 1)));
    assert!(range_pt.contains_va(VirtAddr(1usize << 30)));
    assert!(range_pt.contains_va(VirtAddr(2usize << 30)));
    assert!(range_pt.contains_va(VirtAddr(range_size - 1)));
    assert!(!range_pt.contains_va(VirtAddr(range_size)));
    assert!(range_pt.translate(range_va.floor()).is_none());

    log::info!("[mm] LoongArch page table test passed");
}