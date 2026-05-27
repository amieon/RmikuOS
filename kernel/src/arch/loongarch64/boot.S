    .section .text.boot, "ax"
    .global _start
    .type _start, @function

    .equ KERNEL_OFFSET, 0x9000000000000000
    .equ STACK_SIZE,    65536

    .equ CSR_CRMD, 0x0
    .equ CSR_DMW0, 0x180
    .equ CSR_DMW1, 0x181

    .equ CRMD_DA, 0x8
    .equ CRMD_PG, 0x10

    .equ DMW_PLV0,   0x1
    .equ DMW_MAT_CC, 0x10

_start:
    /*
     * Read CPU id.
     */
    csrrd   $tp, 0x20

    /*
     * Temporary low physical stack.
     */
    la.local $sp, boot_stack
    li.d     $t0, STACK_SIZE
    addi.d   $t1, $tp, 1
    mul.d    $t0, $t0, $t1
    add.d    $sp, $sp, $t0

    /*
     * DMW0: low identity window.
     * VA 0x0000_xxxx -> PA 0x0000_xxxx
     */
    li.d    $t0, DMW_PLV0 | DMW_MAT_CC
    csrwr   $t0, CSR_DMW0

    /*
     * DMW1: high direct map.
     * VA 0x9000_0000_xxxx_xxxx -> PA 0x0000_0000_xxxx_xxxx
     */
    li.d    $t0, KERNEL_OFFSET
    ori     $t0, $t0, DMW_PLV0 | DMW_MAT_CC
    csrwr   $t0, CSR_DMW1

    /*
     * Switch to mapped mode:
     * CRMD.DA = 0
     * CRMD.PG = 1
     */
    csrrd   $t0, CSR_CRMD
    li.d    $t1, -9
    and     $t0, $t0, $t1
    ori     $t0, $t0, CRMD_PG
    csrwr   $t0, CSR_CRMD
    ibar    0
    dbar    0

    /*
     * Switch stack to high alias.
     */
    li.d    $t0, KERNEL_OFFSET
    add.d   $sp, $sp, $t0

    /*
     * Clear high-half .bss through high direct-map alias.
     */
    la.local $t0, _sbss_phys
    la.local $t1, _ebss_phys

    li.d     $t2, KERNEL_OFFSET
    add.d    $t0, $t0, $t2
    add.d    $t1, $t1, $t2

1:
    bgeu    $t0, $t1, 2f
    st.d    $zero, $t0, 0
    addi.d  $t0, $t0, 8
    b       1b
2:

    /*
     * Jump to high-half rust_main:
     *
     * high rust_main = _rust_main_phys + KERNEL_OFFSET
     */
    la.local $t0, _rust_main_phys
    li.d     $t1, KERNEL_OFFSET
    add.d    $t0, $t0, $t1

    addi.d   $a0, $tp, 0
    jirl     $zero, $t0, 0

    .section .bss.boot, "aw", @nobits
    .align 12
boot_stack:
    .space STACK_SIZE * 8