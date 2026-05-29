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
     * Do NOT use 0x30 here.
     *
     * trap.S uses:
     *   KSAVE0 / 0x30 = kernel_stack_top while running user
     *
     * TLB refill may happen while running user.  If we overwrite 0x30,
     * the next user trap cannot switch to the kernel stack.
     *
     * Use only 0x31/0x32 for temporary saves.
     */
    .equ CSR_SAVE_T1,    0x31
    .equ CSR_SAVE_T2,    0x32

    /*
     * Real TLB entry flags, not page-table-walker flags.
     *
     * V | D | MAT_CC | G
     */
    .equ TLB_KERNEL_RWX, 0x53


__tlb_refill:
    /*
     * Save temporaries used by this handler.
     *
     * csrwr clobbers its source register with old CSR value,
     * so do not use these registers until after they are saved.
     */
    csrwr   $t0, CSR_TLBRSAVE
    csrwr   $t1, CSR_SAVE_T1
    csrwr   $t2, CSR_SAVE_T2

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
    and     $t2, $t0, $t1
    ori     $t2, $t2, 12
    csrwr   $t2, CSR_TLBREHI

    /*
     * Walk current page table.
     *
     * With your current 4-level layout:
     *   Dir3 = VA[47:39]
     *   Dir2 = VA[38:30]
     *   Dir1 = VA[29:21]
     *   PT   = VA[20:12]
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
    csrrd   $t0, CSR_TLBRBADV

    /*
     * Set TLBREHI:
     *   VPPN = BADV with low 13 bits cleared
     *   PS   = 12 for 4KiB page
     */
    li.d    $t1, -8192
    and     $t2, $t0, $t1
    ori     $t2, $t2, 12
    csrwr   $t2, CSR_TLBREHI

    /*
     * PA = BADV - KERNEL_OFFSET.
     */
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
    or      $t2, $t0, $t1
    csrwr   $t2, CSR_TLBRELO0

    /*
     * TLBRELO1 = odd page.
     */
    lu12i.w $t2, 1          /* 0x1000 */
    add.d   $t0, $t0, $t2

    li.d    $t1, TLB_KERNEL_RWX
    or      $t2, $t0, $t1
    csrwr   $t2, CSR_TLBRELO1

    tlbfill


.Lrestore_and_ertn:
    /*
     * Restore temporaries.
     */
    csrrd   $t2, CSR_SAVE_T2
    csrrd   $t1, CSR_SAVE_T1
    csrrd   $t0, CSR_TLBRSAVE

    ertn