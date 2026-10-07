
/// Early UART/MMIO address should still come from arch::UART_BASE.
pub const UART0: usize = crate::arch::UART_BASE;

use core::arch::asm;


const CSR_CRMD: usize = 0x0;
const CSR_DMW0: usize = 0x180;
const CSR_DMW1: usize = 0x181;

const CRMD_DA: usize = 1 << 3;
const CRMD_PG: usize = 1 << 4;

/// DMW flags
const DMW_PLV0: usize = 1 << 0;

/// MAT:
/// 0 = strongly ordered / uncached-like
/// 1 = coherent cached, depending on platform
const DMW_MAT_CC: usize = 1 << 4;
const DMW_MAT_SUC: usize = 0 << 4;

/// VSEG selects VA[63:60].
const fn dmw(vseg: usize, mat: usize, plv: usize) -> usize {
    (vseg << 60) | mat | plv
}



use alloc::vec;
use alloc::vec::Vec;

use crate::mm::{
    alloc_frame, dealloc_frame, align_down, align_up, PhysAddr, PhysPageNum,
    VirtAddr, VirtPageNum, PAGE_SIZE, PAGE_SIZE_BITS,
};

use super::FrameTracker;

/// LoongArch huge-page encoding at directory levels.
///
/// A directory entry with bit 6 set is a huge leaf. Unlike a common PTE,
/// whose G bit is also bit 6, a huge leaf stores G at bit 12. The in-memory
/// entry keeps bits 14:13 clear; LDDIR fills those bits with the directory
/// level while walking, and LDPTE derives the TLB page size from them.
const HUGE_DIRECTORY: usize = 1 << 6;
const HUGE_GLOBAL: usize = 1 << 12;

/// Leaf sizes supported by the automatic range mapper.
///
/// Dir3 leaf: 512GiB, Dir2 leaf: 1GiB, Dir1 leaf: 2MiB, PT leaf: 4KiB.
const DIR3_PAGE_SIZE: usize = 1usize << 39;
const DIR2_PAGE_SIZE: usize = 1usize << 30;
const DIR1_PAGE_SIZE: usize = 1usize << 21;
const SUPPORTED_PAGE_SIZES: [usize; 4] = [
    DIR3_PAGE_SIZE,
    DIR2_PAGE_SIZE,
    DIR1_PAGE_SIZE,
    PAGE_SIZE,
];

/// LoongArch64 page table.
///
/// This layout is intended to work with LoongArch LDDIR/LDPTE based
/// software page walking.
///
/// We use a 4-level 4KiB page table:
///
/// VA[47:39] -> Dir3
/// VA[38:30] -> Dir2
/// VA[29:21] -> Dir1
/// VA[20:12] -> PTEwalk
/// VA[11:0]  -> page offset
pub struct PageTable {
    root_ppn: PhysPageNum,
    frames: Vec<FrameTracker>,
}

#[derive(Copy, Clone, Debug)]
#[repr(transparent)]
pub struct PageTableEntry {
    bits: usize,
}

#[derive(Copy, Clone, Debug)]
pub struct PteFlags {
    bits: usize,
}

impl PteFlags {
    /// Valid.
    pub const V: Self = Self { bits: 1 << 0 };

    /// Dirty / modified.
    pub const D: Self = Self { bits: 1 << 1 };

    /// PLV bits.
    ///
    /// PLV0 is kernel mode.
    pub const PLV0: Self = Self { bits: 0 << 2 };
    pub const PLV1: Self = Self { bits: 1 << 2 };
    pub const PLV2: Self = Self { bits: 2 << 2 };
    pub const PLV3: Self = Self { bits: 3 << 2 };

    /// Memory access type.
    ///
    /// For the first version:
    /// - MAT_CC is good for normal cached memory.
    /// - MAT_SUC can be used for MMIO later.
    pub const MAT_SUC: Self = Self { bits: 0 << 4 };
    pub const MAT_CC: Self = Self { bits: 1 << 4 };

