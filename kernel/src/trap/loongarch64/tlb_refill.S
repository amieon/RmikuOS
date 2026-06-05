    .section .text.tlb_refill, "ax"
    .global __tlb_refill
    .type __tlb_refill, @function
    .balign 4096

    /*
     * Canonical high-half offset.
     */
    .equ KERNEL_OFFSET,  0xffff800000000000

    /*
     * TLB refill CSRs.
     */
    .equ CSR_PGD,        0x1b
    .equ CSR_TLBRBADV,   0x89
    .equ CSR_TLBRSAVE,   0x8b
    .equ CSR_TLBRELO0,   0x8c
    .equ CSR_TLBRELO1,   0x8d
    .equ CSR_TLBREHI,    0x8e

    /*
     * Do NOT use 0x30 / 0x31 / 0x32 here.
     *
     * trap.S uses:
     *   0x30 = KSAVE0 = kernel_stack_top while running user
     *   0x31 = KSAVE1 = saved original user r12
     *   0x32 = KSAVE2 = saved original user r13
     *
     * TLB refill may happen while running user or while trap.S is saving
     * registers, so overwriting 0x31 / 0x32 can corrupt TrapContext.
     *
     * This file only uses:
     *   CSR_TLBRSAVE = save original t0/r12
     *   0x33         = KSAVE3, save original t1/r13
     */
    .equ CSR_SAVE_T1,    0x33

    /*
     * Real TLB entry flags, not page-table-walker flags.
     *
     * V | D | MAT_CC | G
     *
     * Used only for kernel high-half direct-map refill.
     */
    .equ TLB_KERNEL_RWX, 0x53


__tlb_refill:
    /*
     * Save temporaries.
     *
     * csrwr writes the GPR value into CSR and returns old CSR value into
     * the same GPR, so t0/t1 are clobbered after these instructions.
     * That is fine because their original values are now saved.
     */
    csrwr   $t0, CSR_TLBRSAVE
    csrwr   $t1, CSR_SAVE_T1

    /*
     * Decide by faulting VA.
     *
     * Canonical high-half kernel addresses have bit 63 set, so signed
     * comparison sees them as negative. User addresses are low positive.
     */
    csrrd   $t0, CSR_TLBRBADV
    blt     $t0, $zero, .Lkernel_direct_refill


/*
 * User / low-address refill.
 *
 * This uses the currently active page table root through CSR.PGD.
 */
.Lpage_table_refill:
    /*
     * Set TLBREHI for the faulting user VA.
     *
     * TLB entry covers an even/odd 4KiB page pair:
     *   clear low 13 bits, then set PS=12.
     */
    csrrd   $t0, CSR_TLBRBADV
    li.d    $t1, -8192
    and     $t0, $t0, $t1
    ori     $t0, $t0, 12
    csrwr   $t0, CSR_TLBREHI

    /*
     * Walk current page table.
     *
     * Current 4-level layout:
     *   Dir3 = VA[47:39]
     *   Dir2 = VA[38:30]
     *   Dir1 = VA[29:21]
     *   PT   = VA[20:12]
     *
     * ldpte loads the even/odd page pair into TLBRELO0 / TLBRELO1.
     */
    csrrd   $t0, CSR_PGD

    lddir   $t0, $t0, 3
    lddir   $t0, $t0, 2
    lddir   $t0, $t0, 1

    ldpte   $t0, 0
    ldpte   $t0, 1

    tlbfill

    b       .Lrestore_and_ertn


/*
 * Kernel high-half direct-map refill.
 *
 *   VA = KERNEL_OFFSET + PA
 */
.Lkernel_direct_refill:
    /*
     * Set TLBREHI:
     *   VPPN = BADV with low 13 bits cleared
     *   PS   = 12 for 4KiB page
     */
    csrrd   $t0, CSR_TLBRBADV
    li.d    $t1, -8192
    and     $t0, $t0, $t1
    ori     $t0, $t0, 12
    csrwr   $t0, CSR_TLBREHI

    /*
     * PA = BADV - KERNEL_OFFSET.
     */
    csrrd   $t0, CSR_TLBRBADV
    li.d    $t1, KERNEL_OFFSET
    sub.d   $t0, $t0, $t1

    /*
     * Even-page pair PA base.
     */
    li.d    $t1, -8192
    and     $t0, $t0, $t1

    /*
     * TLBRELO0 = even page.
     */
    li.d    $t1, TLB_KERNEL_RWX
    or      $t1, $t0, $t1
    csrwr   $t1, CSR_TLBRELO0

    /*
     * TLBRELO1 = odd page.
     */
    lu12i.w $t1, 1              /* 0x1000 */
    add.d   $t0, $t0, $t1

    li.d    $t1, TLB_KERNEL_RWX
    or      $t1, $t0, $t1
    csrwr   $t1, CSR_TLBRELO1

    tlbfill


.Lrestore_and_ertn:
    /*
     * Restore temporaries.
     */
    csrrd   $t1, CSR_SAVE_T1
    csrrd   $t0, CSR_TLBRSAVE

    ertn