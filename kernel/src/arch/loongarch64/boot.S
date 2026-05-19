// kernel/src/arch/loongarch64/boot.rs

use core::arch::global_asm;

global_asm!(
    r#"
    .equ LOONGARCH_CSR_CRMD,     0x0
    .equ LOONGARCH_CSR_PRMD,     0x1
    .equ LOONGARCH_CSR_EUEN,     0x2
    .equ LOONGARCH_CSR_DMWIN0,   0x180
    .equ LOONGARCH_CSR_DMWIN1,   0x181
    .equ LOONGARCH_CSR_DMWIN2,   0x182
    .equ LOONGARCH_CSR_DMWIN3,   0x183
    .equ LOONGARCH_CSR_TLBRENTRY, 0x88
    .equ LOONGARCH_CSR_CPUID,    0x20


    .section .text.init
    .globl _entry
_entry:
    // DMW 配置
    li.d        $t0, 0x8000000000000001
    csrwr       $t0, 0x180       // LOONGARCH_CSR_DMWIN0
    li.d        $t0, (0x9000000000000001 | 1 << 4)
    csrwr       $t0, 0x181       // LOONGARCH_CSR_DMWIN1
    // 关闭 DMW2 和 DMW3
    li.d        $t0, 0x0
    csrwr       $t0, 0x182       // LOONGARCH_CSR_DMWIN2
    csrwr       $t0, 0x183       // LOONGARCH_CSR_DMWIN3
    csrwr       $t0, 0x88        // LOONGARCH_CSR_TLBRENTRY

    // CPU 状态
    li.w        $t0, 0xb0
    csrwr       $t0, 0x0         // LOONGARCH_CSR_CRMD
    li.w        $t0, 0x00
    csrwr       $t0, 0x1         // LOONGARCH_CSR_PRMD
    li.w        $t0, 0x00
    csrwr       $t0, 0x2         // LOONGARCH_CSR_EUEN

    // 栈设置
    la.global   $sp, boot_stack
    li.d        $a0, 4096
    csrrd       $a1, 0x20        // LOONGARCH_CSR_CPUID
    addi.d      $tp, $a1, 0
    addi.d      $a1, $a1, 1
    mul.d       $a0, $a0, $a1
    add.d       $sp, $sp, $a0

    // 读取 CPU 配置，跳转到 Rust
    li.d        $t0, 0x1
    cpucfg      $t1, $t0
    bl          rust_main

spin:
    b           spin

    .section .bss.stack
    .balign 12
    .globl boot_stack
boot_stack:
    .space 4096 * 4 * 4
    .globl boot_stack_top
boot_stack_top:
"#
);