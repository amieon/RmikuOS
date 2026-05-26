use alloc::vec;
use alloc::vec::Vec;


use super::{
    alloc_frame, dealloc_frame, PhysPageNum, VirtPageNum,
};

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
    let mut va = super::align_down(start, super::PAGE_SIZE);
    let end = super::align_up(end, super::PAGE_SIZE);

    while va < end {
        pt.map(
            super::VirtAddr::from(va).floor(),
            super::PhysAddr::from(va).floor(),
            flags,
        );
        va += super::PAGE_SIZE;
    }
}



pub struct FrameTracker {
    pub ppn: PhysPageNum,
}

impl FrameTracker {
    pub fn new(ppn: PhysPageNum) -> Self {
        // 清空页表页
        let bytes = ppn.bytes_array();
        for b in bytes {
            *b = 0;
        }
        Self { ppn }
    }
}

impl Drop for FrameTracker {
    fn drop(&mut self) {
        dealloc_frame(self.ppn);
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
        let idxs = vpn.indexes();
        let mut ppn = self.root_ppn;

        for i in 0..2 {
            let pte = &mut ppn.pte_array()[idxs[i]];

            if !pte.is_valid() {
                let frame = alloc_frame()?;
                let tracker = FrameTracker::new(frame);
                *pte = PageTableEntry::new(frame, PteFlags::V);
                self.frames.push(tracker);
            }

            ppn = pte.ppn();
        }

        Some(&mut ppn.pte_array()[idxs[2]])
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
        let idxs = vpn.indexes();
        let mut ppn = self.root_ppn;

        for i in 0..3 {
            let pte = &mut ppn.pte_array()[idxs[i]];
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