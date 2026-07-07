.section .text.boot, "ax"
    .globl _start
    .type _start, @function

    .equ KERNEL_OFFSET, 0xffffffc000000000
    .equ PAGE_SIZE,     4096

    .equ SATP_MODE_SV39, 0x8000000000000000

    .equ PTE_V, 0x001
    .equ PTE_R, 0x002
    .equ PTE_W, 0x004
    .equ PTE_X, 0x008
    .equ PTE_A, 0x040
    .equ PTE_D, 0x080
    .equ PTE_RWXAD, 0x0cf

    .equ DRAM_BASE, 0x80000000

_start:
    mv      tp, a0


    la      sp, boot_stack
    li      t0, 65536
    addi    t1, tp, 1
    mul     t0, t0, t1
    add     sp, sp, t0

    bnez    tp, .Lsecondary_harts

.Lprimary_hart:
    la      t0, boot_page_table
    li      t1, 512
1:
    sd      zero, 0(t0)
    addi    t0, t0, 8
    addi    t1, t1, -1
    bnez    t1, 1b

    la      t0, boot_page_table

    li      t1, DRAM_BASE
    srli    t1, t1, 2
    li      t2, PTE_RWXAD
    or      t1, t1, t2
    li      t3, 2
    slli    t3, t3, 3
    add     t4, t0, t3
    sd      t1, 0(t4)

    li      t1, DRAM_BASE
    srli    t1, t1, 2
    li      t2, PTE_RWXAD
    or      t1, t1, t2
    li      t3, 258
    slli    t3, t3, 3
    add     t4, t0, t3
    sd      t1, 0(t4)

    li      t1, 0
    srli    t1, t1, 2
    li      t2, PTE_RWXAD
    or      t1, t1, t2
    li      t3, 256
    slli    t3, t3, 3
    add     t4, t0, t3
    sd      t1, 0(t4)

    la      t0, boot_page_table
    srli    t0, t0, 12
    li      t1, SATP_MODE_SV39
    or      t0, t0, t1
    csrw    satp, t0
    sfence.vma

    la      t0, boot_page_table_ready
    li      t1, 1
    sd      t1, 0(t0)

    j       .Lswitch_to_high_half

.Lsecondary_harts:
    la      t0, boot_page_table_ready
2:
    ld      t1, 0(t0)
    beqz    t1, 2b

    la      t0, boot_page_table
    srli    t0, t0, 12
    li      t1, SATP_MODE_SV39
    or      t0, t0, t1
    csrw    satp, t0
    sfence.vma

.Lswitch_to_high_half:
    la      sp, boot_stack
    li      t0, 65536
    addi    t1, tp, 1
    mul     t0, t0, t1
    add     sp, sp, t0

    li      t0, KERNEL_OFFSET
    add     sp, sp, t0

    la      t0, _rust_main_phys
    li      t1, KERNEL_OFFSET
    add     t0, t0, t1

    mv      a0, tp
    jr      t0

    .section .bss.boot, "aw", @nobits
    .align 12
boot_page_table:
    .space 4096

    .align 3
boot_page_table_ready:
    .space 8

    .align 12
boot_stack:
    .space 65536 * 8
boot_stack_top: