    .section .text
    .global _start
    .type _start, @function

_start:
    /*
     * write(1, msg0, 20)
     */
    addi.d   $r4,  $r0, 1
    la.local $r5, msg0
    addi.d   $r6,  $r0, 20
    addi.d   $r11, $r0, 2
    syscall 0

    /*
     * busy loop
     */
    li.d     $r12, 200000
1:
    addi.d   $r12, $r12, -1
    bnez     $r12, 1b

    /*
     * write(1, msg1, 19)
     */
    addi.d   $r4,  $r0, 1
    la.local $r5, msg1
    addi.d   $r6,  $r0, 19
    addi.d   $r11, $r0, 2
    syscall 0

    /*
     * exit(1)
     */
    addi.d   $r4,  $r0, 1
    addi.d   $r11, $r0, 0
    syscall 0

2:
    b 2b

msg0:
    .ascii "[user1] busy start\n"

msg1:
    .ascii "[user1] busy end\n"