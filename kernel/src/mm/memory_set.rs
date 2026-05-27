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

        let mmio_perm = MapPermission::R
            .union(MapPermission::W);

        let uart_start = crate::mm::align_down(crate::arch::UART_BASE, crate::mm::PAGE_SIZE);
        let uart_end = crate::mm::align_up(
            crate::arch::UART_BASE + crate::mm::PAGE_SIZE,
            crate::mm::PAGE_SIZE,
        );

        // 普通内存：UART 前半段
        if crate::arch::MEMORY_START < uart_start {
            memory_set.insert_area(MapArea::new(
                VirtAddr(crate::arch::MEMORY_START),
                VirtAddr(uart_start),
                MapType::Identical,
                kernel_perm,
            ));
        }

        // UART/MMIO 页
        memory_set.insert_area(MapArea::new(
            VirtAddr(uart_start),
            VirtAddr(uart_end),
            MapType::Identical,
            mmio_perm,
        ));

        // 普通内存：UART 后半段
        if uart_end < crate::arch::MEMORY_END {
            memory_set.insert_area(MapArea::new(
                VirtAddr(uart_end),
                VirtAddr(crate::arch::MEMORY_END),
                MapType::Identical,
                kernel_perm,
            ));
        }

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

