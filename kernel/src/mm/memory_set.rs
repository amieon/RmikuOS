use alloc::vec::Vec;

use crate::mm::{
    alloc_frame, phys_to_virt, PhysAddr, PhysPageNum, VirtAddr, VirtPageNum,
    FrameTracker, KERNEL_OFFSET, PAGE_SIZE,
};
use crate::arch::{MEMORY_END, MEMORY_START};
use crate::mm::page_table::{PageTable, PteFlags};
use crate::mm::map_area::{MapArea,MapPermission,MapType};
use crate::sync::spin::Mutex;


pub struct MemorySet {
    page_table: Mutex<PageTable>,
    areas: Vec<MapArea>,
}


impl MemorySet {
    pub fn debug_count_pages(&self) -> usize {
        self.areas
            .iter()
            .map(|area| area.page_count())
            .sum()
    }
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
            page_table: Mutex::new(PageTable::new()),
            areas: Vec::new(),
        }
    }

    pub fn root_ppn(&self) -> PhysPageNum {
        self.page_table.lock().root_ppn()
    }

    pub fn insert_area(&mut self, mut area: MapArea) {
        area.map(&mut self.page_table.lock());
        self.areas.push(area);
    }
    
    pub fn remove_area(&mut self, start: VirtAddr, end: VirtAddr) -> bool {
        let start_vpn = start.floor();
        let end_vpn = end.ceil();

        let Some(index) = self.areas.iter().position(|area| {
            area.start_vpn() == start_vpn && area.end_vpn() == end_vpn
        }) else {
            return false;
        };

        let mut area = self.areas.remove(index);
        area.unmap(&mut self.page_table.lock());

        true
    }

    pub fn translate(&self, vpn: VirtPageNum) -> Option<crate::mm::page_table::PageTableEntry> {
        self.page_table.lock().translate(vpn)
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

    fn insert_linear_pa_range(
        &mut self,
        pa_start: usize,
        pa_end: usize,
        perm: MapPermission,
    ) {
        if pa_start >= pa_end {
            return;
        }

        self.insert_area(MapArea::new(
            VirtAddr(crate::mm::kernel_phys_to_virt(pa_start)),
            VirtAddr(crate::mm::kernel_phys_to_virt(pa_end)),
            MapType::Linear {
                offset: crate::mm::KERNEL_OFFSET,
            },
            perm,
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

        let va_start = crate::mm::kernel_phys_to_virt(map_start);

        assert!(
            va_start >= crate::mm::KERNEL_OFFSET,
            "kernel mapping must be high-half: map_start={:#x}, va_start={:#x}, offset={:#x}",
            map_start,
            va_start,
            crate::mm::KERNEL_OFFSET,
        );

        #[cfg(target_arch = "loongarch64")]
        let mmio_ranges: &[(usize, usize)] = &[
            (
                crate::mm::align_down(crate::arch::UART_PADDR, crate::mm::PAGE_SIZE),
                crate::mm::align_down(
                    crate::arch::UART_PADDR + crate::mm::PAGE_SIZE - 1,
                    crate::mm::PAGE_SIZE,
                ) + crate::mm::PAGE_SIZE,
            ),
            (
                crate::mm::align_down(crate::arch::PCI_ECAM_BASE, crate::mm::PAGE_SIZE),
                crate::mm::align_down(
                    crate::arch::PCI_ECAM_BASE + crate::arch::PCI_ECAM_SIZE - 1,
                    crate::mm::PAGE_SIZE,
                ) + crate::mm::PAGE_SIZE,
            ),
            (
                crate::mm::align_down(crate::arch::PCI_MMIO_BASE, crate::mm::PAGE_SIZE),
                crate::mm::align_down(
                    crate::arch::PCI_MMIO_BASE + crate::arch::PCI_MMIO_SIZE - 1,
                    crate::mm::PAGE_SIZE,
                ) + crate::mm::PAGE_SIZE,
            ),
        ];

        #[cfg(target_arch = "riscv64")]
        let mmio_ranges: &[(usize, usize)] = &[
            (
                crate::mm::align_down(crate::arch::UART_PADDR, crate::mm::PAGE_SIZE),
                crate::mm::align_down(
                    crate::arch::UART_PADDR + crate::mm::PAGE_SIZE - 1,
                    crate::mm::PAGE_SIZE,
                ) + crate::mm::PAGE_SIZE,
            ),
            (
                crate::mm::align_down(crate::arch::VIRTIO_MMIO_BASE, crate::mm::PAGE_SIZE),
                crate::mm::align_down(
                    crate::arch::VIRTIO_MMIO_BASE + crate::arch::VIRTIO_MMIO_SIZE - 1,
                    crate::mm::PAGE_SIZE,
                ) + crate::mm::PAGE_SIZE,
            ),
        ];

        let mut cursor = map_start;

        for &(raw_start, raw_end) in mmio_ranges.iter() {
            let start = core::cmp::max(raw_start, map_start);
            let end = core::cmp::min(raw_end, map_end);

            if end <= map_start || start >= map_end || start >= end {
                continue;
            }

            if cursor < start {
                self.insert_linear_pa_range(cursor, start, kernel_perm);
            }

            if cursor < end {
                cursor = end;
            }
        }

        if cursor < map_end {
            self.insert_linear_pa_range(cursor, map_end, kernel_perm);
        }

        
        //MMIO 区域单独映射。
        
        for &(start, end) in mmio_ranges.iter() {
            log::info!(
                "[mm] map mmio: pa={:#x}..{:#x}, va={:#x}..{:#x}",
                start,
                end,
                crate::mm::kernel_phys_to_virt(start),
                crate::mm::kernel_phys_to_virt(end),
            );

            self.insert_linear_pa_range(start, end, mmio_perm);
        }


        #[cfg(target_arch = "riscv64")]
        {
            let shutdown_begin = crate::mm::align_down(
                crate::shutdown::SIFIVE_TEST_BASE,
                crate::mm::PAGE_SIZE,
            );
            let shutdown_end = crate::mm::align_down(
                crate::shutdown::SIFIVE_TEST_BASE + crate::mm::PAGE_SIZE - 1,
                crate::mm::PAGE_SIZE,
            ) + crate::mm::PAGE_SIZE;
            self.insert_linear_pa_range(shutdown_begin, shutdown_end, kernel_perm);
        }


    }

    pub fn copy_data(&self, start_va: VirtAddr, data: &[u8]) {
        let mut offset = 0usize;

        while offset < data.len() {
            let va = start_va.0 + offset;
            let vpn = VirtAddr(va).floor();
            let page_offset = va & (PAGE_SIZE - 1);

            let pte = self
                .page_table
                .lock()
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

        #[cfg(target_arch = "riscv64")]
        {
            memory_set.map_kernel_areas();
        }
  

        let app_start = crate::mm::USER_TEXT_BASE;
        let app_end = crate::mm::align_up(
            app_start + core::cmp::max(app.len(), 1),
            PAGE_SIZE,
        );

        let app_perm = MapPermission::R
            .union(MapPermission::X)
            .union(MapPermission::W)
            .union(MapPermission::U);

        memory_set.insert_framed_area(
            VirtAddr(app_start),
            VirtAddr(app_end),
            app_perm,
        );

        memory_set.copy_data(VirtAddr(app_start), app);

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


    pub fn from_existed_user(user_space: &Self) -> Self {
        let mut memory_set = Self::new_bare();


        memory_set.map_kernel_areas();
        

        for area in user_space.areas.iter() {
            if !area.is_user() {
                continue;
            }

            assert!(
                area.is_framed(),
                "user area must be framed when cloning user MemorySet"
            );

            let new_area = area.clone_framed_area_data(
                &user_space.page_table.lock(),
                &mut memory_set.page_table.lock(),
            );

            memory_set.areas.push(new_area);
        }

        memory_set
    }
        
}



//elf section
impl MemorySet {
    fn write_bytes_to_user(&self, user_va: usize, data: &[u8]) -> Option<()> {
        for (offset, byte) in data.iter().enumerate() {
            let va = user_va.checked_add(offset)?;

            let vpn = VirtAddr(va).floor();
            let page_offset = va & (PAGE_SIZE - 1);

            let pte = self.translate(vpn)?;

            let page = pte.ppn().bytes_array();
            page[page_offset] = *byte;
        }

        Some(())
    }

    fn zero_user(&self, user_va: usize, len: usize) -> Option<()> {
        for offset in 0..len {
            let va = user_va.checked_add(offset)?;

            let vpn = VirtAddr(va).floor();
            let page_offset = va & (PAGE_SIZE - 1);

            let pte = self.translate(vpn)?;

            let page = pte.ppn().bytes_array();
            page[page_offset] = 0;
        }

        Some(())
    }

    pub fn from_elf(elf_data: &[u8]) -> Option<(Self, usize, usize)> {
        use crate::mm::elf::{
            Elf64Header,
            PF_R,
            PF_W,
            PF_X,
            PT_INTERP,
            PT_LOAD,
        };

        let header = Elf64Header::parse(elf_data)?;

        let mut memory_set = MemorySet::new_bare();
        memory_set.map_kernel_areas();

        for i in 0..header.e_phnum as usize {
            let ph = header.program_header(elf_data, i)?;

            if ph.p_type == PT_INTERP {
                //不支持动态链接器的哦
                log::warn!("[elf] PT_INTERP is not supported");
                return None;
            }

            if ph.p_type != PT_LOAD {
                continue;
            }

            if ph.p_memsz < ph.p_filesz {
                log::warn!(
                    "[elf] invalid segment: memsz={} filesz={}",
                    ph.p_memsz,
                    ph.p_filesz,
                );
                return None;
            }

            if ph.p_memsz == 0 {
                continue;
            }

            let file_start = ph.p_offset as usize;
            let file_size = ph.p_filesz as usize;
            let file_end = file_start.checked_add(file_size)?;

            if file_end > elf_data.len() {
                log::warn!("[elf] segment out of file range");
                return None;
            }

            let seg_start_va = ph.p_vaddr as usize;
            let seg_file_end_va = seg_start_va.checked_add(file_size)?;
            let seg_mem_end_va = seg_start_va.checked_add(ph.p_memsz as usize)?;

            let map_start = crate::mm::align_down(seg_start_va, PAGE_SIZE);
            let map_end = crate::mm::align_up(seg_mem_end_va, PAGE_SIZE);

            let mut perm = MapPermission::U;

            if ph.p_flags & PF_R != 0 {
                perm = perm.union(MapPermission::R);
            }

            if ph.p_flags & PF_W != 0 {
                perm = perm.union(MapPermission::W);
            }

            if ph.p_flags & PF_X != 0 {
                perm = perm.union(MapPermission::X);
            }

            memory_set.insert_area(MapArea::new(
                VirtAddr(map_start),
                VirtAddr(map_end),
                MapType::Framed,
                perm,
            ));

            
            //拷贝文件中真实存在的部分。
            memory_set.write_bytes_to_user(
                seg_start_va,
                &elf_data[file_start..file_end],
            )?;

            
            // 清零 .bss:
            // [p_vaddr + p_filesz, p_vaddr + p_memsz)

            if seg_mem_end_va > seg_file_end_va {
                memory_set.zero_user(
                    seg_file_end_va,
                    seg_mem_end_va - seg_file_end_va,
                )?;
            }

            log::info!(
                "[elf] load segment {}: va={:#x}..{:#x}, file={} bytes, mem={} bytes, flags={:#x}",
                i,
                map_start,
                map_end,
                ph.p_filesz,
                ph.p_memsz,
                ph.p_flags,
            );
        }



        let user_stack_top = crate::mm::USER_STACK_TOP;
        let user_stack_bottom = user_stack_top - crate::mm::USER_STACK_SIZE;

        memory_set.insert_area(MapArea::new(
            VirtAddr(user_stack_bottom),
            VirtAddr(user_stack_top),
            MapType::Framed,
            MapPermission::R
                .union(MapPermission::W)
                .union(MapPermission::U),
        ));

        Some((
            memory_set,
            header.e_entry as usize,
            user_stack_top,
        ))
    }
}
