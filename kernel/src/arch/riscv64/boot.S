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
    .equ BOOT_STACK_SIZE, 65536

_start:
    /*
     * OpenSBI 进入 S-mode 时：
     *   a0 = hartid
     *   a1 = dtb / opaque
     *
     * 内核约定 tp 保存当前 hart id。
     */
    mv      tp, a0

    /*
     * 还没有开分页，先用物理启动栈。
     */
    la      sp, boot_stack
    li      t0, BOOT_STACK_SIZE
    addi    t1, tp, 1
    mul     t0, t0, t1
    add     sp, sp, t0

    /*
     * OpenSBI 不保证 boot hart 是 0。
     * 所以这里不能 bnez tp, secondary。
     *
     * 谁第一个把 boot_primary_claimed 从 0 换成 1，
     * 谁就是 early primary，负责建立 early page table。
     */
    la      t0, boot_primary_claimed
    li      t1, 1
    amoswap.w.aqrl t2, t1, (t0)

    beqz    t2, .Lprimary_hart
    j       .Lsecondary_harts


.Lprimary_hart:
    /*
     * 清空 early boot page table。
     */
    la      t0, boot_page_table
    li      t1, 512
1:
    sd      zero, 0(t0)
    addi    t0, t0, 8
    addi    t1, t1, -1
    bnez    t1, 1b

    la      t0, boot_page_table

    /*
     * 映射 0x80000000..0xbfffffff identity。
     * Sv39 top-level index = 2。
     */
    li      t1, DRAM_BASE
    srli    t1, t1, 2
    li      t2, PTE_RWXAD
    or      t1, t1, t2

    li      t3, 2
    slli    t3, t3, 3
    add     t4, t0, t3
    sd      t1, 0(t4)

    /*
     * high-half 映射：
     * 0xffffffc000000000 -> 0x80000000
     * Sv39 top-level index = 258。
     */
    li      t1, DRAM_BASE
    srli    t1, t1, 2
    li      t2, PTE_RWXAD
    or      t1, t1, t2

    li      t3, 258
    slli    t3, t3, 3
    add     t4, t0, t3
    sd      t1, 0(t4)

    /*
     * 保留你原来的低 1GiB identity map。
     * Sv39 top-level index = 256。
     */
    li      t1, 0
    srli    t1, t1, 2
    li      t2, PTE_RWXAD
    or      t1, t1, t2

    li      t3, 256
    slli    t3, t3, 3
    add     t4, t0, t3
    sd      t1, 0(t4)

    /*
     * 启用 early page table。
     */
    la      t0, boot_page_table
    srli    t0, t0, 12
    li      t1, SATP_MODE_SV39
    or      t0, t0, t1

    csrw    satp, t0
    sfence.vma

    /*
     * 通知 secondary：early page table 已经准备好。
     */
    fence   rw, rw

    la      t0, boot_page_table_ready
    li      t1, 1
    sd      t1, 0(t0)

    fence   rw, rw

    j       .Lswitch_to_high_half


.Lsecondary_harts:
    /*
     * 其他 hart 等 primary 建好 early page table。
     */
    la      t0, boot_page_table_ready
2:
    ld      t1, 0(t0)
    beqz    t1, 2b

    fence   rw, rw

    /*
     * secondary 也启用同一张 early page table。
     */
    la      t0, boot_page_table
    srli    t0, t0, 12
    li      t1, SATP_MODE_SV39
    or      t0, t0, t1

    csrw    satp, t0
    sfence.vma


.Lswitch_to_high_half:
    /*
     * high-half 栈。
     */
    la      sp, boot_stack
    li      t0, BOOT_STACK_SIZE
    addi    t1, tp, 1
    mul     t0, t0, t1
    add     sp, sp, t0

    li      t0, KERNEL_OFFSET
    add     sp, sp, t0

    /*
     * 跳到 high-half rust_main。
     */
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

    /*
     * early primary 抢占标志。
     * 0 = 还没有 primary
     * 1 = 已经有 primary
     */
    .align 3
boot_primary_claimed:
    .space 8

    .align 12
boot_stack:
    .space BOOT_STACK_SIZE * 8
boot_stack_top: