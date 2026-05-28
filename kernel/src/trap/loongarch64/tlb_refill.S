    .section .text.tlb_refill, "ax"
    .global __tlb_refill
    .type __tlb_refill, @function
    .balign 4096

    .equ UART_PADDR, 0x1fe001e0

__tlb_refill:
    csrwr   $t0, 0x8b

    /*
     * Debug only: print A.
     * This clobbers t1/t2, but right now we only use it to confirm entry.
     */
    li.d    $t1, UART_PADDR
1:
    ld.bu   $t2, $t1, 5
    andi    $t2, $t2, 0x20
    beqz    $t2, 1b
    li.w    $t2, 'A'
    st.b    $t2, $t1, 0

    csrrd   $t0, 0x1b

    lddir   $t0, $t0, 4
    lddir   $t0, $t0, 3
    lddir   $t0, $t0, 2

    ldpte   $t0, 0
    ldpte   $t0, 1

    tlbfill

    csrrd   $t0, 0x8b
    ertn