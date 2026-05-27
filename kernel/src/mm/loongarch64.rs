use core::arch::asm;

use crate::mm::{PhysPageNum, PAGE_SIZE_BITS};

unsafe extern "C" {
    fn __tlb_refill();
}

const CRMD_DA: usize = 1 << 3;
const CRMD_PG: usize = 1 << 4;

pub fn activate_kernel_page_table(root_ppn: PhysPageNum) {
    let root_pa = root_ppn.0 << PAGE_SIZE_BITS;

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
         * 注意：这里先不要清 DMW。
         * 当前高半内核还靠 DMW1 跑。
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
            stlbps = in(reg) 12usize,
            tlbrentry = in(reg) refill_pa,
            options(nostack)
        );

        asm!("tlbflush", options(nostack));

        /*
         * boot.S 已经设置过 DA=0, PG=1。
         * 这里再设置一次没问题，但不是关键。
         */
        let mut crmd: usize;
        asm!("csrrd {0}, 0x0", out(reg) crmd, options(nostack));

        crmd &= !CRMD_DA;
        crmd |= CRMD_PG;

        asm!("csrwr {0}, 0x0", in(reg) crmd, options(nostack));
        asm!("ibar 0", "dbar 0", options(nostack));
    }

    log::info!(
        "[mm] LoongArch page table prepared: root_pa={:#x}, refill_va={:#x}, refill_pa={:#x}",
        root_pa,
        refill_va,
        refill_pa
    );
}