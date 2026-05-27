
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
    VirtPageNum, PAGE_SIZE, PAGE_SIZE_BITS,
};

use super::FrameTracker;

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
/// VA[20:12] -> PTE
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
                let tracker = FrameTracker::new(frame);

                // Non-leaf directory entry:
                // physical address of next-level table.
                //
                // Keep bit 6 clear, otherwise LDDIR treats it as a huge page.
                *entry = frame.0 << PAGE_SIZE_BITS;

                self.frames.push(tracker);
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

            // If bit 6 is set here, this is a huge-page directory entry.
            // We do not support huge pages yet.
            assert!(
                entry & (1 << 6) == 0,
                "LoongArch huge page directory entry is not supported yet"
            );

            ppn = PhysPageNum(entry >> PAGE_SIZE_BITS);
        }

        let entries = raw_entry_array(ppn);
        Some(&mut entries[idxs[3]])
    }

    pub fn map(&mut self, vpn: VirtPageNum, ppn: PhysPageNum, flags: PteFlags) {
        let pte = self
            .find_pte_create(vpn)
            .expect("failed to create LoongArch pte");

        assert!(*pte == 0, "vpn {:?} is already mapped", vpn);

        let flags = flags
            .union(PteFlags::V)
            .union(PteFlags::P)
            .union(PteFlags::PLV0);

        *pte = PageTableEntry::new(ppn, flags).bits();
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
    let mut addr = align_down(start, PAGE_SIZE);
    let end = align_up(end, PAGE_SIZE);

    while addr < end {
        pt.map(
            crate::mm::VirtAddr::from(addr).floor(),
            PhysAddr::from(addr).floor(),
            flags,
        );
        addr += PAGE_SIZE;
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

fn raw_entry_array(ppn: PhysPageNum) -> &'static mut [usize] {
    let pa = ppn.0 << PAGE_SIZE_BITS;
    unsafe { core::slice::from_raw_parts_mut(pa as *mut usize, PAGE_SIZE / core::mem::size_of::<usize>()) }
}

fn vpn_indexes(vpn: VirtPageNum) -> [usize; 4] {
    [
        (vpn.0 >> 27) & 0x1ff, // VA[47:39]
        (vpn.0 >> 18) & 0x1ff, // VA[38:30]
        (vpn.0 >> 9) & 0x1ff,  // VA[29:21]
        vpn.0 & 0x1ff,         // VA[20:12]
    ]
}






unsafe extern "C" {
    fn __tlb_refill();
}
/// Activate LoongArch64 page-mapped mode.
///
/// This expects:
/// - a LoongArch-format page table,
/// - non-leaf directory entries storing next-table physical addresses,
/// - leaf PTEs using LoongArch TLBRELO format,
/// - `__tlb_refill` installed and 4KiB-aligned.
pub fn activate_kernel_page_table(root_ppn: PhysPageNum) {
    let root_pa = root_ppn.0 << PAGE_SIZE_BITS;

    /*
     * 4KiB page, 4-level table:
     *
     * VA[47:39] Dir3
     * VA[38:30] Dir2
     * VA[29:21] Dir1
     * VA[20:12] PT
     * VA[11:0]  offset
     *
     * PWCL:
     *   PTbase      bits 4:0
     *   PTwidth     bits 9:5
     *   Dir1_base   bits 14:10
     *   Dir1_width  bits 19:15
     *   Dir2_base   bits 24:20
     *   Dir2_width  bits 29:25
     *   PTEWidth    bits 31:30, 0 means 64-bit PTE
     *
     * PWCH:
     *   Dir3_base   bits 5:0
     *   Dir3_width  bits 11:6
     *   Dir4_base   bits 17:12
     *   Dir4_width  bits 23:18
     */
    let pwcl =
        (12usize << 0)  | // PTbase
        (9usize  << 5)  | // PTwidth
        (21usize << 10) | // Dir1_base
        (9usize  << 15) | // Dir1_width
        (30usize << 20) | // Dir2_base
        (9usize  << 25) | // Dir2_width
        (0usize  << 30);  // PTEWidth = 64-bit

    let pwch =
        (39usize << 0)  | // Dir3_base
        (9usize  << 6)  | // Dir3_width
        (0usize  << 12) | // Dir4_base
        (0usize  << 18);  // Dir4_width = no Dir4

    let refill_pa = __tlb_refill as usize;

    unsafe {
        /*
         * Clear DMWs first.
         *
         * If DMW covers current VA, page table refill may not be tested at all.
         * For bring-up, we want real TLB refill to happen.
         */
        asm!(
            "csrwr $zero, 0x180", // DMW0
            "csrwr $zero, 0x181", // DMW1
            "csrwr $zero, 0x182", // DMW2
            "csrwr $zero, 0x183", // DMW3
            options(nostack)
        );

        /*
         * PGDL 0x19
         * PGDH 0x1a
         * PWCL 0x1c
         * PWCH 0x1d
         * STLBPS 0x1e
         * TLBRENTRY 0x88
         */
        asm!(
            "csrwr {pgdl}, 0x19",
            "csrwr {pgdh}, 0x1a",
            "csrwr {pwcl}, 0x1c",
            "csrwr {pwch}, 0x1d",
            "csrwr {stlbps}, 0x1e",
            "csrwr {tlbrentry}, 0x88",
            pgdl = in(reg) root_pa,
            pgdh = in(reg) root_pa,
            pwcl = in(reg) pwcl,
            pwch = in(reg) pwch,
            stlbps = in(reg) 12usize,       // 4KiB
            tlbrentry = in(reg) refill_pa,
            options(nostack)
        );

        /*
         * Flush old TLB entries.
         */
        asm!("tlbflush", options(nostack));

        /*
         * Switch from direct-address mode to mapped-address mode:
         *
         * CRMD.DA = 0
         * CRMD.PG = 1
         */
        let mut crmd: usize;
        asm!("csrrd {0}, 0x0", out(reg) crmd, options(nostack));

        crmd &= !CRMD_DA;
        crmd |= CRMD_PG;

        asm!("csrwr {0}, 0x0", in(reg) crmd, options(nostack));

        /*
         * After switching mode, immediately serialize a little.
         */
        asm!("ibar 0", "dbar 0", options(nostack));
    }

    log::info!(
        "[mm] LoongArch paging enabled: root_pa={:#x}, refill_pa={:#x}",
        root_pa,
        refill_pa
    );
}