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

        let map_start = crate::arch::MEMORY_START;
        let map_end = crate::arch::MEMORY_START + crate::arch::KERNEL_DIRECT_MAP_SIZE;

        let uart_start = crate::mm::align_down(crate::arch::UART_PADDR, crate::mm::PAGE_SIZE);
        let uart_end = uart_start + crate::mm::PAGE_SIZE;

        // Case 1: direct map does not touch UART.
        if map_end <= uart_start || map_start >= uart_end {
            memory_set.insert_area(MapArea::new(
                VirtAddr(crate::mm::kernel_phys_to_virt(map_start)),
                VirtAddr(crate::mm::kernel_phys_to_virt(map_end)),
                MapType::Linear {
                    offset: crate::mm::KERNEL_OFFSET,
                },
                kernel_perm,
            ));
        } else {
            // Case 2: direct map overlaps UART. Split it.

            if map_start < uart_start {
                memory_set.insert_area(MapArea::new(
                    VirtAddr(crate::mm::kernel_phys_to_virt(map_start)),
                    VirtAddr(crate::mm::kernel_phys_to_virt(uart_start)),
                    MapType::Linear {
                        offset: crate::mm::KERNEL_OFFSET,
                    },
                    kernel_perm,
                ));
            }

            if uart_end < map_end {
                memory_set.insert_area(MapArea::new(
                    VirtAddr(crate::mm::kernel_phys_to_virt(uart_end)),
                    VirtAddr(crate::mm::kernel_phys_to_virt(map_end)),
                    MapType::Linear {
                        offset: crate::mm::KERNEL_OFFSET,
                    },
                    kernel_perm,
                ));
            }
        }

        // UART is always mapped once, with MMIO permission.
        memory_set.insert_area(MapArea::new(
            VirtAddr(crate::mm::kernel_phys_to_virt(uart_start)),
            VirtAddr(crate::mm::kernel_phys_to_virt(uart_end)),
            MapType::Linear {
                offset: crate::mm::KERNEL_OFFSET,
            },
            mmio_perm,
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

