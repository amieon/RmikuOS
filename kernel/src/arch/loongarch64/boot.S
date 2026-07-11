    .section .text.boot, "ax"
    .global _start
    .type _start, @function

    .equ KERNEL_OFFSET, 0xffff800000000000
    .equ KERNEL_PHYS_BASE, 0x01000000

    .equ STACK_SIZE,       65536

    .equ BOOT_MAP_SIZE,    0x10000000
    .equ BOOT_MAP_PAGES,   BOOT_MAP_SIZE / 4096
    .equ BOOT_PT_PAGES,    BOOT_MAP_SIZE / 0x200000

    .equ UART_PADDR,       0x1fe001e0
    .equ UART_PAGE_PADDR,  0x1fe00000

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

    .equ DMW_PLV0,   0x1
    .equ DMW_MAT_CC, 0x10

    .equ BOOT_VA_BASE,  KERNEL_OFFSET + KERNEL_PHYS_BASE
    .equ BOOT_DIR3_IDX, (BOOT_VA_BASE >> 39) & 0x1ff
    .equ BOOT_DIR2_IDX, (BOOT_VA_BASE >> 30) & 0x1ff
    .equ BOOT_DIR1_IDX, (BOOT_VA_BASE >> 21) & 0x1ff

    .equ UART_VA_BASE,  KERNEL_OFFSET + UART_PAGE_PADDR
    .equ UART_DIR3_IDX, (UART_VA_BASE >> 39) & 0x1ff
    .equ UART_DIR2_IDX, (UART_VA_BASE >> 30) & 0x1ff
    .equ UART_DIR1_IDX, (UART_VA_BASE >> 21) & 0x1ff
    .equ UART_PT_IDX,   (UART_VA_BASE >> 12) & 0x1ff

    .equ PTE_V,            0x01
    .equ PTE_D,            0x02
    .equ PTE_MAT_CC,       0x10
    .equ PTE_G,            0x40
    .equ PTE_P,            0x80
    .equ PTE_W,            0x100
    .equ PTE_DIR,          0x80
    .equ PTE_KERNEL_RWX,   0x1d3

    .macro PUTCH_PHYS ch
        li.d    $t6, UART_PADDR
    1:
        ld.bu   $t5, $t6, 5
        andi    $t5, $t5, 0x20
        beqz    $t5, 1b
        li.w    $t5, \ch
        st.b    $t5, $t6, 0
    .endm

    .macro PUTCH_REG reg
        li.d    $t6, UART_PADDR
    .Lputch_wait\@:
        ld.bu   $t5, $t6, 5
        andi    $t5, $t5, 0x20
        beqz    $t5, .Lputch_wait\@
        st.b    \reg, $t6, 0
    .endm

    .macro PUTHEX64 reg
        addi.d  $t3, \reg, 0
        li.d    $t4, 60
        li.d    $t0, 16
    .Lhex_loop\@:
        srl.d   $t1, $t3, $t4
        andi    $t1, $t1, 0xf
        li.d    $t2, 10
        blt     $t1, $t2, .Lhex_digit\@
        addi.d  $t1, $t1, 87
        b       .Lhex_out\@
    .Lhex_digit\@:
        addi.d  $t1, $t1, 48
    .Lhex_out\@:
        PUTCH_REG $t1
        addi.d  $t4, $t4, -4
        addi.d  $t0, $t0, -1
        bnez    $t0, .Lhex_loop\@
    .endm

_start:
    /*
     * 读 hart/core id。
     * 低 9 位作为 core id，避免 CSR 0x20 高位混入导致 boot_stack 越界。
     */
    csrrd   $tp, 0x20
    andi    $tp, $tp, 0x1ff

    /*
     * 每个 hart 先用物理地址栈。
     */
    la.local $sp, boot_stack
    li.d     $t0, STACK_SIZE
    addi.d   $t1, $tp, 1
    mul.d    $t0, $t0, $t1
    add.d    $sp, $sp, $t0

    /*
     * boot 阶段打开低地址物理直映，方便所有 hart 访问 boot_ready / boot 页表。
     */
    li.d    $t0, DMW_PLV0 | DMW_MAT_CC
    csrwr   $t0, CSR_DMW0
    csrwr   $zero, CSR_DMW1
    csrwr   $zero, CSR_DMW2
    csrwr   $zero, CSR_DMW3

    /*
     * LoongArch SMP: 只有 hart0 做 boot 初始化。
     * 其他 hart 等 boot_ready，不允许清 BSS / 清 boot 页表 / 构造 boot 页表。
     */
    beqz    $tp, .Lprimary_boot_init

.Lsecondary_wait_boot:
    la.local $t0, boot_ready
1:
    ld.d    $t1, $t0, 0
    beqz    $t1, 1b
    dbar    0
    b       .Lboot_common_after_init

