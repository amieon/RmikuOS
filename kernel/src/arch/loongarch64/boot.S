/* kernel/src/arch/loongarch64/boot.S */

.equ BOOT_STACK_SIZE, 4096 * 4
.equ BOOT_MAX_HARTS, 8

.section .text.boot, "ax", @progbits
.global _entry
_entry:
    /*
     * QEMU/LoongArch 复位后通常处于直接地址翻译模式：
     *   CSR.CRMD.DA = 1, CSR.CRMD.PG = 0
     * 这里不要提前打开分页；先配置 DMW，后续内核初始化阶段再切换到映射地址模式。
     */

    /* DMWIN0: 0x8000... -> 物理 0x0，uncached，常用于设备空间 */
    li.d    $t0, 0x8000000000000001
    csrwr   $t0, 0x180

    /* DMWIN1: 0x9000... -> 物理 0x0，cached，后续若使用高地址映射可用 */
    li.d    $t0, 0x9000000000000011
    csrwr   $t0, 0x181

    /* PRMD / EUEN 清零：早期启动阶段不启用浮点/向量等扩展 */
    li.w    $t0, 0x0
    csrwr   $t0, 0x1
    csrwr   $t0, 0x2

    /* 读取 CPUID，并在还没有栈之前先做边界检查 */
    csrrd   $t0, 0x20
    li.w    $t1, BOOT_MAX_HARTS
    bgeu    $t0, $t1, .Lspin

    /* CPU0 清零 BSS；其他 CPU 等待 CPU0 清零完成，避免提前进入 Rust 访问 BSS */
    bnez    $t0, .Lwait_bss_done

    la.global $t0, _bss_start
    la.global $t1, _bss_end
.Lzero_bss:
    bgeu    $t0, $t1, .Lbss_done
    st.d    $zero, $t0, 0
    addi.d  $t0, $t0, 8
    b       .Lzero_bss

.Lbss_done:
    dbar    0
    la.global $t0, boot_bss_done
    li.w    $t1, 1
    st.w    $t1, $t0, 0
    dbar    0
    b       .Lstack_setup

.Lwait_bss_done:
    la.global $t1, boot_bss_done
.Lwait_bss_done_loop:
    ld.w    $t2, $t1, 0
    beqz    $t2, .Lwait_bss_done_loop
    dbar    0

.Lstack_setup:
    csrrd   $t1, 0x20
    addi.d  $tp, $t1, 0

    la.global $sp, boot_stack
    li.d    $t0, BOOT_STACK_SIZE
    addi.d  $t1, $t1, 1
    mul.d   $t0, $t0, $t1
    add.d   $sp, $sp, $t0

    /* LoongArch 调用约定：第一个参数放在 $a0，显式传 rust_main(id) */
    addi.d  $a0, $tp, 0
    bl      rust_main

.Lspin:
    b       .Lspin

.section .data.boot, "aw", @progbits
.align 3
.global boot_bss_done
boot_bss_done:
    .word 0
    .word 0

.section .bss.stack, "aw", @nobits
.align 12
.global boot_stack
boot_stack:
    .space BOOT_STACK_SIZE * BOOT_MAX_HARTS
.global boot_stack_top
boot_stack_top:
