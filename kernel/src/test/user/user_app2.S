    .section .text
    .global _start
    .type _start, @function

_start:
    /*
     * write(1, msg0, 21)
     */
    addi.d   $r4,  $r0, 1
    la.local $r5, msg0
    addi.d   $r6,  $r0, 21
    addi.d   $r11, $r0, 2
    syscall 0

    /*
     * yield()
     */
    addi.d   $r11, $r0, 1
    syscall 0

    /*
     * write(1, msg1, 20)
     */
    addi.d   $r4,  $r0, 1
    la.local $r5, msg1
    addi.d   $r6,  $r0, 20
    addi.d   $r11, $r0, 2
    syscall 0

    /*
     * exit(0)
     */
    addi.d   $r4,  $r0, 0
    addi.d   $r11, $r0, 0
    syscall 0

1:
    b 1b

msg0:
    .ascii "[user0] before yield\n"

msg1:
    .ascii "[user0] after yield\n"