.Lprimary_boot_init:
    PUTCH_PHYS 'c'

    /*
     * hart0 清 boot 页表区。
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
     * hart0 清 kernel .bss。
     */
    la.local $t0, _sbss_phys
    la.local $t1, _ebss_phys
.Lclear_kernel_bss_phys:
    bgeu    $t0, $t1, .Lclear_kernel_bss_phys_done
    st.d    $zero, $t0, 0
    addi.d  $t0, $t0, 8
    b       .Lclear_kernel_bss_phys
.Lclear_kernel_bss_phys_done:
    PUTCH_PHYS 'C'

    /*
     * root 所有项临时指向同一个 dir2。
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

    /*
     * dir2 所有项临时指向同一个 dir1。
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
     * 给 kernel high-half boot 映射挂 PT。
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
     * 叶子页表：KERNEL_PHYS_BASE 起的 BOOT_MAP_SIZE 映射到 high-half。
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
     * UART high-half mapping。
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

    dbar    0

    /*
     * boot 页表构造完成，允许其他 hart 进入公共分页初始化。
     */
    la.local $t0, boot_ready
    li.d     $t1, 1
    st.d     $t1, $t0, 0
    dbar     0

    b       .Lboot_common_after_init

/*
 * 每个 hart 都必须执行的分页初始化。
 */
.Lboot_common_after_init:
    la.local $t0, boot_pgd

    li.d    $t1, (12 << 0)  | (9 << 5)  |  (21 << 10) | (9 << 15) |  (30 << 20) | (9 << 25) |  (0 << 30)
    li.d    $t2, (39 << 0) | (9 << 6)

    csrwr   $t0, CSR_PGDL
    la.local $t0, boot_pgd
    csrwr   $t0, CSR_PGDH
    csrwr   $t1, CSR_PWCL
    csrwr   $t2, CSR_PWCH

    li.d    $t3, 12
    csrwr   $t3, CSR_STLBPS

    la.local $t4, __tlb_refill
    csrwr   $t4, CSR_TLBRENTRY

    la.local $t4, boot_exception
    csrwr   $t4, CSR_EENTRY

    tlbflush

    csrrd   $t0, CSR_CRMD
    li.d    $t1, -9
    and     $t0, $t0, $t1
    ori     $t0, $t0, CRMD_PG
    csrwr   $t0, CSR_CRMD

    ibar    0
    dbar    0

    /*
     * 切换到 high-half stack。
     */
    PUTCH_PHYS 'p'

    li.d    $t0, KERNEL_OFFSET
    add.d   $sp, $sp, $t0

    PUTCH_PHYS 's'

    la.local $t0, boot_high_trampoline
    addi.d  $a0, $tp, 0
    jirl    $zero, $t0, 0

    .align 12
    .equ CSR_ESTAT, 0x5
    .equ CSR_ERA,   0x6
    .equ CSR_BADV,  0x7
    .equ CSR_BADI,  0x8

    .macro PRINT_CSR tag, csr
        PUTCH_PHYS \tag
        PUTCH_PHYS '['
        csrrd   $t0, \csr
        PUTHEX64 $t0
        PUTCH_PHYS ']'
    .endm

boot_exception:
    PUTCH_PHYS 'E'
    PRINT_CSR 'S', CSR_ESTAT
    PRINT_CSR 'P', CSR_ERA
    PRINT_CSR 'I', CSR_BADI
    PRINT_CSR 'V', CSR_BADV

.Lboot_exception_hang:
    b .Lboot_exception_hang

    .section .text.highboot, "ax"
    .align 4
    .global boot_high_trampoline
    .type boot_high_trampoline, @function

boot_high_trampoline:
    b rust_main_false

    .align 4
    .global rust_main_false
    .type rust_main_false, @function

rust_main_false:
    /*
     * 你的链接方式下，la.local rust_main 得到低地址/物理符号，
     * 需要加 KERNEL_OFFSET 后跳入 high-half。
     */
    la.local $r22, rust_main
    li.d     $r23, KERNEL_OFFSET
    add.d    $r23, $r22, $r23

    addi.d   $sp, $sp, -16
    st.d     $ra, $sp, 8
    ld.d     $ra, $sp, 8
    addi.d   $sp, $sp, 16

    jirl     $ra, $r23, 0

.Lrust_main_hang:
    b .Lrust_main_hang

    /*
     * boot_ready 放在 data，不依赖 hart0 清 BSS 后才变成 0。
     */
    .section .data.boot, "aw"
    .align 3
boot_ready:
    .quad 0

    .section .bss.boot, "aw", @nobits
    .align 12

boot_pg_start:

boot_pgd:
    .space 4096

boot_dir2:
    .space 4096

boot_dir1:
    .space 4096

boot_uart_pt:
    .space 4096

boot_pt_pool:
    .space BOOT_PT_PAGES * 4096

boot_pg_end:

    .align 12
boot_stack:
    .space STACK_SIZE * 8
boot_stack_top: