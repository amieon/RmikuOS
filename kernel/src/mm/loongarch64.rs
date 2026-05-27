use core::arch::asm;

use crate::mm::{PhysPageNum, PAGE_SIZE_BITS};

unsafe extern "C" {
    fn __tlb_refill();
}


const CRMD_DA: usize = 1 << 3;
const CRMD_PG: usize = 1 << 4;

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

    let refill_va = __tlb_refill as usize;
    let refill_pa = crate::mm::virt_to_phys(refill_va);

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