    /// Global mapping.
    pub const G: Self = Self { bits: 1 << 6 };

    /// Page exists / present.
    pub const P: Self = Self { bits: 1 << 7 };

    /// Writable.
    pub const W: Self = Self { bits: 1 << 8 };

    /// Not readable.
    pub const NR: Self = Self { bits: 1usize << 61 };

    /// Not executable.
    pub const NX: Self = Self { bits: 1usize << 62 };

    /// Restricted privilege level.
    pub const RPLV: Self = Self { bits: 1usize << 63 };

    pub const fn empty() -> Self {
        Self { bits: 0 }
    }

    pub const fn bits(self) -> usize {
        self.bits
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

impl PageTableEntry {
    pub const fn empty() -> Self {
        Self { bits: 0 }
    }

    pub fn new(ppn: PhysPageNum, flags: PteFlags) -> Self {
        Self {
            bits: (ppn.0 << PAGE_SIZE_BITS) | flags.bits(),
        }
    }

    pub const fn from_bits(bits: usize) -> Self {
        Self { bits }
    }

    pub fn bits(self) -> usize {
        self.bits
    }

    pub fn ppn(self) -> PhysPageNum {
        // QEMU/early bring-up usually uses low physical addresses.
        // Keep a conservative 48-bit physical address mask for now.
        // Later this can be derived from CPUCFG PALEN.
        const PA_WIDTH: usize = 48;
        const PA_MASK: usize = ((1usize << PA_WIDTH) - 1) & !((1usize << PAGE_SIZE_BITS) - 1);

        PhysPageNum((self.bits & PA_MASK) >> PAGE_SIZE_BITS)
    }

    pub fn flags(self) -> PteFlags {
        PteFlags {
            bits: self.bits & !(((1usize << 48) - 1) & !((1usize << PAGE_SIZE_BITS) - 1)),
        }
    }

    pub fn is_valid(self) -> bool {
        self.bits & PteFlags::V.bits() != 0
    }

    pub fn is_present(self) -> bool {
        self.bits & PteFlags::P.bits() != 0
    }

    pub fn writable(self) -> bool {
        self.bits & PteFlags::W.bits() != 0
    }

    pub fn readable(self) -> bool {
        self.bits & PteFlags::NR.bits() == 0
    }

    pub fn executable(self) -> bool {
        self.bits & PteFlags::NX.bits() == 0
    }
}

impl PageTable {
    pub fn new() -> Self {
        let frame = alloc_frame().expect("failed to allocate LoongArch root page table");
        let tracker = FrameTracker::new(frame);

        Self {
            root_ppn: frame,
            frames: vec![tracker],
        }
    }

    pub fn root_ppn(&self) -> PhysPageNum {
        self.root_ppn
    }

    fn find_pte_create(&mut self, vpn: VirtPageNum) -> Option<&'static mut usize> {
        let idxs = vpn_indexes(vpn);
        let mut ppn = self.root_ppn;

        // Walk Dir3 -> Dir2 -> Dir1.
        //
        // LoongArch directory entries used by LDDIR are NOT normal leaf PTEs.
        // If bit 6 is 0, the entry is treated as the physical base address
        // of the next-level page table.
        //
        // Therefore we store:
        //
        //     next_table_phys_addr
        //
        // not:
        //
        //     ppn << 12 | flags
        //
        // and definitely not RISC-V style:
        //
        //     ppn << 10 | V
        for level in 0..3 {
            let entries = raw_entry_array(ppn);
            let entry = &mut entries[idxs[level]];

            if *entry == 0 {
                let frame = alloc_frame()?;

                // 页表页必须清零：alloc_frame 不保证返回干净页。
                // 诊断打印（确认修复有效后可删）：看看到底有多少脏帧、脏在哪
                let pa = frame.0 << PAGE_SIZE_BITS;
                let va = crate::mm::kernel_phys_to_virt(pa);
                let slice = unsafe { core::slice::from_raw_parts(va as *const usize, PAGE_SIZE / 8) };
                // if let Some((i, &v)) = slice.iter().enumerate().find(|(_, &v)| v != 0) {
                //     crate::println!("[la64 pt] dirty frame from alloc_frame: ppn={:#x} idx={} val={:#x}",
                //                     frame.0, i, v);
                // }
                unsafe { core::ptr::write_bytes(va as *mut u8, 0, PAGE_SIZE); }

                let tracker = FrameTracker::new(frame);
                *entry = frame.0 << PAGE_SIZE_BITS;
                self.frames.push(tracker);
            } else if *entry & HUGE_DIRECTORY != 0 {
                panic!(
                    "cannot create a 4KiB mapping below a huge directory entry: vpn {:?}, level {}",
                    vpn,
                    level
                );
            }
            ppn = PhysPageNum(*entry >> PAGE_SIZE_BITS);
        }

        let entries = raw_entry_array(ppn);
        Some(&mut entries[idxs[3]])
    }

    fn find_pte(&self, vpn: VirtPageNum) -> Option<&'static mut usize> {
        let idxs = vpn_indexes(vpn);
        let mut ppn = self.root_ppn;

        for level in 0..3 {
            let entries = raw_entry_array(ppn);
            let entry = entries[idxs[level]];

            if entry == 0 {
                return None;
            }

            // A directory entry with bit 6 set terminates the walk as a
            // huge leaf. translate() is still a 4KiB-PTE lookup helper, so
            // report no ordinary PTE instead of treating the physical page
            // base as another page-table page.
            if entry & HUGE_DIRECTORY != 0 {
                return None;
            }

            ppn = PhysPageNum(entry >> PAGE_SIZE_BITS);
        }

        let entries = raw_entry_array(ppn);
        Some(&mut entries[idxs[3]])
    }

