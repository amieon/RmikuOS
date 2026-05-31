use alloc::vec::Vec;

use crate::mm::{
    alloc_frame, phys_to_virt, PhysAddr, PhysPageNum, VirtAddr, VirtPageNum,
    FrameTracker, PAGE_SIZE, KERNEL_OFFSET
};
use crate::mm::page_table::{PageTable, PteFlags};

use crate::arch::{MEMORY_END, MEMORY_START};


#[derive(Clone, Copy, Debug)]
pub struct VPNRange {
    current: usize,
    end: usize,
}

impl VPNRange {
    pub fn new(start: VirtPageNum, end: VirtPageNum) -> Self {
        Self {
            current: start.0,
            end: end.0,
        }
    }
}

impl Iterator for VPNRange {
    type Item = VirtPageNum;

    fn next(&mut self) -> Option<Self::Item> {
        if self.current < self.end {
            let vpn = VirtPageNum(self.current);
            self.current += 1;
            Some(vpn)
        } else {
            None
        }
    }
}

#[derive(Clone, Copy, Debug)]
pub enum MapType {
    Identical,
    Linear {
        offset: usize,
    },
    Framed,
}

#[derive(Clone, Copy, Debug)]
pub struct MapPermission {
    bits: u8,
}

impl MapPermission {
    pub const R: Self = Self { bits: 1 << 0 };
    pub const W: Self = Self { bits: 1 << 1 };
    pub const X: Self = Self { bits: 1 << 2 };
    pub const U: Self = Self { bits: 1 << 3 };

    pub const fn empty() -> Self {
        Self { bits: 0 }
    }

    pub const fn union(self, rhs: Self) -> Self {
        Self {
            bits: self.bits | rhs.bits,
        }
    }

    pub fn contains(self, rhs: Self) -> bool {
        self.bits & rhs.bits != 0
    }
}

pub struct MapArea {
    vpn_range: VPNRange,
    map_type: MapType,
    permission: MapPermission,
    // 只有 Framed 映射拥有这些物理页。
    data_frames: Vec<FrameTracker>,
}

impl MapArea {
    pub fn new(
        start_va: VirtAddr,
        end_va: VirtAddr,
        map_type: MapType,
        permission: MapPermission,
    ) -> Self {
        let start_vpn = start_va.floor();
        let end_vpn = end_va.ceil();

        Self {
            vpn_range: VPNRange::new(start_vpn, end_vpn),
            map_type,
            permission,
            data_frames: Vec::new(),
        }
    }

    pub fn map(&mut self, page_table: &mut PageTable) {
        for vpn in self.vpn_range {
            self.map_one(page_table, vpn);
        }
    }

    pub fn unmap(&mut self, page_table: &mut PageTable) {
        for vpn in self.vpn_range {
            page_table.unmap(vpn);
        }
        self.data_frames.clear();
    }

    fn map_one(&mut self, page_table: &mut PageTable, vpn: VirtPageNum) {
        let ppn = match self.map_type {
            MapType::Identical => PhysPageNum(vpn.0),

            MapType::Linear { offset } => {
                let va = vpn.addr().0;
                let pa = va - offset;
                PhysAddr::from(pa).floor()
            }

            MapType::Framed => {
                let frame = FrameTracker::new(
                    alloc_frame().expect("failed to allocate frame for MapArea"),
                );
                let ppn = frame.ppn;
                self.data_frames.push(frame);
                ppn
            }
        };

        page_table.map(vpn, ppn, map_perm_to_pte_flags(self.permission));
    }
}



impl MapArea {
    pub fn is_user(&self) -> bool {
        self.permission.contains(MapPermission::U)
    }

    pub fn is_framed(&self) -> bool {
        matches!(self.map_type, MapType::Framed)
    }

    pub fn clone_framed_area_data(
        &self,
        old_page_table: &PageTable,
        new_page_table: &mut PageTable,
    ) -> Self {
        assert!(
            self.is_framed(),
            "clone_framed_area_data only supports framed areas"
        );

 
        let mut new_area = MapArea {
            vpn_range: self.vpn_range,
            map_type: MapType::Framed,
            permission: self.permission,
            data_frames: Vec::new(),
        };

        new_area.map(new_page_table);


        for vpn in self.vpn_range {
            let src_pte = old_page_table
                .translate(vpn)
                .expect("clone_framed_area_data: old pte not found");

            let dst_pte = new_page_table
                .translate(vpn)
                .expect("clone_framed_area_data: new pte not found");

            let src = src_pte.ppn().bytes_array();
            let dst = dst_pte.ppn().bytes_array();

            dst.copy_from_slice(src);
        }

        new_area
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
#[cfg(target_arch = "loongarch64")]
fn map_perm_to_pte_flags(permission: MapPermission) -> PteFlags {
    let mut flags = PteFlags::MAT_CC;


    if permission.contains(MapPermission::U) {
        flags = flags.union(PteFlags::PLV3);
    } else {
        flags = flags.union(PteFlags::PLV0).union(PteFlags::G);
    }


    if !permission.contains(MapPermission::R) {
        flags = flags.union(PteFlags::NR);
    }

    if !permission.contains(MapPermission::X) {
        flags = flags.union(PteFlags::NX);
    }


    if permission.contains(MapPermission::W) {
        flags = flags.union(PteFlags::W).union(PteFlags::D);
    }

    flags
}