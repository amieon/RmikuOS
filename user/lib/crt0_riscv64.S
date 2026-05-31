    .section .text.entry
    .global _start
    .type _start, @function

_start:
    call main

    mv      a0, a0
    li      a7, 0
    ecall

1:
    j 1b