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

msg:
    .ascii "[raw.S] hello\n"