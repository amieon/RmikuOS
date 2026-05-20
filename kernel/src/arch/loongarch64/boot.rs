// kernel/src/arch/loongarch64/boot.rs
use core::arch::global_asm;

global_asm!(
    r#"
.section .text.boot
.global _entry
_entry:
    /* DMWIN0: 0x8000... -> 物理 0x0 (uncached，设备空间) */
    li.d    $t0, 0x8000000000000001
    csrwr   $t0, 0x180

    /* DMWIN1: 0x9000... -> 物理 0x0 (cached，后续若用高地址) */
    li.d    $t0, 0x9000000000000011
    csrwr   $t0, 0x181

    /* 注意：不要写 CRMD！保持复位默认值 DA=0，QEMU 才能正常取指 */
    /* li.w    $t0, 0xb0 */
    /* csrwr   $t0, 0x0 */

    /* PRMD / EUEN 清零（可选，不影响启动） */
    li.w    $t0, 0x0
    csrwr   $t0, 0x1
    csrwr   $t0, 0x2

    /* CPU0 清零 BSS */
    csrrd   $t0, 0x20
    bnez    $t0, .Lstack_setup

    la.global   $t0, _bss_start
    la.global   $t1, _bss_end
.Lzero_bss:
    st.d    $zero, $t0, 0
    addi.d  $t0, $t0, 8
    bne     $t0, $t1, .Lzero_bss

.Lstack_setup:
    la.global   $sp, boot_stack
    li.d    $t0, 4096
    csrrd   $t1, 0x20
    addi.d  $tp, $t1, 0
    addi.d  $t1, $t1, 1
    mul.d   $t0, $t0, $t1
    add.d   $sp, $sp, $t0

    bl      rust_main

.Lspin:
    b       .Lspin

    .section .bss.stack
    .align 12
    .globl boot_stack
boot_stack:
    .space 4096 * 4 * 4
    .globl boot_stack_top
boot_stack_top:
"#
);