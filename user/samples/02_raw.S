    .section .text.entry
    .global _start
    .type _start, @function

_start:
#if defined(USER_ARCH_RISCV64)
    li      a0, 1
    la      a1, msg
    li      a2, 16
    li      a7, 2
    ecall

    li      a0, 2
    li      a7, 0
    ecall

1:
    j 1b

#elif defined(USER_ARCH_LOONGARCH64)
    addi.d   $r4,  $r0, 1
    la.local $r5, msg
    addi.d   $r6,  $r0, 16
    addi.d   $r11, $r0, 2
    syscall 0

    addi.d   $r4,  $r0, 2
    addi.d   $r11, $r0, 0
    syscall 0

1:
    b 1b
#else
#error unsupported user arch
#endif

    /* 让段内数据 4 字节对齐: 否则 .text.entry 段大小可能非 4 的倍数,
     * 导致链接脚本中紧随其后的 .text 段(含 syscall3)起始地址错位,
     * 跨段 bl(R_LARCH_B26)的偏移未对齐而链接失败(LoongArch 特有)。 */
    .balign 4
msg:
    .ascii "[raw.S] hello\n"