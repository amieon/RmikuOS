    .section .text.tlb_refill, "ax"
    .global __tlb_refill
    .type __tlb_refill, @function
    .balign 4096

/*
 * LoongArch64 TLB refill handler.
 *
 * Do not use stack here.
 * Stack access itself may be the reason we entered TLB refill.
 *
 * Page table shape:
 *
 *   PGD/root -> Dir3 -> Dir2 -> Dir1 -> PT
 *
 * PWCL/PWCH:
 *   PT    VA[20:12]
 *   Dir1  VA[29:21]
 *   Dir2  VA[38:30]
 *   Dir3  VA[47:39]
 */
__tlb_refill:
    /*
     * Save one temporary register.
     *
     * CSR.TLBRSAVE = 0x8b
     */
    csrwr   $t0, 0x8b

    /*
     * CSR.PGD = 0x1b
     *
     * PGD automatically chooses PGDL or PGDH according to the bad VA.
     * In TLB refill context, the bad VA is CSR.TLBRBADV.
     */
    csrrd   $t0, 0x1b

    /*
     * Walk page table.
     *
     * LDDIR/LDPTE use CSR.TLBRBADV in TLB refill context.
     *
     * For our 4-level layout:
     *   root has Dir3 entries
     *   then Dir2
     *   then Dir1
     *   then final PT
     *
     * If this sequence does not work on your QEMU version, try changing
     * the first lddir level from 3 to 4 and enable Dir4 in PWCH.
     */
    lddir   $t0, $t0, 3
    lddir   $t0, $t0, 2
    lddir   $t0, $t0, 1

    /*
     * Load even/odd PTE into CSR.TLBRELO0 and CSR.TLBRELO1.
     * LoongArch TLB entry covers a pair of adjacent pages.
     */
    ldpte   $t0, 0
    ldpte   $t0, 1

    /*
     * Fill one random TLB entry.
     */
    tlbfill

    /*
     * Restore temporary register and return.
     */
    csrrd   $t0, 0x8b
    ertn