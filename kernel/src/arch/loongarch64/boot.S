    .section .text.boot, "ax"
    .global _start
    .type _start, @function

    /*
     * Canonical high-half offset.
     * Do NOT use 0x9000... anymore.
     */
    .equ KERNEL_OFFSET, 0xffff800000000000

    .equ KERNEL_PHYS_BASE, 0x01000000
    .equ STACK_SIZE,       65536

    /*
     * Bootstrap mapping size.
     * 256MiB for early kernel/bootstrap mapping.
     */
    .equ BOOT_MAP_SIZE,    0x10000000
    .equ BOOT_MAP_PAGES,   BOOT_MAP_SIZE / 4096
    .equ BOOT_PT_PAGES,    BOOT_MAP_SIZE / 0x200000

    .equ UART_PADDR,       0x1fe001e0
    .equ UART_PAGE_PADDR,  0x1fe00000

    /*
     * CSR numbers.
     */
    .equ CSR_CRMD,      0x0
    .equ CSR_EENTRY,    0x0c
    .equ CSR_PGDL,      0x19
    .equ CSR_PGDH,      0x1a
    .equ CSR_PWCL,      0x1c
    .equ CSR_PWCH,      0x1d
    .equ CSR_STLBPS,    0x1e
    .equ CSR_TLBRENTRY, 0x88

    .equ CSR_DMW0,      0x180
    .equ CSR_DMW1,      0x181
    .equ CSR_DMW2,      0x182
    .equ CSR_DMW3,      0x183

    .equ CRMD_DA, 0x8
    .equ CRMD_PG, 0x10

    /*
     * DMW0 only for low boot identity.
     */
    .equ DMW_PLV0,   0x1
    .equ DMW_MAT_CC, 0x10

    /*
     * Indexes for canonical high-half direct map:
     *
     *   VA = KERNEL_OFFSET + PA
     */
    .equ BOOT_VA_BASE,  KERNEL_OFFSET + KERNEL_PHYS_BASE
    .equ BOOT_DIR3_IDX, (BOOT_VA_BASE >> 39) & 0x1ff
    .equ BOOT_DIR2_IDX, (BOOT_VA_BASE >> 30) & 0x1ff
    .equ BOOT_DIR1_IDX, (BOOT_VA_BASE >> 21) & 0x1ff

    .equ UART_VA_BASE,  KERNEL_OFFSET + UART_PAGE_PADDR
    .equ UART_DIR3_IDX, (UART_VA_BASE >> 39) & 0x1ff
    .equ UART_DIR2_IDX, (UART_VA_BASE >> 30) & 0x1ff
    .equ UART_DIR1_IDX, (UART_VA_BASE >> 21) & 0x1ff
    .equ UART_PT_IDX,   (UART_VA_BASE >> 12) & 0x1ff

    /*
     * Debug UART output through physical address.
     * Safe in boot because DMW0 low identity is enabled.
     */
    .macro PUTCH_PHYS ch
        li.d    $t6, UART_PADDR
    1:
        ld.bu   $t5, $t6, 5
        andi    $t5, $t5, 0x20
        beqz    $t5, 1b
        li.w    $t5, \ch
        st.b    $t5, $t6, 0
    .endm

    /*
     * LoongArch PTE bits.
     *
     * V        bit 0
     * D        bit 1
     * PLV0     bits 2..3 = 0
     * MAT_CC   bit 4
     * G        bit 6
     * P        bit 7
     * W        bit 8
     */
    .equ PTE_V,            0x01
    .equ PTE_D,            0x02
    .equ PTE_MAT_CC,       0x10
    .equ PTE_G,            0x40
    .equ PTE_P,            0x80
    .equ PTE_W,            0x100

    /*
     * Directory entries:
     * use P | V for better compatibility with hardware page walk.
     */
    .equ PTE_DIR,          0x80

    /*
     * Kernel RWX leaf:
     * V | D | MAT_CC | G | P | W
     */
    .equ PTE_KERNEL_RWX,   0x1d3


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
     * DMW1 is intentionally NOT set.
     */
    li.d    $t0, DMW_PLV0 | DMW_MAT_CC
    csrwr   $t0, CSR_DMW0

    csrwr   $zero, CSR_DMW1
    csrwr   $zero, CSR_DMW2
    csrwr   $zero, CSR_DMW3

    /*
     * Clear bootstrap page tables.
     */
    la.local $t0, boot_pg_start
    la.local $t1, boot_pg_end
.Lclear_boot_pg:
    bgeu    $t0, $t1, .Lclear_boot_pg_done
    st.d    $zero, $t0, 0
    addi.d  $t0, $t0, 8
    b       .Lclear_boot_pg
.Lclear_boot_pg_done:

    /*
    * Build bootstrap page table.
    *
    * Normally:
    *   root[BOOT_DIR3_IDX] -> boot_dir2
    *
    * Debug/safe alias:
    *   root[0] -> boot_dir2
    *
    * This helps detect whether the hardware page-walk uses the index
    * we expected for 0xffff8000....
    */

    /*
    * Debug-safe bootstrap:
    * Fill every root entry with boot_dir2.
    */
    li.d     $t0, 0
    li.d     $t1, 512
    la.local $t2, boot_pgd
    la.local $t3, boot_dir2
    ori      $t3, $t3, PTE_DIR

    .Lfill_root_all:
        bgeu    $t0, $t1, .Lfill_root_all_done
        slli.d  $t4, $t0, 3
        add.d   $t5, $t2, $t4
        st.d    $t3, $t5, 0
        addi.d  $t0, $t0, 1
        b       .Lfill_root_all

    .Lfill_root_all_done:

    /* root[0] = boot_dir2 | P */
    la.local $t0, boot_pgd
    la.local $t2, boot_dir2
    ori      $t2, $t2, PTE_DIR
    st.d     $t2, $t0, 0

    /*
    * Debug-safe bootstrap:
    * Fill every dir2 entry with boot_dir1.
    */
    li.d     $t0, 0
    li.d     $t1, 512
    la.local $t2, boot_dir2
    la.local $t3, boot_dir1
    ori      $t3, $t3, PTE_DIR

    .Lfill_dir2_all:
        bgeu    $t0, $t1, .Lfill_dir2_all_done
        slli.d  $t4, $t0, 3
        add.d   $t5, $t2, $t4
        st.d    $t3, $t5, 0
        addi.d  $t0, $t0, 1
        b       .Lfill_dir2_all

    .Lfill_dir2_all_done:

    /*
     * Fill boot_dir1 entries:
     *
     * boot_dir1[BOOT_DIR1_IDX + i] -> boot_pt_pool[i]
     */
    li.d     $t0, 0
    li.d     $t1, BOOT_PT_PAGES
.Lfill_dir1:
    bgeu     $t0, $t1, .Lfill_dir1_done

    la.local $t2, boot_dir1
    li.d     $t3, BOOT_DIR1_IDX
    add.d    $t3, $t3, $t0
    slli.d   $t3, $t3, 3
    add.d    $t2, $t2, $t3

    la.local $t4, boot_pt_pool
    slli.d   $t5, $t0, 12
    add.d    $t4, $t4, $t5
    ori      $t4, $t4, PTE_DIR
    st.d     $t4, $t2, 0

    addi.d   $t0, $t0, 1
    b        .Lfill_dir1
.Lfill_dir1_done:

    /*
     * Fill leaf PTEs:
     *
     * PA 0x0100_0000 .. 0x1100_0000
     * VA KERNEL_OFFSET + PA
     */
    li.d     $t0, 0
    li.d     $t1, BOOT_MAP_PAGES
    li.d     $t2, KERNEL_PHYS_BASE
    la.local $t3, boot_pt_pool
.Lfill_leaf:
    bgeu     $t0, $t1, .Lfill_leaf_done

    slli.d   $t4, $t0, 12
    add.d    $t5, $t2, $t4
    ori      $t5, $t5, PTE_KERNEL_RWX

    slli.d   $t6, $t0, 3
    add.d    $t6, $t3, $t6
    st.d     $t5, $t6, 0

    addi.d   $t0, $t0, 1
    b        .Lfill_leaf
.Lfill_leaf_done:

    /*
     * Map UART page:
     *
     * PA 0x1fe0_0000
     * VA KERNEL_OFFSET + 0x1fe0_0000
     */
    la.local $t0, boot_dir1
    li.d     $t1, UART_DIR1_IDX
    slli.d   $t1, $t1, 3
    add.d    $t0, $t0, $t1

    la.local $t2, boot_uart_pt
    ori      $t2, $t2, PTE_DIR
    st.d     $t2, $t0, 0

    la.local $t0, boot_uart_pt
    li.d     $t1, UART_PT_IDX
    slli.d   $t1, $t1, 3
    add.d    $t0, $t0, $t1

    li.d     $t1, UART_PAGE_PADDR
    ori      $t1, $t1, PTE_KERNEL_RWX
    st.d     $t1, $t0, 0

    /*
     * Configure page walk.
     *
     * PT    VA[20:12]
     * Dir1  VA[29:21]
     * Dir2  VA[38:30]
     * Dir3  VA[47:39]
     *
     * TLB refill uses lddir 4,3,2.
     */
   /*
    * Make page-table writes visible before enabling page walk.
    */
    dbar 0

    la.local $t0, boot_pgd

    li.d    $t1, (12 << 0)  | (9 << 5)  | (21 << 10) | (9 << 15) |  (30 << 20) | (9 << 25) |  (0 << 30)

    li.d    $t2, (39 << 0) | (9 << 6)

    /*
    * csrwr clobbers its source register.
    * So reload boot_pgd before writing PGDH.
    */
    csrwr   $t0, CSR_PGDL

    la.local $t0, boot_pgd
    csrwr   $t0, CSR_PGDH

    /*
    * Do NOT write these twice.
    */
    csrwr   $t1, CSR_PWCL
    csrwr   $t2, CSR_PWCH

    li.d    $t3, 12
    csrwr   $t3, CSR_STLBPS

    /*
     * TLB refill entry must be physical address.
     */
    la.local $t4, __tlb_refill
    li.d     $t5, KERNEL_OFFSET
    sub.d    $t4, $t4, $t5
    csrwr    $t4, CSR_TLBRENTRY

    /*
     * Emergency normal-exception entry during boot.
     */
    la.local $t4, boot_exception
    csrwr    $t4, CSR_EENTRY

    tlbflush

    /*
     * Switch to mapped mode.
     *
     * Low boot code remains available through DMW0.
     * Canonical high-half kernel uses bootstrap page table.
     */
    csrrd   $t0, CSR_CRMD
    li.d    $t1, -9
    and     $t0, $t0, $t1
    ori     $t0, $t0, CRMD_PG
    csrwr   $t0, CSR_CRMD
    ibar    0
    dbar    0

    PUTCH_PHYS 'p'

    /*
     * Switch stack to canonical high-half alias.
     */
    li.d    $t0, KERNEL_OFFSET
    add.d   $sp, $sp, $t0

    PUTCH_PHYS 's'

    /*
     * Prepare high-half .bss addresses.
        */
    /*
    * Prepare high-half .bss addresses.
    *
    * Use high-half linker symbols directly.
    * Do NOT use _sbss_phys + KERNEL_OFFSET here.
    */
    la.local $t0, _sbss
    la.local $t1, _ebss

    PUTCH_PHYS 'b'

    /*
     * Test high-half rust_main read mapping.
     * If this succeeds, print 'r'.
     */
    la.local $t3, rust_main
    ld.d     $t5, $t3, 0
    PUTCH_PHYS 'r'

    /*
     * Clear high-half .bss through page-table mapping.
     */
