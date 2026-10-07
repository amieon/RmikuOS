

/// UART0 on QEMU virt.
pub const UART0: usize = 0x1000_0000;

use core::arch::asm;

use super::FrameTracker;

const SATP_MODE_SV39: usize = 8usize << 60;

/// Sv39 leaf sizes supported by the automatic range mapper.
///
/// Level-2 leaf: 1GiB, level-1 leaf: 2MiB, level-0 leaf: 4KiB.
const LEVEL2_PAGE_SIZE: usize = 1usize << 30;
const LEVEL1_PAGE_SIZE: usize = 1usize << 21;
const SUPPORTED_PAGE_SIZES: [usize; 3] = [
    LEVEL2_PAGE_SIZE,
    LEVEL1_PAGE_SIZE,
    PAGE_SIZE,
];


use alloc::vec;
use alloc::vec::Vec;

use crate::mm::address::*;
use crate::mm::config::*;
use crate::mm::{
    alloc_frame, dealloc_frame, PhysPageNum, VirtPageNum,
};


fn vpn_indexes(vpn: VirtPageNum) -> [usize; 3] {
    let mut vpn = vpn.0;
    let mut idx = [0usize; 3];

    for i in (0..3).rev() {
        idx[i] = vpn & 0x1ff;
        vpn >>= 9;
    }

    idx
}

fn pte_array(ppn: PhysPageNum) -> &'static mut [PageTableEntry] {
    let pa = ppn.0 << PAGE_SIZE_BITS;
    let va = crate::mm::phys_to_virt(pa);
        unsafe {
        core::slice::from_raw_parts_mut(va as *mut PageTableEntry, 512)
    }
}

pub struct PageTable {
    root_ppn: PhysPageNum,
    frames: Vec<FrameTracker>,
}


#[derive(Copy, Clone)]
#[repr(transparent)]
pub struct PageTableEntry {
    bits: usize,
}

impl PageTableEntry {
    pub const fn empty() -> Self {
        Self { bits: 0 }
    }

    pub fn new(ppn: PhysPageNum, flags: PteFlags) -> Self {
        Self {
            bits: (ppn.0 << 10) | flags.bits,
        }
    }

    pub fn bits(self) -> usize {
        self.bits
    }

    pub fn ppn(self) -> PhysPageNum {
        PhysPageNum((self.bits >> 10) & ((1usize << 44) - 1))
    }

    pub fn flags(self) -> PteFlags {
        PteFlags {
            bits: self.bits & 0x3ff,
        }
    }

    pub fn is_valid(self) -> bool {
        self.flags().contains(PteFlags::V)
    }

    /// Sv39 non-leaf PTEs have V set and R/W/X all clear. Any R/W/X bit
    /// means this entry directly maps a 4KiB/2MiB/1GiB page.
    pub fn is_leaf(self) -> bool {
        self.readable() || self.writable() || self.executable()
    }

    pub fn readable(self) -> bool {
        self.flags().contains(PteFlags::R)
    }

    pub fn writable(self) -> bool {
        self.flags().contains(PteFlags::W)
    }

    pub fn executable(self) -> bool {
        self.flags().contains(PteFlags::X)
    }
}



#[derive(Copy, Clone)]
pub struct PteFlags {
    bits: usize,
}

impl PteFlags {
    pub const V: Self = Self { bits: 1 << 0 };
    pub const R: Self = Self { bits: 1 << 1 };
    pub const W: Self = Self { bits: 1 << 2 };
    pub const X: Self = Self { bits: 1 << 3 };
    pub const U: Self = Self { bits: 1 << 4 };
    pub const G: Self = Self { bits: 1 << 5 };
    pub const A: Self = Self { bits: 1 << 6 };
    pub const D: Self = Self { bits: 1 << 7 };

    pub const fn empty() -> Self {
        Self { bits: 0 }
    }

    pub const fn bits(self) -> usize {
        self.bits
    }

    pub fn contains(self, rhs: Self) -> bool {
        self.bits & rhs.bits != 0
    }

    pub const fn union(self, rhs: Self) -> Self {
        Self {
            bits: self.bits | rhs.bits,
        }
    }
}

