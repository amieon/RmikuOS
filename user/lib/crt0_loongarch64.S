    .section .text.entry
    .global _start
    .type _start, @function

_start:
    bl      main

    addi.d  $r11, $r0, 0
    syscall 0

1:
    b 1b