/// QEMU virt DRAM starts at 0x8000_0000.
pub const MEMORY_START: usize = 0x8000_0000;

/// run.sh uses `-m 128M` for RISC-V.
pub const MEMORY_SIZE: usize = 128 * 1024 * 1024;

pub const MEMORY_END: usize = MEMORY_START + MEMORY_SIZE;

/// UART0 on QEMU virt.
pub const UART0: usize = 0x1000_0000;

use core::arch::asm;

use super::FrameTracker;

const SATP_MODE_SV39: usize = 8usize << 60;

pub fn activate_kernel_page_table(root_ppn: PhysPageNum) {
    let satp = SATP_MODE_SV39 | root_ppn.0;

    unsafe {
        asm!(
            "csrw satp, {satp}",
            "sfence.vma",
            satp = in(reg) satp,
            options(nostack)
        );
    }

    log::info!(
        "[mm] RISC-V paging enabled: satp={:#x}, root_ppn={:#x}",
        satp,
        root_ppn.0
    );
}


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
    let mut va = crate::mm::align_down(va_start, crate::mm::PAGE_SIZE);
    let mut pa = crate::mm::align_down(pa_start, crate::mm::PAGE_SIZE);
    let end = crate::mm::align_up(va_start + size, crate::mm::PAGE_SIZE);

    while va < end {
        pt.map(
            crate::mm::VirtAddr::from(va).floor(),
            crate::mm::PhysAddr::from(pa).floor(),
            flags,
        );
        va += crate::mm::PAGE_SIZE;
        pa += crate::mm::PAGE_SIZE;
    }
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

    pub fn unmap(&mut self, vpn: VirtPageNum) {
        let pte = self.find_pte(vpn).expect("pte not found");
        assert!(pte.is_valid(), "vpn {:?} is invalid", vpn);
        *pte = PageTableEntry::empty();
    }

    pub fn translate(&self, vpn: VirtPageNum) -> Option<PageTableEntry> {
        self.find_pte(vpn).map(|pte| *pte)
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
            ppn = pte.ppn();
        }

        None
    }
}