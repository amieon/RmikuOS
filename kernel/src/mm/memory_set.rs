use alloc::vec::Vec;

use crate::mm::{
    alloc_frame, phys_to_virt, PhysAddr, PhysPageNum, VirtAddr, VirtPageNum,
    FrameTracker, KERNEL_OFFSET, PAGE_SIZE,
};
use crate::arch::{MEMORY_END, MEMORY_START};
use crate::mm::page_table::{PageTable, PteFlags};
use crate::mm::map_area::{MapArea,MapPermission,MapType};


pub struct MemorySet {
    page_table: PageTable,
    areas: Vec<MapArea>,
}


impl MemorySet {
    #[cfg(target_arch = "riscv64")]
    pub fn new_kernel() -> Self {
        let mut memory_set = Self::new_bare();

        let kernel_perm = MapPermission::R
            .union(MapPermission::W)
            .union(MapPermission::X);

        let rw_perm = MapPermission::R
            .union(MapPermission::W);

        memory_set.insert_area(MapArea::new(
            VirtAddr(crate::mm::phys_to_virt(crate::arch::MEMORY_START)),
            VirtAddr(crate::mm::phys_to_virt(crate::arch::MEMORY_END)),
            MapType::Linear {
                offset: crate::mm::KERNEL_OFFSET,
            },
            kernel_perm,
        ));

        memory_set.insert_area(MapArea::new(
            VirtAddr(crate::arch::UART_BASE),
            VirtAddr(crate::arch::UART_BASE + PAGE_SIZE),
            MapType::Linear {
                offset: crate::mm::KERNEL_OFFSET,
            },
            rw_perm,
        ));

        memory_set
    }

    #[cfg(target_arch = "loongarch64")]
    pub fn new_kernel() -> Self {
        let mut memory_set = Self::new_bare();

        let kernel_perm = MapPermission::R
            .union(MapPermission::W)
            .union(MapPermission::X);

        let rw_perm = MapPermission::R
            .union(MapPermission::W);

        // LoongArch 现阶段低地址运行，先做 identity kernel mapping。
        memory_set.insert_area(MapArea::new(
            VirtAddr(crate::arch::MEMORY_START),
            VirtAddr(crate::arch::MEMORY_END),
            MapType::Identical,
            kernel_perm,
        ));

        memory_set.insert_area(MapArea::new(
            VirtAddr(crate::arch::UART_BASE),
            VirtAddr(crate::arch::UART_BASE + PAGE_SIZE),
            MapType::Identical,
            rw_perm,
        ));

        memory_set
    }
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


    pub fn translate(&self, vpn: VirtPageNum) -> Option<crate::mm::page_table::PageTableEntry> {
        self.page_table.translate(vpn)
    }
}

