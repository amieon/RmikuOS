use alloc::vec::Vec;

use crate::mm::{
    alloc_frame, phys_to_virt, PhysAddr, PhysPageNum, VirtAddr, VirtPageNum,
    FrameTracker, KERNEL_OFFSET, MEMORY_END, MEMORY_START, PAGE_SIZE,
};
use crate::mm::page_table::{PageTable, PteFlags};



pub struct MemorySet {
    page_table: PageTable,
    areas: Vec<MapArea>,
}

impl MemorySet {
    pub fn new_bare() -> Self {
        Self {
            page_table: PageTable::new(),
            areas: Vec::new(),
        }
    }

    pub fn root_ppn(&self) -> PhysPageNum {
        self.page_table.root_ppn()
    }

    pub fn insert_area(&mut self, mut area: MapArea) {
        area.map(&mut self.page_table);
        self.areas.push(area);
    }

    pub fn new_kernel() -> Self {
        let mut memory_set = Self::new_bare();

        let kernel_perm = MapPermission::R
            .union(MapPermission::W)
            .union(MapPermission::X);

        let rw_perm = MapPermission::R
            .union(MapPermission::W);

        memory_set.insert_area(MapArea::new(
            VirtAddr(phys_to_virt(MEMORY_START)),
            VirtAddr(phys_to_virt(MEMORY_END)),
            MapType::Linear {
                offset: KERNEL_OFFSET,
            },
            kernel_perm,
        ));

        memory_set.insert_area(MapArea::new(
            VirtAddr(crate::arch::UART_BASE),
            VirtAddr(crate::arch::UART_BASE + PAGE_SIZE),
            MapType::Linear {
                offset: KERNEL_OFFSET,
            },
            rw_perm,
        ));

        memory_set
    }

    pub fn translate(&self, vpn: VirtPageNum) -> Option<crate::mm::page_table::PageTableEntry> {
        self.page_table.translate(vpn)
    }
}

#[cfg(target_arch = "riscv64")]
fn map_perm_to_pte_flags(permission: MapPermission) -> PteFlags {
    let mut flags = PteFlags::empty();

    if permission.contains(MapPermission::R) {
        flags = flags.union(PteFlags::R);
    }
    if permission.contains(MapPermission::W) {
        flags = flags.union(PteFlags::W);
    }
    if permission.contains(MapPermission::X) {
        flags = flags.union(PteFlags::X);
    }
    if permission.contains(MapPermission::U) {
        flags = flags.union(PteFlags::U);
    }

    
    //第一版直接置 A/D，避免硬件或 QEMU 因 A/D 位触发异常。
    flags.union(PteFlags::A).union(PteFlags::D)
}

#[cfg(target_arch = "loongarch64")]
fn map_perm_to_pte_flags(permission: MapPermission) -> PteFlags {
    let mut flags = PteFlags::MAT_CC.union(PteFlags::G);

    if permission.contains(MapPermission::W) {
        flags = flags.union(PteFlags::W).union(PteFlags::D);
    }

    if !permission.contains(MapPermission::R) {
        flags = flags.union(PteFlags::NR);
    }

    if !permission.contains(MapPermission::X) {
        flags = flags.union(PteFlags::NX);
    }

    if permission.contains(MapPermission::U) {
        flags = flags.union(PteFlags::PLV3);
    } else {
        flags = flags.union(PteFlags::PLV0);
    }

    flags
}