.Lclear_bss:
    bgeu    $t0, $t1, .Lclear_bss_done
    st.d    $zero, $t0, 0
    addi.d  $t0, $t0, 8
    b       .Lclear_bss
.Lclear_bss_done:

    PUTCH_PHYS 'B'

    /*
     * Jump to high-half rust_main.
     */
    la.local $t0, boot_high_trampoline

    PUTCH_PHYS 'j'

    addi.d   $a0, $tp, 0
    jirl     $zero, $t0, 0


    .align 4
boot_to_rust:
    /*
     * If you see R, high-half jirl works and we reached this trampoline.
     */
    PUTCH_PHYS 'R'

    /*
     * Now jump to real Rust entry.
     */
    la.local $t0, rust_main
    jirl     $zero, $t0, 0


    /*
     * Emergency exception handler before Rust trap is initialized.
     * Prints E + ecode hex.
     */
    .align 12
boot_exception:
    PUTCH_PHYS 'E'

    /*
     * CSR.ESTAT = 0x5
     * ecode = ESTAT[21:16]
     */
    csrrd   $t0, 0x5
    srli.d  $t0, $t0, 16
    andi    $t0, $t0, 0x3f

    /*
     * High hex nibble.
     */
    srli.d  $t1, $t0, 4
    andi    $t1, $t1, 0xf

    li.d    $t2, 10
    blt     $t1, $t2, .Lecode_hi_digit
    addi.d  $t1, $t1, 87
    b       .Lecode_hi_out
.Lecode_hi_digit:
    addi.d  $t1, $t1, 48
.Lecode_hi_out:
    li.d    $t6, UART_PADDR
.Lecode_hi_wait:
    ld.bu   $t5, $t6, 5
    andi    $t5, $t5, 0x20
    beqz    $t5, .Lecode_hi_wait
    st.b    $t1, $t6, 0

    /*
     * Low hex nibble.
     */
    andi    $t1, $t0, 0xf

    li.d    $t2, 10
    blt     $t1, $t2, .Lecode_lo_digit
    addi.d  $t1, $t1, 87
    b       .Lecode_lo_out
.Lecode_lo_digit:
    addi.d  $t1, $t1, 48
.Lecode_lo_out:
    li.d    $t6, UART_PADDR
.Lecode_lo_wait:
    ld.bu   $t5, $t6, 5
    andi    $t5, $t5, 0x20
    beqz    $t5, .Lecode_lo_wait
    st.b    $t1, $t6, 0

.Lboot_exception_hang:
    b .Lboot_exception_hang


    .section .bss.boot, "aw", @nobits
    .align 12

boot_pg_start:

boot_pgd:
    .space 4096

boot_dir2:
    .space 4096

boot_dir1:
    .space 4096

/*
 * BOOT_MAP_SIZE / 2MiB PT pages.
 */
boot_pt_pool:
    .space 4096 * BOOT_PT_PAGES

boot_uart_pt:
    .space 4096

boot_pg_end:

    .align 12
boot_stack:
    .space STACK_SIZE * 8




.section .text.highboot, "ax"
.align 4
.global boot_high_trampoline
boot_high_trampoline:
    PUTCH_PHYS 'H'

    /*
     * Do not use la.local rust_main here.
     * Use physical symbol + KERNEL_OFFSET, same as early boot style.
     */
    la.local $t0, _rust_main_phys
    li.d     $t1, KERNEL_OFFSET
    add.d    $t0, $t0, $t1

    PUTCH_PHYS 'h'

    jirl     $zero, $t0, 0