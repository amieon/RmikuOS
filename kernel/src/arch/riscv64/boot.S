为了实现多核启动，我们需要在汇编阶段区分主核（Hart 0）和从核（其他 Hart）。主核负责初始化早期页表并开启分页，然后通过一个内存标志位通知从核；从核在此期间轮询等待，直到主核完成后，再各自开启分页并跳转到高地址的 `rust_main`。由于你的 Rust 代码中已经实现了主从核在 `rust_main` 里的同步（`MASTER_READY`），汇编层面只需保证多核能同时进入高地址即可。

以下是修改后的多核启动汇编代码：

```assembly
    .section .text.boot, "ax"
    .globl _start
    .type _start, @function

    .equ KERNEL_OFFSET, 0xffffffc000000000
    .equ PAGE_SIZE,     4096

    /*
     * RISC-V Sv39:
     * satp.MODE = 8
     */
    .equ SATP_MODE_SV39, 0x8000000000000000

    /*
     * PTE flags:
     * V | R | W | X | A | D
     */
    .equ PTE_V, 0x001
    .equ PTE_R, 0x002
    .equ PTE_W, 0x004
    .equ PTE_X, 0x008
    .equ PTE_A, 0x040
    .equ PTE_D, 0x080
    .equ PTE_RWXAD, 0x0cf

    /*
     * QEMU virt DRAM starts at 0x80000000.
     * Kernel is loaded at 0x80200000.
     */
    .equ DRAM_BASE, 0x80000000

_start:
    /*
     * OpenSBI enters S-mode with:
     *   a0 = hartid
     *   a1 = dtb pointer
     */
    mv      tp, a0

    /*
     * Set a temporary physical stack before paging.
     * One stack per hart.
     */
    la      sp, boot_stack
    li      t0, 65536              /* KERNEL_STACK_SIZE */
    addi    t1, tp, 1
    mul     t0, t0, t1
    add     sp, sp, t0

    /* 判断当前核是否为主核 (hartid == 0) */
    bnez    tp, .Lsecondary_harts

.Lprimary_hart:
    /*
     * Clear early root page table.
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
     * ------------------------------------------------------------------
     * Entry 2:
     * Low temporary identity map: VA 0x80000000..0xbfffffff -> PA 0x80000000..0xbfffffff
     * ------------------------------------------------------------------
     */
    li      t1, DRAM_BASE
    srli    t1, t1, 2              /* (pa >> 12) << 10 == pa >> 2 */
    li      t2, PTE_RWXAD
    or      t1, t1, t2

    li      t3, 2                  /* VPN2 index of 0x80000000 */
    slli    t3, t3, 3
    add     t4, t0, t3
    sd      t1, 0(t4)

    /*
     * ------------------------------------------------------------------
     * Entry 258:
     * High direct map for DRAM: VA KERNEL_OFFSET + 0x80000000 -> PA 0x80000000
     * ------------------------------------------------------------------
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
     * ------------------------------------------------------------------
     * Entry 256:
     * High direct map for low MMIO area: VA KERNEL_OFFSET + 0x00000000 -> PA 0x00000000
     * ------------------------------------------------------------------
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
     * Enable Sv39 for primary hart.
     */
    la      t0, boot_page_table
    srli    t0, t0, 12
    li      t1, SATP_MODE_SV39
    or      t0, t0, t1

    csrw    satp, t0
    sfence.vma

    /* 通知从核：页表已初始化完毕，可以开启分页了 */
    la      t0, boot_page_table_ready
    li      t1, 1
    sd      t1, 0(t0)

    j       .Lswitch_to_high_half

.Lsecondary_harts:
    /* 从核等待主核将早期页表初始化完毕 */
    la      t0, boot_page_table_ready
2:
    ld      t1, 0(t0)
    beqz    t1, 2b

    /* 使用主核准备好的页表开启分页 */
    la      t0, boot_page_table
    srli    t0, t0, 12
    li      t1, SATP_MODE_SV39
    or      t0, t0, t1

    csrw    satp, t0
    sfence.vma

.Lswitch_to_high_half:
    /*
     * We are still executing through the temporary low identity map.
     * Now switch stack to its high virtual alias.
     */
    la      sp, boot_stack
    li      t0, 65536
    addi    t1, tp, 1
    mul     t0, t0, t1
    add     sp, sp, t0

    li      t0, KERNEL_OFFSET
    add     sp, sp, t0

    /*
     * Jump to high-half rust_main.
     *
     * _rust_main_phys is provided by linker:
     *   _rust_main_phys = rust_main - KERNEL_OFFSET;
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

    .align 12
boot_stack:
    .space 65536 * 8
boot_stack_top:
```