pub fn map_range_identity(
    pt: &mut PageTable,
    start: usize,
    end: usize,
    flags: PteFlags,
) {
    let mut va = align_down(start, PAGE_SIZE);
    let end = align_up(end, PAGE_SIZE);

    while va < end {
        pt.map(
            VirtAddr::from(va).floor(),
            PhysAddr::from(va).floor(),
            flags,
        );
        va += PAGE_SIZE;
    }
}
pub fn map_range(
    pt: &mut PageTable,
    va_start: usize,
    pa_start: usize,
    size: usize,
    flags: PteFlags,
) {
    let va = crate::mm::align_down(va_start, crate::mm::PAGE_SIZE);
    let pa = crate::mm::align_down(pa_start, crate::mm::PAGE_SIZE);
    let end = crate::mm::align_up(va_start + size, crate::mm::PAGE_SIZE);

    pt.map_range(
        crate::mm::VirtAddr::from(va),
        crate::mm::PhysAddr::from(pa),
        end - va,
        flags,
    );
}



impl PageTable {
    pub fn new() -> Self {
        let frame = alloc_frame().expect("failed to allocate root page table");
        let tracker = FrameTracker::new(frame);
        Self {
            root_ppn: frame,
            frames: vec![tracker],
        }
    }

    pub fn root_ppn(&self) -> PhysPageNum {
        self.root_ppn
    }

    fn find_pte_create(&mut self, vpn: VirtPageNum) -> Option<&'static mut PageTableEntry> {
        let idxs = vpn_indexes(vpn);
        let mut ppn = self.root_ppn;

        for i in 0..2 {
            let pte = &mut pte_array(ppn)[idxs[i]];

            if !pte.is_valid() {
                let frame = alloc_frame()?;
                let tracker = FrameTracker::new(frame);
                *pte = PageTableEntry::new(frame, PteFlags::V);
                self.frames.push(tracker);
            } else if pte.is_leaf() {
                panic!(
                    "cannot create 4KiB mapping below a huge page: vpn {:?}, level {}",
                    vpn,
                    i
                );
            }

            ppn = pte.ppn();
        }

        Some(&mut pte_array(ppn)[idxs[2]])
    }

    pub fn map(&mut self, vpn: VirtPageNum, ppn: PhysPageNum, flags: PteFlags) {
        let pte = self.find_pte_create(vpn).expect("failed to create pte");
        assert!(!pte.is_valid(), "vpn {:?} is already mapped", vpn);
        *pte = PageTableEntry::new(ppn, flags.union(PteFlags::V));
    }

    /// Map `[va, va + size)` to `[pa, pa + size)` with the largest usable
    /// Sv39 leaf at every step.
    ///
    /// The caller does not choose a page size. For example, an aligned
    /// 2.5GiB range is split into 1GiB + 1GiB + 256 * 2MiB mappings.
    /// Unaligned prefixes/suffixes automatically fall back to smaller pages.
    /// Returns the number of page-table leaves created.
    pub fn map_range(
        &mut self,
        va_start: VirtAddr,
        pa_start: PhysAddr,
        size: usize,
        flags: PteFlags,
    ) -> usize {
        assert!(size > 0, "cannot map an empty range");
        assert!(
            va_start.0 % PAGE_SIZE == 0,
            "range VA is not page aligned: {:#x}",
            va_start.0
        );
        assert!(
            pa_start.0 % PAGE_SIZE == 0,
            "range PA is not page aligned: {:#x}",
            pa_start.0
        );
        assert!(
            size % PAGE_SIZE == 0,
            "range size is not page aligned: {:#x}",
            size
        );
        va_start
            .0
            .checked_add(size)
            .expect("virtual range overflows");
        pa_start
            .0
            .checked_add(size)
            .expect("physical range overflows");

        let mut va = va_start.0;
        let mut pa = pa_start.0;
        let mut remaining = size;
        let mut mapped_leaves = 0usize;

        while remaining > 0 {
            let page_size = Self::largest_usable_page(va, pa, remaining);

            match page_size {
                LEVEL2_PAGE_SIZE => self.map_level2_leaf(VirtAddr(va), PhysAddr(pa), flags),
                LEVEL1_PAGE_SIZE => self.map_level1_leaf(VirtAddr(va), PhysAddr(pa), flags),
                PAGE_SIZE => self.map(VirtAddr(va).floor(), PhysAddr(pa).floor(), flags),
                _ => unreachable!("unsupported Sv39 page size"),
            }

            va += page_size;
            pa += page_size;
            remaining -= page_size;
            mapped_leaves += 1;
        }

        mapped_leaves
    }

    fn largest_usable_page(va: usize, pa: usize, remaining: usize) -> usize {
        for &page_size in SUPPORTED_PAGE_SIZES.iter() {
            if remaining >= page_size && va % page_size == 0 && pa % page_size == 0 {
                return page_size;
            }
        }

        PAGE_SIZE
    }

    /// Map one aligned 1GiB region with a level-2 leaf PTE.
    fn map_level2_leaf(&mut self, va: VirtAddr, pa: PhysAddr, flags: PteFlags) {
        assert!(
            va.0 % LEVEL2_PAGE_SIZE == 0,
            "level-2 leaf VA is not aligned: {:#x}",
            va.0
        );
        assert!(
            pa.0 % LEVEL2_PAGE_SIZE == 0,
            "level-2 leaf PA is not aligned: {:#x}",
            pa.0
        );

        let index = (va.0 >> 30) & 0x1ff;
        let pte = &mut pte_array(self.root_ppn)[index];
        assert!(
            !pte.is_valid(),
            "level-2 leaf conflicts at va={:#x}, old_bits={:#x}",
            va.0,
            pte.bits()
        );

        *pte = PageTableEntry::new(pa.floor(), flags.union(PteFlags::V));
    }

    /// Map one aligned 2MiB region with a level-1 leaf PTE.
    fn map_level1_leaf(&mut self, va: VirtAddr, pa: PhysAddr, flags: PteFlags) {
        assert!(
            va.0 % LEVEL1_PAGE_SIZE == 0,
            "level-1 leaf VA is not aligned: {:#x}",
            va.0
        );
        assert!(
            pa.0 % LEVEL1_PAGE_SIZE == 0,
            "level-1 leaf PA is not aligned: {:#x}",
            pa.0
        );

        let idxs = vpn_indexes(va.floor());
        let root_pte = &mut pte_array(self.root_ppn)[idxs[0]];

        if !root_pte.is_valid() {
            let frame = alloc_frame().expect("failed to allocate level-1 page table");
            let tracker = FrameTracker::new(frame);
            *root_pte = PageTableEntry::new(frame, PteFlags::V);
            self.frames.push(tracker);
        } else {
            assert!(
                !root_pte.is_leaf(),
                "level-1 leaf conflicts with a level-2 leaf at va={:#x}",
                va.0
            );
        }

        let pte = &mut pte_array(root_pte.ppn())[idxs[1]];
        assert!(
            !pte.is_valid(),
            "level-1 leaf conflicts at va={:#x}, old_bits={:#x}",
            va.0,
            pte.bits()
        );

        *pte = PageTableEntry::new(pa.floor(), flags.union(PteFlags::V));
    }

    /// mprotect 用: 只改已有 PTE 的权限位, 不重新映射(不分配新帧)。
    pub fn update_flags(&mut self, vpn: VirtPageNum, flags: PteFlags) {
        let pte = self.find_pte(vpn).expect("update_flags: pte not found");
        assert!(pte.is_valid(), "update_flags: vpn {:?} is invalid", vpn);
        *pte = PageTableEntry::new(pte.ppn(), flags.union(PteFlags::V));
    }

    pub fn unmap(&mut self, vpn: VirtPageNum) {
        let pte = self.find_pte(vpn).expect("pte not found");
        assert!(pte.is_valid(), "vpn {:?} is invalid", vpn);
        *pte = PageTableEntry::empty();
    }

    pub fn translate(&self, vpn: VirtPageNum) -> Option<PageTableEntry> {
        self.find_pte(vpn).map(|pte| *pte)
    }

    /// Return whether any 4KiB/2MiB/1GiB mapping covers `va`.
    ///
    /// Unlike translate(), this is safe to call on addresses covered by a
    /// kernel huge mapping.
    pub fn contains_va(&self, va: VirtAddr) -> bool {
        let idxs = vpn_indexes(va.floor());
        let mut ppn = self.root_ppn;

        for i in 0..3 {
            let pte = pte_array(ppn)[idxs[i]];
            if !pte.is_valid() {
                return false;
            }
            if pte.is_leaf() {
                return true;
            }
            if i == 2 {
                return false;
            }
            ppn = pte.ppn();
        }

        false
    }

    fn find_pte(&self, vpn: VirtPageNum) -> Option<&'static mut PageTableEntry> {
        let idxs = vpn_indexes(vpn);
        let mut ppn = self.root_ppn;

        for i in 0..3 {
            let pte = &mut pte_array(ppn)[idxs[i]];
            if !pte.is_valid() {
                return None;
            }
            if i == 2 {
                return Some(pte);
            }
            if pte.is_leaf() {
                // A 1GiB/2MiB kernel mapping cannot provide a 4KiB PTE.
                // Kernel direct-map access goes through the active TLB entry,
                // not through this software lookup helper.
                return None;
            }
            ppn = pte.ppn();
        }

        None
    }
}