    pub fn map(&mut self, vpn: VirtPageNum, ppn: PhysPageNum, flags: PteFlags) {
        let pte = self
            .find_pte_create(vpn)
            .expect("failed to create LoongArch pte");

        if *pte != 0 {
            let old = PageTableEntry::from_bits(*pte);
            if old.ppn().0 == ppn.0 {
                return;  // 同一映射重复建立，直接容忍（ELF 段重叠很常见）
            }
            panic!(
                "[la64 map] vpn {:?} already mapped: old_ppn={:?}, old_bits={:#x}, new_ppn={:?}, new_flags={:#x}",
                vpn,
                old.ppn(),
                old.bits(),
                ppn,
                flags.bits(),
            );
        }

        let flags = flags
            .union(PteFlags::V)
            .union(PteFlags::P);

        *pte = PageTableEntry::new(ppn, flags).bits();
    }

    /// Map `[va, va + size)` to `[pa, pa + size)` with the largest usable
    /// LoongArch leaf at every step.
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

            if page_size == PAGE_SIZE {
                self.map(VirtAddr(va).floor(), PhysAddr(pa).floor(), flags);
            } else {
                self.map_huge_leaf(VirtAddr(va), PhysAddr(pa), page_size, flags);
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

    fn map_huge_leaf(
        &mut self,
        va: VirtAddr,
        pa: PhysAddr,
        page_size: usize,
        flags: PteFlags,
    ) {
        assert!(
            va.0 % page_size == 0,
            "huge leaf VA is not aligned: va={:#x}, size={:#x}",
            va.0,
            page_size
        );
        assert!(
            pa.0 % page_size == 0,
            "huge leaf PA is not aligned: pa={:#x}, size={:#x}",
            pa.0,
            page_size
        );

        let idxs = vpn_indexes(va.floor());
        let entry = match page_size {
            DIR3_PAGE_SIZE => &mut raw_entry_array(self.root_ppn)[idxs[0]],
            DIR2_PAGE_SIZE => {
                let dir2 = self.create_next_directory(self.root_ppn, idxs[0]);
                &mut raw_entry_array(dir2)[idxs[1]]
            }
            DIR1_PAGE_SIZE => {
                let dir2 = self.create_next_directory(self.root_ppn, idxs[0]);
                let dir1 = self.create_next_directory(dir2, idxs[1]);
                &mut raw_entry_array(dir1)[idxs[2]]
            }
            _ => unreachable!("map_huge_leaf called with a non-huge page size"),
        };

        assert!(
            *entry == 0,
            "huge mapping conflicts at va={:#x}, old_entry={:#x}",
            va.0,
            *entry
        );

        *entry = huge_directory_entry(pa, flags);
    }

    fn create_next_directory(
        &mut self,
        table_ppn: PhysPageNum,
        index: usize,
    ) -> PhysPageNum {
        let entry = &mut raw_entry_array(table_ppn)[index];

        if *entry == 0 {
            let frame = alloc_frame().expect("failed to allocate LoongArch page table");
            let pa = frame.0 << PAGE_SIZE_BITS;
            let va = crate::mm::kernel_phys_to_virt(pa);
            unsafe { core::ptr::write_bytes(va as *mut u8, 0, PAGE_SIZE); }

            let tracker = FrameTracker::new(frame);
            *entry = pa;
            self.frames.push(tracker);
        }

        assert!(
            *entry & HUGE_DIRECTORY == 0,
            "cannot create a lower page table below a huge leaf"
        );

        PhysPageNum(*entry >> PAGE_SIZE_BITS)
    }

    /// Return whether any 4KiB/2MiB/1GiB/512GiB mapping covers `va`.
    ///
    /// Unlike translate(), this is safe to call on an address covered by a
    /// huge directory leaf.
    pub fn contains_va(&self, va: VirtAddr) -> bool {
        let idxs = vpn_indexes(va.floor());
        let mut ppn = self.root_ppn;

        for level in 0..4 {
            let entry = raw_entry_array(ppn)[idxs[level]];
            if entry == 0 {
                return false;
            }

            if level == 3 {
                return entry & PteFlags::V.bits() != 0;
            }

            if entry & HUGE_DIRECTORY != 0 {
                return entry & PteFlags::V.bits() != 0;
            }

            ppn = PhysPageNum(entry >> PAGE_SIZE_BITS);
        }

        false
    }


    /// mprotect 用: 只改已有 PTE 的权限位, 不重新映射(不分配新帧)。
    pub fn update_flags(&mut self, vpn: VirtPageNum, flags: PteFlags) {
        let pte = self.find_pte(vpn).expect("update_flags: pte not found");
        assert!(*pte != 0, "update_flags: vpn {:?} is invalid", vpn);
        let old = PageTableEntry::from_bits(*pte);
        *pte = PageTableEntry::new(old.ppn(), flags.union(PteFlags::V).union(PteFlags::P)).bits();
    }


    pub fn unmap(&mut self, vpn: VirtPageNum) {
        let pte = self.find_pte(vpn).expect("pte not found");
        assert!(*pte != 0, "vpn {:?} is invalid", vpn);
        *pte = 0;
    }

    pub fn translate(&self, vpn: VirtPageNum) -> Option<PageTableEntry> {
        self.find_pte(vpn)
            .map(|entry| PageTableEntry::from_bits(*entry))
            .filter(|pte| pte.is_valid())
    }
}

pub fn map_range_identity(
    pt: &mut PageTable,
    start: usize,
    end: usize,
    flags: PteFlags,
) {
    let start = align_down(start, PAGE_SIZE);
    let end = align_up(end, PAGE_SIZE);

    if start < end {
        pt.map_range(
            VirtAddr::from(start),
            PhysAddr::from(start),
            end - start,
            flags,
        );
    }
}
pub fn map_range_identity_exclude(
    pt: &mut PageTable,
    start: usize,
    end: usize,
    exclude_start: usize,
    exclude_end: usize,
    flags: PteFlags,
) {
    let start = align_down(start, PAGE_SIZE);
    let end = align_up(end, PAGE_SIZE);

    let exclude_start = align_down(exclude_start, PAGE_SIZE);
    let exclude_end = align_up(exclude_end, PAGE_SIZE);

    if start < exclude_start {
        map_range_identity(
            pt,
            start,
            exclude_start.min(end),
            flags,
        );
    }

    if exclude_end < end {
        map_range_identity(
            pt,
            exclude_end.max(start),
            end,
            flags,
        );
    }
}

/// Kernel normal memory: readable, writable, executable.
///
/// This is intentionally broad for bring-up. Later split into:
/// - text: RX
/// - rodata: R
/// - data/bss/heap: RW + NX
pub fn kernel_rwx_flags() -> PteFlags {
    PteFlags::D
        .union(PteFlags::W)
        .union(PteFlags::MAT_CC)
        .union(PteFlags::G)
}

/// Kernel read/write memory, non-executable.
pub fn kernel_rw_flags() -> PteFlags {
    PteFlags::D
        .union(PteFlags::W)
        .union(PteFlags::MAT_CC)
        .union(PteFlags::G)
        .union(PteFlags::NX)
}

/// Kernel read/execute memory.
pub fn kernel_rx_flags() -> PteFlags {
    PteFlags::MAT_CC
        .union(PteFlags::G)
}

/// MMIO mapping: read/write, non-executable, strongly ordered / uncached-ish.
///
/// If your UART behaves weirdly after paging, use this for UART instead of
/// kernel_rwx_flags().
pub fn mmio_rw_flags() -> PteFlags {
    PteFlags::D
        .union(PteFlags::W)
        .union(PteFlags::MAT_SUC)
        .union(PteFlags::G)
        .union(PteFlags::NX)
}

fn huge_directory_entry(pa: PhysAddr, flags: PteFlags) -> usize {
    assert!(
        pa.0 & (DIR1_PAGE_SIZE - 1) == 0,
        "huge leaf PA must be at least 2MiB aligned: {:#x}",
        pa.0
    );

    // Bit 6 means "huge directory leaf" here, so the common-PTE G bit must
    // be moved to bit 12. Bits 14:13 stay clear in memory; LDDIR marks the
    // directory level while walking the entry.
    let mut bits = pa.0
        | (flags.bits() & !PteFlags::G.bits())
        | HUGE_DIRECTORY
        | PteFlags::V.bits()
        | PteFlags::P.bits();

    if flags.contains(PteFlags::G) {
        bits |= HUGE_GLOBAL;
    }

    bits
}

fn raw_entry_array(ppn: PhysPageNum) -> &'static mut [usize] {
    let pa = ppn.0 << PAGE_SIZE_BITS;
    let va = crate::mm::kernel_phys_to_virt(pa);

