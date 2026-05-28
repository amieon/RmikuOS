    .section .text.tlb_refill, "ax"
    .global __tlb_refill
    .type __tlb_refill, @function
    .balign 4096

/*
 * LoongArch64 TLB refill handler.
 *
 * Do not use stack here.
 *
 * Page table shape:
 *
 *   root -> Dir3 -> Dir2 -> Dir1 -> PT
 *
 * PWCL/PWCH:
 *   PT    VA[20:12]
 *   Dir1  VA[29:21]
 *   Dir2  VA[38:30]
 *   Dir3  VA[47:39]
 *
 * LDDIR level mapping:
 *   level 4 -> Dir3
 *   level 3 -> Dir2
 *   level 2 -> Dir1
 *   then LDPTE reads PT entry.
 */
__tlb_refill:
    /*
     * Save t0.
     *
     * CSR.TLBRSAVE = 0x8b
     */
    csrwr   $t0, 0x8b

    /*
     * CSR.PGD = 0x1b
     * It selects PGDL/PGDH according to bad VA.
     */
    csrrd   $t0, 0x1b

    /*
     * Correct 4-level walk:
     *
     * root -> Dir3 -> Dir2 -> Dir1 -> PT
     */
    lddir   $t0, $t0, 4
    lddir   $t0, $t0, 3
    lddir   $t0, $t0, 2

    /*
     * Load even/odd PTEs.
     */
    ldpte   $t0, 0
    ldpte   $t0, 1

    tlbfill

    csrrd   $t0, 0x8b
    ertn