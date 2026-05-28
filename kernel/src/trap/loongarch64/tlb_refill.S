    .section .text.tlb_refill, "ax"
    .global __tlb_refill
    .type __tlb_refill, @function
    .balign 4096

    .equ UART_PADDR,     0x1fe001e0
    .equ KERNEL_OFFSET,  0xffff800000000000

    .equ CSR_TLBRBADV,   0x89
    .equ CSR_TLBRSAVE,   0x8b
    .equ CSR_TLBRELO0,   0x8c
    .equ CSR_TLBRELO1,   0x8d
    .equ CSR_TLBREHI,    0x8e

    /*
     * TLB entry flags, not page-table-walker flags.
     *
     * V | D | MAT_CC | G
     * = 0x01 | 0x02 | 0x10 | 0x40
     * = 0x53
     *
     * Do NOT use 0x1d3 here. 0x1d3 includes P/W bits used by page walk,
     * but TLBRELO bits 11:7 are reserved zero in real TLB entry format.
     */
    .equ TLB_KERNEL_RWX,  0x53

__tlb_refill:
    csrwr   $t0, CSR_TLBRSAVE

    /*
     * Debug: print A.
     */
    li.d    $t1, UART_PADDR
1:
    ld.bu   $t2, $t1, 5
    andi    $t2, $t2, 0x20
    beqz    $t2, 1b
    li.w    $t2, 'A'
    st.b    $t2, $t1, 0

    /*
     * BADV = faulting high-half VA.
     */
    csrrd   $t0, CSR_TLBRBADV

    /*
     * Set TLBREHI:
     *
     * VPPN = BADV[...:13]
     * PS   = 12  => 4 KiB page
     *
     * Clear low 13 bits, then OR PS=12.
     */
    li.d    $t1, -8192
    and     $t4, $t0, $t1
    ori     $t4, $t4, 12
    csrwr   $t4, CSR_TLBREHI

    /*
     * PA = BADV - KERNEL_OFFSET.
     */
    li.d    $t1, KERNEL_OFFSET
    sub.d   $t0, $t0, $t1

    /*
     * TLB entry covers even/odd page pair.
     * Clear low 13 bits to get even-page pair PA base.
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
    lu12i.w $t3, 1          /* 0x1000 */
    add.d   $t0, $t0, $t3
    or      $t2, $t0, $t1
    csrwr   $t2, CSR_TLBRELO1

    tlbfill

    csrrd   $t0, CSR_TLBRSAVE
    ertn