    assert!(pa != 0, "raw_entry_array: ppn is zero");
    assert!(
        va >= crate::mm::KERNEL_OFFSET,
        "raw_entry_array: va is not high-half: ppn={:#x}, pa={:#x}, va={:#x}",
        ppn.0,
        pa,
        va
    );

    unsafe {
        core::slice::from_raw_parts_mut(
            va as *mut usize,
            PAGE_SIZE / core::mem::size_of::<usize>(),
        )
    }
}

fn vpn_indexes(vpn: VirtPageNum) -> [usize; 4] {
    [
        (vpn.0 >> 27) & 0x1ff, // VA[47:39]
        (vpn.0 >> 18) & 0x1ff, // VA[38:30]
        (vpn.0 >> 9) & 0x1ff,  // VA[29:21]
        vpn.0 & 0x1ff,         // VA[20:12]
    ]
}



pub fn map_range(
    pt: &mut PageTable,
    va_start: usize,
    pa_start: usize,
    size: usize,
    flags: PteFlags,
) {
    let va = align_down(va_start, PAGE_SIZE);
    let pa = align_down(pa_start, PAGE_SIZE);
    let end = align_up(va_start + size, PAGE_SIZE);

    pt.map_range(
        VirtAddr::from(va),
        PhysAddr::from(pa),
        end - va,
        flags,
    );
}
