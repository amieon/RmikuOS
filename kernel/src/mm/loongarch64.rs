use core::arch::asm;

use crate::mm::{PhysPageNum, PAGE_SIZE_BITS};

unsafe extern "C" {
    fn __tlb_refill();
}

const CRMD_DA: usize = 1 << 3;
const CRMD_PG: usize = 1 << 4;

pub fn early_putc(ch: u8) {
    let uart = crate::arch::UART_BASE as *mut u8;

    unsafe {
        while uart.add(5).read_volatile() & 0x20 == 0 {}
        uart.write_volatile(ch);
    }
}

pub fn activate_kernel_page_table_by_root(root_ppn: PhysPageNum) {
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
        (39usize << 0) | // Dir3_base
        (9usize  << 6); // Dir3_width

    let refill_va = __tlb_refill as usize;
    let refill_pa = crate::mm::virt_to_phys(refill_va);

    unsafe {

        early_putc(b'd');
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

        let mut crmd: usize;
        asm!("csrrd {0}, 0x0", out(reg) crmd, options(nostack));

        const CRMD_DA: usize = 1 << 3;
        const CRMD_PG: usize = 1 << 4;

        crmd &= !CRMD_DA;
        crmd |= CRMD_PG;

        asm!("csrwr {0}, 0x0", in(reg) crmd, options(nostack));
        asm!("ibar 0", "dbar 0", options(nostack));

        /*
         * Bring-up 阶段：
         *
         * 清 DMW0，保证低地址用户空间必须走页表。
         * 保留 DMW1，让内核高半继续稳定运行。
         */

        asm!(
            "csrwr $zero, 0x180", // DMW0
            "csrwr $zero, 0x181", // DMW1: keep high direct map
            "csrwr $zero, 0x182",
            "csrwr $zero, 0x183",
            options(nostack)
        );

    }
}




pub fn activate_page_table(root_ppn: PhysPageNum) {
    use core::arch::asm;

    let root_pa = root_ppn.0 << crate::mm::PAGE_SIZE_BITS;

    let old_pgdl: usize;
    let old_pgdh: usize;
    let new_pgdl: usize;
    let new_pgdh: usize;

    unsafe {
        asm!("csrrd {}, 0x19", out(reg) old_pgdl, options(nostack));
        asm!("csrrd {}, 0x1a", out(reg) old_pgdh, options(nostack));

        let mut pgdl = root_pa;
        asm!(
            "csrwr {0}, 0x19",
            inout(reg) pgdl => _,
            options(nostack),
        );

        let mut pgdh = root_pa;
        asm!(
            "csrwr {0}, 0x1a",
            inout(reg) pgdh => _,
            options(nostack),
        );

        /*
         * 强制全局 TLB 失效。
         * 如果 tlbflush 在 QEMU/当前环境下没有清掉普通用户 TLB，
         * invtlb all 会更直接。
         */
        asm!(
            "dbar 0",
            "invtlb 0x0, $r0, $r0",
            "ibar 0",
            options(nostack),
        );

        asm!("csrrd {}, 0x19", out(reg) new_pgdl, options(nostack));
        asm!("csrrd {}, 0x1a", out(reg) new_pgdh, options(nostack));
    }

    log::info!(
        "[mm] activate_page_table: root={:?}, root_pa={:#x}, PGDL {:#x}->{:#x}, PGDH {:#x}->{:#x}",
        root_ppn,
        root_pa,
        old_pgdl,
        new_pgdl,
        old_pgdh,
        new_pgdh,
    );
}