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
    pub fn new_kernel() -> Self {
        let mut memory_set = Self::new_bare();
        memory_set.map_kernel_areas();
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

impl MemorySet {
    pub fn insert_framed_area(
        &mut self,
        start_va: VirtAddr,
        end_va: VirtAddr,
        permission: MapPermission,
    ) {
        self.insert_area(MapArea::new(
            start_va,
            end_va,
            MapType::Framed,
            permission,
        ));
    }

    
    fn map_kernel_areas(&mut self) {



        let kernel_perm = MapPermission::R
            .union(MapPermission::W)
            .union(MapPermission::X);

        let mmio_perm = MapPermission::R
            .union(MapPermission::W);

        let map_start = crate::arch::MEMORY_START;
        let map_end = crate::arch::MEMORY_START + crate::arch::KERNEL_DIRECT_MAP_SIZE;

        let uart_start = crate::mm::align_down(crate::arch::UART_PADDR, crate::mm::PAGE_SIZE);
        let uart_end = uart_start + crate::mm::PAGE_SIZE;


        let va_start = crate::mm::kernel_phys_to_virt(map_start);
        let va_end = crate::mm::kernel_phys_to_virt(map_end);

        assert!(
            va_start >= crate::mm::KERNEL_OFFSET,
            "kernel mapping must be high-half: map_start={:#x}, va_start={:#x}, offset={:#x}",
            map_start,
            va_start,
            crate::mm::KERNEL_OFFSET,
        );

        // direct map 没碰到 UART，整段映射。
        if map_end <= uart_start || map_start >= uart_end {
            self.insert_area(MapArea::new(
                VirtAddr(va_start),
                VirtAddr(va_end),
                MapType::Linear {
                    offset: crate::mm::KERNEL_OFFSET,
                },
                kernel_perm,
            ));
        } else {
            // direct map 覆盖 UART，挖掉 UART 页。

            if map_start < uart_start {
                self.insert_area(MapArea::new(
                    VirtAddr(crate::mm::kernel_phys_to_virt(map_start)),
                    VirtAddr(crate::mm::kernel_phys_to_virt(uart_start)),
                    MapType::Linear {
                        offset: crate::mm::KERNEL_OFFSET,
                    },
                    kernel_perm,
                ));
            }

            if uart_end < map_end {
                self.insert_area(MapArea::new(
                    VirtAddr(crate::mm::kernel_phys_to_virt(uart_end)),
                    VirtAddr(crate::mm::kernel_phys_to_virt(map_end)),
                    MapType::Linear {
                        offset: crate::mm::KERNEL_OFFSET,
                    },
                    kernel_perm,
                ));
            }
        }

        // UART 单独映射，避免被普通 memory flags 覆盖。
        self.insert_area(MapArea::new(
            VirtAddr(crate::mm::kernel_phys_to_virt(uart_start)),
            VirtAddr(crate::mm::kernel_phys_to_virt(uart_end)),
            MapType::Linear {
                offset: crate::mm::KERNEL_OFFSET,
            },
            mmio_perm,
        ));
    }


    pub fn copy_data(&self, start_va: VirtAddr, data: &[u8]) {
        let mut offset = 0usize;

        while offset < data.len() {
            let va = start_va.0 + offset;
            let vpn = VirtAddr(va).floor();
            let page_offset = va & (PAGE_SIZE - 1);

            let pte = self
                .page_table
                .translate(vpn)
                .expect("copy_data: target page is not mapped");

            let ppn = pte.ppn();

            let copy_len = core::cmp::min(
                PAGE_SIZE - page_offset,
                data.len() - offset,
            );

            let dst_pa = (ppn.0 << super::PAGE_SIZE_BITS) + page_offset;
            let dst_va = crate::mm::kernel_phys_to_virt(dst_pa);

            unsafe {
                core::ptr::copy_nonoverlapping(
                    data.as_ptr().add(offset),
                    dst_va as *mut u8,
                    copy_len,
                );
            }

            offset += copy_len;
        }
    }

    


    pub fn new_user_test(app: &[u8]) -> (Self, usize, usize) {
        let mut memory_set = Self::new_bare();

        /*
         * 第一版先把内核高半也映射进用户页表。
         *
         * 这些区域 U=0，用户态不能访问。
         * 但是 trap 进入内核后，内核代码/栈/UART 可以继续工作。
         */
        #[cfg(target_arch = "riscv64")]
        {
            memory_set.map_kernel_areas();
        }
  

        /*
         * 用户程序区域。
         */
        let app_start = crate::mm::USER_TEXT_BASE;
        let app_end = crate::mm::align_up(
            app_start + core::cmp::max(app.len(), 1),
            PAGE_SIZE,
        );

        let app_perm = MapPermission::R
            .union(MapPermission::X)
            .union(MapPermission::U);

        memory_set.insert_framed_area(
            VirtAddr(app_start),
            VirtAddr(app_end),
            app_perm,
        );

        memory_set.copy_data(VirtAddr(app_start), app);

        /*
         * 用户栈。
         */

        let stack_perm = MapPermission::R
            .union(MapPermission::W)
            .union(MapPermission::U);

        memory_set.insert_framed_area(
            VirtAddr(crate::mm::USER_STACK_BOTTOM),
            VirtAddr(crate::mm::USER_STACK_TOP),
            stack_perm,
        );

        let entry = app_start;
        let user_sp = crate::mm::USER_STACK_TOP;

        (memory_set, entry, user_sp)
    }
}